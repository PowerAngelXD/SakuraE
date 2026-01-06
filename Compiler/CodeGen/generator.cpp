// Disable warnings from LLVM headers that we can't control
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "generator.hpp"
#include <iostream>

// Restore warnings
#pragma GCC diagnostic pop

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
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Literal generation (Int, Float, String, etc.)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIndexOpNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Array Indexing
        return nullptr;
    }

    llvm::Value* CodeGenerator::genCallingOpNode(NodePtr node) {
        // Extract the function name from the Identifier node
        if (!node->hasNode(ASTTag::Identifier)) {
            return logError("Function call without identifier");
        }
        
        NodePtr identifierNode = (*node)[ASTTag::Identifier];
        if (!identifierNode || !identifierNode->isLeaf()) {
            return logError("Invalid function identifier");
        }
        
        std::string funcName(identifierNode->getToken().content.c_str());
        
        // Look up the function in the module
        llvm::Function* calleeFunc = module->getFunction(funcName);
        if (!calleeFunc) {
            return logError(("Unknown function referenced: " + funcName).c_str());
        }
        
        // Generate code for each argument
        std::vector<llvm::Value*> args;
        if (node->hasNode(ASTTag::Exprs)) {
            NodePtr exprsNode = (*node)[ASTTag::Exprs];
            for (const auto& child : exprsNode->getChildren()) {
                llvm::Value* argVal = generate(child.second);
                if (!argVal) {
                    return logError("Failed to generate argument expression");
                }
                args.push_back(argVal);
            }
        }
        
        // Verify argument count matches function signature
        if (calleeFunc->arg_size() != args.size()) {
            return logError(("Incorrect number of arguments for function: " + funcName).c_str());
        }
        
        // Create the call instruction
        return builder->CreateCall(calleeFunc, args, "calltmp");
    }

    llvm::Value* CodeGenerator::genAtomIdentifierNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Atom Identifier resolution
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIdentifierExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Identifier Expression
        return nullptr;
    }

    llvm::Value* CodeGenerator::genPrimExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Primary Expression
        return nullptr;
    }

    llvm::Value* CodeGenerator::genMulExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Multiplication/Division
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAddExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Addition/Subtraction
        return nullptr;
    }

    llvm::Value* CodeGenerator::genLogicExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Logic Operations (AND, OR)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBinaryExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Binary Expressions
        return nullptr;
    }

    llvm::Value* CodeGenerator::genArrayExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Array Literals or Expressions
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWholeExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Whole Expression wrapper
        return nullptr;
    }

    llvm::Value* CodeGenerator::genAssignExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Assignment
        return nullptr;
    }

    llvm::Value* CodeGenerator::genRangeExprNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Range logic
        return nullptr;
    }

    // Statements
    llvm::Value* CodeGenerator::genDeclareStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Variable Declaration (Alloca)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genExprStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Expression Statement
        return nullptr;
    }

    llvm::Value* CodeGenerator::genIfStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement If Statement (Control Flow)
        return nullptr;
    }

    llvm::Value* CodeGenerator::genElseStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Else block
        return nullptr;
    }

    llvm::Value* CodeGenerator::genWhileStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement While Loop
        return nullptr;
    }

    llvm::Value* CodeGenerator::genForStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement For Loop
        return nullptr;
    }

    llvm::Value* CodeGenerator::genBlockStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Block Scope
        return nullptr;
    }

    llvm::Value* CodeGenerator::genFuncDefineStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Function Definition
        return nullptr;
    }

    llvm::Value* CodeGenerator::genReturnStmtNode(NodePtr node) {
        (void)node; // Unused parameter - TODO implementation
        // TODO: Implement Return Statement
        return nullptr;
    }

}
