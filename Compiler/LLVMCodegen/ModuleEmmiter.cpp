#include "ModuleEmmiter.hpp"

#include "FunctionEmiiter.hpp"
#include "LLVMCodegenerator.hpp"
#include "Compiler/IR/Backend/model/function.hpp"
#include "Compiler/IR/Backend/model/module.hpp"
#include "includes/String.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <stdexcept>
#include <string>

namespace sakuraE::Codegen {
    // 仅绑定 Backend 模块与上下文；ID 引用 source.ID，生命周期跟随 BackendModule
    ModuleEmmiter::ModuleEmmiter(const IR::BackendModule& source, LLVMCodeGenerator& codegen):
        backend(source), ID(source.ID), codegenContext(codegen) {}

    // 只释放 FunctionEmiiter；llvm::Module (content) 可能已被 JIT 接管，不在此释放
    ModuleEmmiter::~ModuleEmmiter() {
        for (auto& pair : functions) {
            if (pair.second) delete pair.second;
        }
    }

    // 发射 __gc_get_atomic_type() 调用：用于无内部引用、GC 无需扫描的对象
    llvm::Value* ModuleEmmiter::getAtomicGCType() {
        // runtime GC 工厂不在 BackendModule 的符号表中，按 C 签名直接 getOrInsertFunction 声明
        auto callee = content->getOrInsertFunction(
            "__gc_get_atomic_type",
            llvm::FunctionType::get(codegenContext.builder->getPtrTy(), false)
        );
        return codegenContext.builder->CreateCall(callee, {});
    }

    llvm::Value* ModuleEmmiter::getArrayGCType(bool isPtr, uint32_t memberSize, llvm::Value* memTy, uint64_t length) {
        // 将 LLVM 数组布局转换为运行时扫描描述符，length 用于嵌入式数组递归扫描
        auto callee = content->getOrInsertFunction(
            "__gc_get_array_type_with_length",
            llvm::FunctionType::get(
                codegenContext.builder->getPtrTy(),
                {
                    llvm::Type::getInt1Ty(*codegenContext.context),
                    llvm::Type::getInt32Ty(*codegenContext.context),
                    llvm::Type::getInt64Ty(*codegenContext.context),
                    codegenContext.builder->getPtrTy()
                },
                false
            )
        );
        return codegenContext.builder->CreateCall(
            callee,
            {
                codegenContext.builder->getInt1(isPtr),
                codegenContext.builder->getInt32(memberSize),
                codegenContext.builder->getInt64(length),
                memTy
            }
        );
    }

    // 发射 __gc_get_runtime_value_array_type(memberSize, length)：
    // 元素槽位存放 RuntimeValue*（装箱值）或托管数组指针，运行时按 boxed_value 规则扫描每个元素
    llvm::Value* ModuleEmmiter::getRuntimeValueArrayGCType(uint32_t memberSize, uint64_t length) {
        auto callee = content->getOrInsertFunction(
            "__gc_get_runtime_value_array_type",
            llvm::FunctionType::get(
                codegenContext.builder->getPtrTy(),
                {
                    llvm::Type::getInt32Ty(*codegenContext.context),
                    llvm::Type::getInt64Ty(*codegenContext.context)
                },
                false
            )
        );
        return codegenContext.builder->CreateCall(callee, {
            codegenContext.builder->getInt32(memberSize),
            codegenContext.builder->getInt64(length)
        });
    }

    // 由 LLVM 类型推导 GC 描述符；空类型按原子处理
    llvm::Value* ModuleEmmiter::llvmTy2GCType(llvm::Type* ty) {
        if (!ty) {
            return getAtomicGCType();
        }

        // 数组：指针元素直接以原子描述符作为成员类型（指针本身由 isPtr 标记扫描），
        // 非指针元素递归推导其描述符，并携带嵌入式数组长度
        if (ty->isArrayTy()) {
            auto* arrTy = llvm::cast<llvm::ArrayType>(ty);
            llvm::Type* elemTy = arrTy->getElementType();
            bool elemIsPtr = elemTy->isPointerTy();
            uint32_t elemSize = static_cast<uint32_t>(
                content->getDataLayout().getTypeAllocSize(elemTy)
            );
            llvm::Value* elemGcTy = elemIsPtr ? getAtomicGCType() : llvmTy2GCType(elemTy);

            return getArrayGCType(elemIsPtr, elemSize, elemGcTy, arrTy->getNumElements());
        }

        // 单个指针：描述为元素为指针、长度为 0（未定）的数组
        if (ty->isPointerTy()) {
            uint32_t ptrSize = static_cast<uint32_t>(content->getDataLayout().getPointerSize());
            return getArrayGCType(true, ptrSize, getAtomicGCType());
        }

        // 其余标量/结构体等一律视为原子（不扫描内部引用）
        return getAtomicGCType();
    }

    // 先通过 BackendModule::lookup 按源名/linkage 名解析到 BackendFunction（未找到为 nullptr），
    // 再查本模块的发射器表；本模块未声明该函数时抛错
    FunctionEmiiter* ModuleEmmiter::lookup(const fzlib::String& name) const {
        const auto* function = backend.lookup(name);
        auto found = functions.find(function);
        if (found != functions.end()) {
            return found->second;
        }
        throw std::runtime_error(fzlib::String("Try to call an unknown function: \"" + name + "\"").c_str());
    }

    void ModuleEmmiter::impl() {
        // 每个模块一个独立的 llvm::Module，共享同一个 LLVMContext（结构体类型可跨模块复用）
        content = new llvm::Module(ID.c_str(), *codegenContext.context);

        // 本模块定义的函数：__runtime 模块中的函数 linkage 为 External，只生成声明
        for (const auto& function : backend.functions) {
            functions.emplace(function.get(), new FunctionEmiiter(
                *function, function->linkage == IR::Linkage::External, this, codegenContext));
        }
        // 从 using 模块（含 runtime）导入的函数：在本模块内只生成外部声明，由 JIT/链接器解析到定义
        for (const auto* function : backend.imports) {
            if (!functions.contains(function)) {
                functions.emplace(function, new FunctionEmiiter(*function, true, this, codegenContext));
            }
        }
        // 所有发射器登记完成后再统一创建 LLVM 函数声明，使后续函数体可调用任意可见函数
        for (const auto& [source, function] : functions) {
            function->impl();
        }
    }

    void ModuleEmmiter::codegen() {
        // 只遍历本模块定义的函数；导入函数仅是声明，无需生成或校验
        for (const auto& source : backend.functions) {
            FunctionEmiiter* curFn = functions.at(source.get());

            // external 函数（runtime 声明）没有函数体，跳过生成
            if (!curFn->external)
                curFn->codegen();

            // verifyFunction 返回 true 表示校验失败；把诊断与当前已生成的全部 LLVM IR 一并放入异常信息
            std::string stdstr;
            llvm::raw_string_ostream rstrs(stdstr);
            if (llvm::verifyFunction(*curFn->content, &rstrs)) {
                rstrs.flush();
                throw std::runtime_error((fzlib::String("LLVM Verification Failed for module " +
                    curFn->content->getName().str() + ":\n" + stdstr + "\n Error IR: \n") + this->codegenContext.toString()).c_str());
            }
        }
    }
}
