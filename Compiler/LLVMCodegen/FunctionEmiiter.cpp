#include "FunctionEmiiter.hpp"

#include "LLVMCodegenerator.hpp"
#include "ModuleEmmiter.hpp"
#include "Compiler/IR/Backend/model/function.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <vector>

namespace sakuraE::Codegen {
    FunctionEmiiter::FunctionEmiiter(const IR::BackendFunction& source,
                                   bool isExternal,
                                   ModuleEmmiter* p,
                                   LLVMCodeGenerator& codegen):
        backend(source), external(isExternal),
        scope(source.source->getInfo()), parent(p), codegenContext(codegen) {}

    FunctionEmiiter::~FunctionEmiiter() = default;

    void FunctionEmiiter::gcEnterScope() {
        // 进入函数或临时表达式作用域，后续注册的 root 会按栈序撤销
        auto fn = parent->lookup("__gc_enter_scope");
        codegenContext.builder->CreateCall(fn->content, {});
        gcScopeDepth ++;
    }

    void FunctionEmiiter::gcLeaveScope() {
        if (gcScopeDepth == 0) return;
        auto fn = parent->lookup("__gc_leave_scope");
        codegenContext.builder->CreateCall(fn->content, {});
        gcScopeDepth --;
    }

    void FunctionEmiiter::gcLeaveAllScopes() {
        while (gcScopeDepth > 0) {
            gcLeaveScope();
        }
    }

    llvm::Value* FunctionEmiiter::gcAlloc(llvm::Value* size, llvm::Value* gcTy, llvm::Value* elemCount) {
        // 所有托管对象都通过统一入口分配，GC 可据此维护对象链表和类型信息
        auto fn = parent->lookup("__gc_alloc");
        if (!elemCount) {
            elemCount = codegenContext.builder->getInt64(0);
        }
        return codegenContext.builder->CreateCall(fn->content, {size, gcTy, elemCount});
    }

    llvm::Value* FunctionEmiiter::gcAlloc(int size, llvm::Value* gcTy, uint64_t elemCount) {
        auto fn = parent->lookup("__gc_alloc");
        auto sTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
        return codegenContext.builder->CreateCall(fn->content, {
            llvm::ConstantInt::get(sTy, size),
            gcTy,
            codegenContext.builder->getInt64(elemCount)
        });
    }

    void FunctionEmiiter::gcRegisterRoot(llvm::Value* addr) {
        // addr 指向 LLVM alloca 槽位，运行时会在收集时读取槽位中的最新对象指针
        auto fn = parent->lookup("__gc_register");
        auto ptr = codegenContext.builder->CreateBitCast(addr, llvm::PointerType::getUnqual(*codegenContext.context));
        codegenContext.builder->CreateCall(fn->content, {ptr});
    }

    void FunctionEmiiter::gcRegisterValueRoot(llvm::Value* addr) {
        auto fn = parent->lookup("__gc_register_value_slot");
        auto ptr = codegenContext.builder->CreateBitCast(addr,
            llvm::PointerType::getUnqual(*codegenContext.context));
        codegenContext.builder->CreateCall(fn->content, {ptr});
    }

    void FunctionEmiiter::gcRegisterManagedSlot(llvm::Value* addr, const IR::RuntimeTypeDescription& type) {
        if (type.representation == IR::ValueRepresentation::ManagedArray) {
            gcRegisterRoot(addr);
        }
        else {
            gcRegisterValueRoot(addr);
        }
    }

    void FunctionEmiiter::gcPop(size_t times) {
        if (times == 0) return;
        auto fn = parent->lookup("__gc_pop");
        codegenContext.builder->CreateCall(fn->content, {codegenContext.builder->getInt32(times)});
    }

    void FunctionEmiiter::gcCollect() {
        auto fn = parent->lookup("__gc_collect");
        codegenContext.builder->CreateCall(fn->content, {});
    }

    bool FunctionEmiiter::shouldTrackAsGCRoot(const IR::IRValue* value) const {
        return backend.tracksRoot(value);
    }

    llvm::AllocaInst* FunctionEmiiter::createRootedTemporary(
        llvm::Value* value,
        const fzlib::String& slotName,
        const IR::RuntimeTypeDescription& type) {
        // 将 SSA 值落到 entry block 的槽位，避免后续分配发生时值无法作为 root 被扫描
        auto* slot = createAlloca(value->getType(), nullptr, slotName);
        codegenContext.builder->CreateStore(value, slot);
        gcRegisterManagedSlot(slot, type);
        return slot;
    }

    llvm::AllocaInst* FunctionEmiiter::createAlloca(llvm::Type* ty, llvm::Value* arraySize, fzlib::String n) {
        llvm::BasicBlock* currentBlock = codegenContext.builder->GetInsertBlock();
        llvm::BasicBlock::iterator currentPoint = codegenContext.builder->GetInsertPoint();
        codegenContext.builder->SetInsertPoint(entryBlock, ++ entryBlock->getFirstInsertionPt());
        llvm::AllocaInst* alloca = codegenContext.builder->CreateAlloca(ty, arraySize, n.c_str());
        codegenContext.builder->SetInsertPoint(currentBlock, currentPoint);
        return alloca;
    }

    llvm::Value* FunctionEmiiter::createHeapAlloc(llvm::Type* t, llvm::Value* gcTy, llvm::Value* elemCount) {
        // 数组 payload 不包含 header；header 由 Runtime::__gc_alloc 在 payload 前方创建
        size_t size = parent->content->getDataLayout().getTypeAllocSize(t);
        llvm::Type* sizeTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
        llvm::Value* sizeVal = llvm::ConstantInt::get(sizeTy, size);
        return gcAlloc(sizeVal, gcTy, elemCount);
    }

    llvm::Value* FunctionEmiiter::getParamAddress(fzlib::String n) {
        if (paramAllocaMap.find(n) != paramAllocaMap.end()) {
            return paramAllocaMap[n];
        }
        return nullptr;
    }

    void FunctionEmiiter::impl() {
        std::vector<llvm::Type*> params;
        params.reserve(backend.parameters.size());
        for (std::size_t i = 0; i < backend.parameters.size(); ++i) {
            params.push_back(codegenContext.abiType(backend.parameters[i].second, backend.abi.parameters.at(i)));
        }

        auto* returnType = codegenContext.abiType(backend.returnType, backend.abi.result);
        auto* fnType = llvm::FunctionType::get(returnType, params, false);
        content = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
            backend.linkageName.c_str(), parent->content);
    }

    void FunctionEmiiter::codegen() {
        codegenContext.activeFunction = &backend;
        const auto* source = backend.source;
        const auto& irParams = backend.parameters;

        for (auto block: source->getBlocks()) {
            llvm::BasicBlock* llvmBlock = llvm::BasicBlock::Create(*codegenContext.context, block->getName().c_str(), content);
            codegenContext.bind(block, llvmBlock);

            if (block->getName() == "entry") {
                entryBlock = llvmBlock;
            }
        }

        codegenContext.builder->SetInsertPoint(entryBlock);
        // 函数级 root scope 覆盖参数、局部变量和所有临时 root
        gcEnterScope();

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            arg.setName(irParams[i].first.c_str());

            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            paramAllocaMap[irParams[i].first.c_str()] = argAlloca;

            codegenContext.builder->CreateStore(&arg, argAlloca);

            // 参数如果承载的是 GC 托管对象引用，需要在函数入口立即注册进 root stack
            if (backend.gcInfo.parameterRoots.at(i)) {
                gcRegisterManagedSlot(argAlloca, *backend.parameterRuntimeTypes.at(i));
            }

            scope.declare(irParams[i].first, argAlloca, nullptr);
            i ++;
        }
        for (auto irBlock: source->getBlocks()) {
            codegenContext.builder->SetInsertPoint(llvm::cast<llvm::BasicBlock>(codegenContext.toLLVMValue(irBlock, this)));

            for (auto inst: irBlock->getInstructions()) {
                codegenContext.instgen(inst, this);
            }
        }

        /* 允许 main 函数自然结束，源语言要求 main 返回 i32，而 LLVM ABI 返回装箱后的
         * RuntimeValue*，这对只用于输出结果的小型运行时示例很有用
         */
        if (backend.sourceName == "main") {
            for (auto& block: *content) {
                if (block.getTerminator()) {
                    continue;
                }
                codegenContext.builder->SetInsertPoint(&block);
                auto* boxedZero = codegenContext.boxRaw(
                    codegenContext.builder->getInt32(0),
                    *backend.returnRuntimeType, this);
                codegenContext.builder->CreateRet(boxedZero);
            }
        }
    }
}
