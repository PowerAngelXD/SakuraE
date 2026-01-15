#include "generator.hpp"

namespace sakuraE::IR {
    Value* IRGenerator::visitLiteralNode(NodePtr node) {
        auto literal = Constant::getFromToken((*node)[ASTTag::Literal]->getToken());

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::constant, literal->getType(), {literal}, "constant-pushing");
    }

    Value* IRGenerator::visitIndexOpNode(Value* addr, NodePtr node) {
        Value* indexResult = visitAddExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::indexing, indexResult->getType(), {addr, indexResult}, "indexing-operator");
    }

    Value* IRGenerator::visitCallingOpNode(Value* addr, NodePtr node) {
        std::vector<Value*> args;

        for (auto argExpr: (*node)[ASTTag::Exprs]->getChildren()) {
            args.push_back(visitWholeExprNode(argExpr));
        }

        int index = makeCallingList(args);

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::call, Type::getInt32Ty(), {addr, Constant::get(index)}, "make-calling-list");
    }

    Value* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        Value* result = loadSymbol((*node)[ASTTag::Identifier]->getToken().content);

        if (node->hasNode(ASTTag::Ops)) {
            for (auto op: (*node)[ASTTag::Ops]->getChildren()) {
                switch (op->getTag()) {
                    case ASTTag::IndexOpNode: {
                        result = visitIndexOpNode(result, op);
                        break;
                    }
                    case ASTTag::CallingOpNode: {
                        result = visitCallingOpNode(result, op);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return result;
    }

    Value* IRGenerator::visitIdentifierExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        Value* result = visitAtomIdentifierNode(chain[0]);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            fzlib::String memberName = (*chain[i])[ASTTag::Identifier]->getToken().content;
            result = curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::gmem, result->getType(), {result, Constant::get(memberName)}, "gmem");
            
            if (chain[i]->hasNode(ASTTag::Ops)) {
                for (auto op: (*chain[i])[ASTTag::Ops]->getChildren()) {
                    switch (op->getTag()) {
                        case ASTTag::IndexOpNode: {
                            result = visitIndexOpNode(result, op);
                            break;
                        }
                        case ASTTag::CallingOpNode: {
                            result = visitCallingOpNode(result, op);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

        return result;
    }

    Value* IRGenerator::visitPrimExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::Literal)) {
            return visitLiteralNode((*node)[ASTTag::Literal]);
        }
        else if (node->hasNode(ASTTag::Identifier)) {
            return visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        }
        else {
            return visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        }
    }

    Value* IRGenerator::visitMulExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        Value* lhs = visitPrimExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                Value* rhs = visitPrimExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::MUL: {
                        lhs = curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::mul, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mul-tmp");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::div, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "div-tmp");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::mod, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mod-tmp");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    Value* IRGenerator::visitAddExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        Value* lhs = visitMulExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                Value* rhs = visitMulExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::ADD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::add, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "add-tmp");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::sub, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "sub-tmp");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    Value* IRGenerator::visitLogicExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        Value* lhs = visitAddExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                Value* rhs = visitAddExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_LS_THAN: {
                         lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_ls_than, Type::getBoolTy(), {lhs, rhs}, "lgc-ls-than-tmp");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_ls_than, Type::getBoolTy(), {lhs, rhs}, "lgc-eq-ls-than-tmp");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_mr_than, Type::getBoolTy(), {lhs, rhs}, "lgc-mr-than-tmp");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_mr_than, Type::getBoolTy(), {lhs, rhs}, "lgc-eq-mr-than-tmp");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_equal, Type::getBoolTy(), {lhs, rhs}, "lgc-equal-tmp");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_not_equal, Type::getBoolTy(), {lhs, rhs}, "lgc-not-equal-tmp");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    Value* IRGenerator::visitBinaryExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        Value* lhs = visitLogicExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                Value* rhs = visitLogicExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_AND: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_and, Type::getBoolTy(), {lhs, rhs}, "lgc-and-tmp");
                        break;
                    }
                    case TokenType::LGC_OR: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_or, Type::getBoolTy(), {lhs, rhs}, "lgc-or-tmp");
                        break;
                    }
                    default: 
                        break;
                }
            }
        }
        return lhs;
    }

    Value* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<Value*> array;
        
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        for (auto expr: chain) {
            array.push_back(visitWholeExprNode(expr));
        }

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::create_array,
                                        Type::getArrayTy(array[0]->getType(), array.size()),
                                        array,
                                        "create-array");
    }

    Value* IRGenerator::visitAssignExprNode(NodePtr node) {
        Value* symbol = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        Value* expr = visitWholeExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::assign,
                                        expr->getType(),
                                        {symbol, expr},
                                        "assign-" + symbol->getName());
    }

    Value* IRGenerator::visitWholeExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::AddExprNode)) {
            return visitAddExprNode((*node)[ASTTag::AddExprNode]);
        }
        else if (node->hasNode(ASTTag::BinaryExprNode)) {
            return visitBinaryExprNode((*node)[ASTTag::BinaryExprNode]);
        }
        else if (node->hasNode(ASTTag::ArrayExprNode)) {
            return visitArrayExprNode((*node)[ASTTag::ArrayExprNode]);
        }
        else {
            return visitAssignExprNode((*node)[ASTTag::AssignExprNode]);
        }
    }
}