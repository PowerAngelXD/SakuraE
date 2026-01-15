#ifndef SAKORA_IR_HPP
#define SAKORA_IR_HPP

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>

#include "Compiler/Frontend/AST.hpp"
#include "value/value.hpp"

namespace sakuraE::IR {
    class IRManager {
    public:
        llvm::LLVMContext* context;
        llvm::Module* mainModule;
        llvm::IRBuilder<>* builder;

        IRManager() {
            context = new llvm::LLVMContext();
            mainModule = new llvm::Module("main_module", *context);
            builder = new llvm::IRBuilder<>(*context);
        }
    };
}

#endif // !SAKORA_IR_HPP