#ifndef SAKORAE_GENERATOR_HPP
#define SAKORAE_GENERATOR_HPP

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <memory>
#include <vector>
#include <iostream>

#include "../Frontend/AST.hpp"
#include "../IR/symbol.hpp"

namespace sakoraE::CodeGen {
    using namespace sakoraE::IR;

    class CodeGenerator {
    private:
        llvm::LLVMContext* context;
        llvm::Module* module;
        llvm::IRBuilder<>* builder;
        SymbolManager symManager;

    public:
        CodeGenerator(std::string name) {
            context = new llvm::LLVMContext();
            module = new llvm::Module(name, *context);
            builder = new llvm::IRBuilder<>(*context);
            symManager.NewMap();
        }

        llvm::Module* getModule() { return module; }
        llvm::LLVMContext* getContext() { return context; }
        llvm::IRBuilder<>* getBuilder() { return builder; }

        llvm::Value* generate(NodePtr node);

    private:
        // Tools
        

        // Expressions
        llvm::Value* genLiteralNode(NodePtr node);
        llvm::Value* genIndexOpNode(NodePtr node);
        llvm::Value* genCallingOpNode(NodePtr node);
        llvm::Value* genAtomIdentifierNode(NodePtr node);
        llvm::Value* genIdentifierExprNode(NodePtr node);
        llvm::Value* genPrimExprNode(NodePtr node);
        llvm::Value* genMulExprNode(NodePtr node);
        llvm::Value* genAddExprNode(NodePtr node);
        llvm::Value* genLogicExprNode(NodePtr node);
        llvm::Value* genBinaryExprNode(NodePtr node);
        llvm::Value* genArrayExprNode(NodePtr node);
        llvm::Value* genWholeExprNode(NodePtr node);
        llvm::Value* genAssignExprNode(NodePtr node);
        llvm::Value* genRangeExprNode(NodePtr node);

        // Statements
        llvm::Value* genDeclareStmtNode(NodePtr node);
        llvm::Value* genExprStmtNode(NodePtr node);
        llvm::Value* genIfStmtNode(NodePtr node);
        llvm::Value* genElseStmtNode(NodePtr node);
        llvm::Value* genWhileStmtNode(NodePtr node);
        llvm::Value* genForStmtNode(NodePtr node);
        llvm::Value* genBlockStmtNode(NodePtr node);
        llvm::Value* genFuncDefineStmtNode(NodePtr node);
        llvm::Value* genReturnStmtNode(NodePtr node);

        llvm::Value* logError(const char* str) {
            std::cerr << "CodeGen Error: " << str << std::endl;
            return nullptr;
        }
    };
}

#endif // ! SAKORAE_GENERATOR_HPP
