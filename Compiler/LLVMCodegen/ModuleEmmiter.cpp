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
    ModuleEmmiter::ModuleEmmiter(const IR::BackendModule& source, LLVMCodeGenerator& codegen):
        backend(source), ID(source.ID), codegenContext(codegen) {}

    ModuleEmmiter::~ModuleEmmiter() {
        for (auto& pair : functions) {
            if (pair.second) delete pair.second;
        }
    }

    llvm::Value* ModuleEmmiter::getAtomicGCType() {
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

    llvm::Value* ModuleEmmiter::llvmTy2GCType(llvm::Type* ty) {
        if (!ty) {
            return getAtomicGCType();
        }

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

        if (ty->isPointerTy()) {
            uint32_t ptrSize = static_cast<uint32_t>(content->getDataLayout().getPointerSize());
            return getArrayGCType(true, ptrSize, getAtomicGCType());
        }

        return getAtomicGCType();
    }

    FunctionEmiiter* ModuleEmmiter::lookup(const fzlib::String& name) const {
        const auto* function = backend.lookup(name);
        auto found = functions.find(function);
        if (found != functions.end()) {
            return found->second;
        }
        throw std::runtime_error(fzlib::String("Try to call an unknown function: \"" + name + "\"").c_str());
    }

    void ModuleEmmiter::impl() {
        content = new llvm::Module(ID.c_str(), *codegenContext.context);

        for (const auto& function : backend.functions) {
            functions.emplace(function.get(), new FunctionEmiiter(
                *function, function->linkage == IR::Linkage::External, this, codegenContext));
        }
        for (const auto* function : backend.imports) {
            if (!functions.contains(function)) {
                functions.emplace(function, new FunctionEmiiter(*function, true, this, codegenContext));
            }
        }
        for (const auto& [source, function] : functions) {
            function->impl();
        }
    }

    void ModuleEmmiter::codegen() {
        for (const auto& source : backend.functions) {
            FunctionEmiiter* curFn = functions.at(source.get());

            if (!curFn->external)
                curFn->codegen();

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
