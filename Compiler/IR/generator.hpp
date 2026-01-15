#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "struct/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/value/constant.hpp"

namespace sakuraE::IR {
    class IRGenerator {
        Program program;

        Scope* currentScope = nullptr;

        // Calling List
        std::vector<std::vector<Value*>> callingList;
        int callingCur = -1;

        int makeCallingList(std::vector<Value*> list) {
            callingList.push_back(list);
            callingCur ++;

            return callingCur;
        }

        void declareSymbol(fzlib::String name, Type* t, Value* initVal = nullptr) {
            Value* addr = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::declare, Type::getVoidTy(), {}, "declare-" + name);
            
            curFunc()->fnScope().declare(name, addr, t);

            if (initVal) {
                curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::assign, Type::getVoidTy(), {addr, initVal}, "assign-" + name);
            }
        }

        Value* loadSymbol(fzlib::String name, PositionInfo info = {0, 0, "Normal Load"}) {
            Symbol* symbol = curFunc()->fnScope().lookup(name);

            if (symbol == nullptr)
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "",
                                info);
            
            return curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::load, symbol->getType(), {symbol->address}, "load-" + name);
        }

        // Used to obtain the type of the result from a non-logical binary operation
        Type* handleUnlogicalBinaryCalc(Value* lhs, Value* rhs, PositionInfo info = {0, 0, "Normal Calc"}) {
            switch (lhs->getType()->getTypeID()) {
                case TypeID::IntegerTyID: {
                    auto lhsType = dynamic_cast<IntegerType*>(lhs->getType());
                    switch (rhs->getType()->getTypeID())
                    {
                        case TypeID::IntegerTyID: {
                            auto rhsType = dynamic_cast<IntegerType*>(rhs->getType());
                            return Type::getIntNTy(std::max(lhsType->getBitWidth(), rhsType->getBitWidth()));
                        }
                        case TypeID::FloatTyID: {
                            return Type::getFloatTy();
                        }
                        default:
                            throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Used a type that does not support '+' '-' '*' '%' and '/' operations",
                                    info);
                    }
                    break;
                }
                case TypeID::FloatTyID: {
                    switch (rhs->getType()->getTypeID())
                    {
                        case TypeID::IntegerTyID: {
                            return Type::getFloatTy();
                        }
                        case TypeID::FloatTyID: {
                            return Type::getFloatTy();
                        }
                        default:
                            throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Used a type that does not support '+' '-' '*' '%' and '/' operations",
                                    info);
                    }
                    break;
                }
                default:
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Used a type that does not support '+' '-' '*' '%' and '/' operations",
                                    info);
            }
        }

    public:
        IRGenerator(fzlib::String name): program(name) {
            program.buildModule(name, {1, 1, "Start of the whole program"});
        }

        Program getProgram() {
            return program;
        }

        // Main entry point for generating IR from AST
        void generate(NodePtr node);

    private:
        Function* curFunc() {
            return program.curMod()->curFunc();
        }

        // --- Visit Expressions ---
        Value* visitLiteralNode(NodePtr node);
        Value* visitIndexOpNode(Value* addr, NodePtr node);
        Value* visitCallingOpNode(Value* addr, NodePtr node);
        Value* visitAtomIdentifierNode(NodePtr node);
        Value* visitIdentifierExprNode(NodePtr node);
        Value* visitPrimExprNode(NodePtr node);
        Value* visitMulExprNode(NodePtr node);
        Value* visitAddExprNode(NodePtr node);
        Value* visitLogicExprNode(NodePtr node);
        Value* visitBinaryExprNode(NodePtr node);
        Value* visitArrayExprNode(NodePtr node);
        Value* visitWholeExprNode(NodePtr node);
        Value* visitBasicTypeModifierNode(NodePtr node);
        Value* visitArrayTypeModifierNode(NodePtr node);
        Value* visitTypeModifierNode(NodePtr node);
        Value* visitAssignExprNode(NodePtr node);
        Value* visitRangeExprNode(NodePtr node);

        // --- Visit Statements ---
        Value* visitDeclareStmtNode(NodePtr node);
        Value* visitExprStmtNode(NodePtr node);
        Value* visitIfStmtNode(NodePtr node);
        Value* visitElseStmtNode(NodePtr node);
        Value* visitWhileStmtNode(NodePtr node);
        Value* visitForStmtNode(NodePtr node);
        Value* visitBlockStmtNode(NodePtr node);
        Value* visitFuncDefineStmtNode(NodePtr node);
        Value* visitReturnStmtNode(NodePtr node);
        Value* visitStmt(NodePtr node);
    };
}

#endif // !SAKURAE_GENERATOR_HPP
