#ifndef SAKORA_IR_HPP
#define SAKORA_IR_HPP

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

namespace sakIR {
    using LLVMCtxPtr = std::unique_ptr<llvm::LLVMContext>;
    using LLVMModule = std::unique_ptr<llvm::Module>;
    using LLVMIRBuilder = std::unique_ptr<llvm::IRBuilder<>>;
    struct IRManager {
        LLVMCtxPtr context;
        LLVMModule mainModule;
        LLVMIRBuilder builder;

        IRManager() {
            context = std::make_unique<llvm::LLVMContext>();
            mainModule = std::make_unique<llvm::Module>("main_module", *context);
            builder = std::make_unique<llvm::IRBuilder<>>(*context);
        }
    };
}

#endif // !SAKORA_IR_HPP