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
        if (!node->isLeaf()) 
            throw SakoraError(OccurredTerm::IR_GENERATING, 
                            "This Node is not a leaf-node",
                            node->getToken().info);
        
        auto token = node->getToken();
        switch (token.type) {
            case TokenType::INT_N:
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), atoi(token.content.c_str()));
            case TokenType::FLOAT_N:
                return llvm::ConstantFP::get(llvm::Type::getFloatTy(*context), atof(token.content.c_str()));
            case TokenType::BOOL_CONST:
                return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), (token.content == "true"?true:false));
            case TokenType::STRING:
                return llvm::ConstantDataArray::getString(*context, token.content.c_str());
            case TokenType::CHAR:
                return llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), token.content.at(0));
            default:
                throw SakoraError(OccurredTerm::IR_GENERATING, 
                            "Unknown literal",
                            node->getToken().info);
        }
    }

    llvm::Value* CodeGenerator::genIndexOpNode(NodePtr node) {
        return genAddExprNode((*node)[ASTTag::HeadExpr]);
    }

    llvm::Value* CodeGenerator::genCallingOpNode(NodePtr node) {
        if (!node->hasNode(ASTTag::Exprs)) return nullptr;

        std::vector<llvm::Value*> args;
        for (auto expr: node->getChildren()) {
            args.push_back(genWholeExprNode(expr));
        }

        return nullptr;
    }

    llvm::Value* CodeGenerator::genAtomIdentifierNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIdentifierExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genPrimExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genMulExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAddExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genLogicExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBinaryExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genArrayExprNode(NodePtr node) { 
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWholeExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAssignExprNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genRangeExprNode(NodePtr node) { 
        return nullptr; 
    }

    llvm::Value* CodeGenerator::genDeclareStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genExprStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIfStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genElseStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWhileStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genForStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBlockStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genFuncDefineStmtNode(NodePtr node) {
        return nullptr;
    }

    llvm::Value* CodeGenerator::genReturnStmtNode(NodePtr node) {
        return nullptr;
    }

}
