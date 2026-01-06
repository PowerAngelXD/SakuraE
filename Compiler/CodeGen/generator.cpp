#include "generator.hpp"
#include <iostream>

namespace sakoraE::CodeGen {

    llvm::Value* CodeGenerator::generate(NodePtr node) {
        if (!node) return nullptr;
        switch (node->getTag()) {
            case ASTTag::LiteralNode: return genLiteralNode(node);
            case ASTTag::IndexOpNode: return genIndexOpNode(node);
            case ASTTag::CallingOpNode: return genCallingOpNode(node);
            case ASTTag::AtomIdentifierNode: return genAtomIdentifierNode(node);
            case ASTTag::IdentifierExprNode: return genIdentifierExprNode(node);
            case ASTTag::PrimExprNode: return genPrimExprNode(node);
            case ASTTag::MulExprNode: return genMulExprNode(node);
            case ASTTag::AddExprNode: return genAddExprNode(node);
            case ASTTag::LogicExprNode: return genLogicExprNode(node);
            case ASTTag::BinaryExprNode: return genBinaryExprNode(node);
            case ASTTag::ArrayExprNode: return genArrayExprNode(node);
            case ASTTag::WholeExprNode: return genWholeExprNode(node);
            case ASTTag::AssignExprNode: return genAssignExprNode(node);
            case ASTTag::RangeExprNode: return genRangeExprNode(node);

            case ASTTag::DeclareStmtNode: return genDeclareStmtNode(node);
            case ASTTag::ExprStmtNode: return genExprStmtNode(node);
            case ASTTag::IfStmtNode: return genIfStmtNode(node);
            case ASTTag::ElseStmtNode: return genElseStmtNode(node);
            case ASTTag::WhileStmtNode: return genWhileStmtNode(node);
            case ASTTag::ForStmtNode: return genForStmtNode(node);
            case ASTTag::BlockStmtNode: return genBlockStmtNode(node);
            case ASTTag::FuncDefineStmtNode: return genFuncDefineStmtNode(node);
            case ASTTag::ReturnStmtNode: return genReturnStmtNode(node);
            
            default: return nullptr;
        }
    }

    llvm::Value* CodeGenerator::genLiteralNode(NodePtr node) {
        // TODO: Implement Literal generation (Int, Float, String, etc.)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIndexOpNode(NodePtr node) {
        // TODO: Implement Array Indexing
        return nullptr;
    }

    llvm::Value* CodeGenerator::genCallingOpNode(NodePtr node) {
        // TODO: Implement Function Call
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAtomIdentifierNode(NodePtr node) {
        // TODO: Implement Atom Identifier resolution
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIdentifierExprNode(NodePtr node) {
        // TODO: Implement Identifier Expression
        return nullptr;
    }

    llvm::Value* CodeGenerator::genPrimExprNode(NodePtr node) {
        // TODO: Implement Primary Expression
        return nullptr;
    }

    llvm::Value* CodeGenerator::genMulExprNode(NodePtr node) {
        // TODO: Implement Multiplication/Division
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAddExprNode(NodePtr node) {
        // TODO: Implement Addition/Subtraction
        return nullptr;
    }

    llvm::Value* CodeGenerator::genLogicExprNode(NodePtr node) {
        // TODO: Implement Logic Operations (AND, OR)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBinaryExprNode(NodePtr node) {
        // TODO: Implement Binary Expressions
        return nullptr;
    }

    llvm::Value* CodeGenerator::genArrayExprNode(NodePtr node) {
        // TODO: Implement Array Literals or Expressions
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWholeExprNode(NodePtr node) {
        // TODO: Implement Whole Expression wrapper
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAssignExprNode(NodePtr node) {
        // TODO: Implement Assignment
        return nullptr;
    }

    llvm::Value* CodeGenerator::genRangeExprNode(NodePtr node) {
        // TODO: Implement Range logic
        return nullptr;
    }

    // Statements
    llvm::Value* CodeGenerator::genDeclareStmtNode(NodePtr node) {
        // TODO: Implement Variable Declaration (Alloca)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genExprStmtNode(NodePtr node) {
        // TODO: Implement Expression Statement
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIfStmtNode(NodePtr node) {
        // TODO: Implement If Statement (Control Flow)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genElseStmtNode(NodePtr node) {
        // TODO: Implement Else block
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWhileStmtNode(NodePtr node) {
        // TODO: Implement While Loop
        return nullptr;
    }

    llvm::Value* CodeGenerator::genForStmtNode(NodePtr node) {
        // TODO: Implement For Loop
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBlockStmtNode(NodePtr node) {
        // TODO: Implement Block Scope
        return nullptr;
    }

    llvm::Value* CodeGenerator::genFuncDefineStmtNode(NodePtr node) {
        // TODO: Implement Function Definition
        return nullptr;
    }

    llvm::Value* CodeGenerator::genReturnStmtNode(NodePtr node) {
        // TODO: Implement Return Statement
        return nullptr;
    }

}
