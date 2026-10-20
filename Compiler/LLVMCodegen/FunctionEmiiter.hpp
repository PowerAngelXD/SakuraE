#ifndef SAKURAE_FUNCTIONEMIITER_HPP
#define SAKURAE_FUNCTIONEMIITER_HPP

#include <cstddef>
#include <cstdint>
#include <map>

#include "Compiler/IR/Semantic/model/scope.hpp"
#include "includes/String.hpp"

namespace llvm {
    class AllocaInst;
    class BasicBlock;
    class Function;
    class Type;
    class Value;
}

namespace sakuraE::IR {
    class BackendFunction;
    struct RuntimeTypeDescription;
}

namespace sakuraE::Codegen {
    class LLVMCodeGenerator;
    struct ModuleEmmiter;

    struct FunctionEmiiter {
        const IR::BackendFunction& backend;
        bool external;
        llvm::Function* content = nullptr;
        IR::Scope<llvm::Value*> scope;
        ModuleEmmiter* parent = nullptr;
        LLVMCodeGenerator& codegenContext;
        std::map<fzlib::String, llvm::AllocaInst*> paramAllocaMap;
        uint32_t gcScopeDepth = 0;
        llvm::BasicBlock* entryBlock = nullptr;

        FunctionEmiiter(const IR::BackendFunction& source,
                       bool isExternal,
                       ModuleEmmiter* p,
                       LLVMCodeGenerator& codegen);
        ~FunctionEmiiter();

        void gcEnterScope();
        void gcLeaveScope();
        void gcLeaveAllScopes();
        llvm::Value* gcAlloc(llvm::Value* size, llvm::Value* gcTy, llvm::Value* elemCount = nullptr);
        llvm::Value* gcAlloc(int size, llvm::Value* gcTy, uint64_t elemCount = 0);
        void gcRegisterRoot(llvm::Value* addr);
        void gcRegisterValueRoot(llvm::Value* addr);
        void gcRegisterManagedSlot(llvm::Value* addr, const IR::RuntimeTypeDescription& type);
        void gcPop(size_t times);
        void gcCollect();
        bool shouldTrackAsGCRoot(const IR::IRValue* value) const;
        llvm::AllocaInst* createRootedTemporary(
            llvm::Value* value,
            const fzlib::String& slotName,
            const IR::RuntimeTypeDescription& type);
        llvm::AllocaInst* createAlloca(llvm::Type* ty, llvm::Value* arraySize = nullptr, fzlib::String n = "");
        llvm::Value* createHeapAlloc(llvm::Type* t, llvm::Value* gcTy, llvm::Value* elemCount);
        llvm::Value* getParamAddress(fzlib::String n);

        // 创建 LLVM 函数，并将插入点重置到当前函数的 entry 基本块
        void impl();
        void codegen();
    };
}

#endif /* !SAKURAE_FUNCTIONEMIITER_HPP */
