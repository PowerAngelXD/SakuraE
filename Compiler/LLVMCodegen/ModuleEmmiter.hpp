#ifndef SAKURAE_MODULEEMMITER_HPP
#define SAKURAE_MODULEEMMITER_HPP

#include <cstdint>
#include <map>

namespace llvm {
    class Module;
    class Type;
    class Value;
}

namespace fzlib {
    class String;
}

namespace sakuraE::IR {
    class BackendFunction;
    class BackendModule;
}

namespace sakuraE::Codegen {
    class LLVMCodeGenerator;
    struct FunctionEmiiter;

    struct ModuleEmmiter {
        const IR::BackendModule& backend;
        const fzlib::String& ID;
        llvm::Module* content = nullptr;
        std::map<const IR::BackendFunction*, FunctionEmiiter*> functions;
        LLVMCodeGenerator& codegenContext;

        ModuleEmmiter(const IR::BackendModule& source, LLVMCodeGenerator& codegen);
        ~ModuleEmmiter();

        llvm::Value* getAtomicGCType();
        llvm::Value* getArrayGCType(bool isPtr, uint32_t memberSize, llvm::Value* memTy, uint64_t length = 0);
        llvm::Value* getRuntimeValueArrayGCType(uint32_t memberSize, uint64_t length);
        llvm::Value* llvmTy2GCType(llvm::Type* ty);
        FunctionEmiiter* lookup(const fzlib::String& name) const;

        void impl();
        void codegen();
    };
}

#endif /* !SAKURAE_MODULEEMMITER_HPP */
