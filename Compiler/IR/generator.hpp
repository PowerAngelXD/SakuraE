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

        Value* declareSymbol(fzlib::String name, IRType* t, Value* initVal = nullptr) {
            Value* addr = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::declare, IRType::getVoidTy(), {Constant::get(t), initVal}, "declare-" + name);
            
            curFunc()->fnScope().declare(name, addr, t);

            return addr;
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
        IRType* handleUnlogicalBinaryCalc(Value* lhs, Value* rhs, PositionInfo info = {0, 0, "Normal Calc"}) {
            switch (lhs->getType()->getIRTypeID()) {
                case IRTypeID::IntegerTyID: {
                    auto lhsType = dynamic_cast<IRIntegerType*>(lhs->getType());
                    switch (rhs->getType()->getIRTypeID())
                    {
                        case IRTypeID::IntegerTyID: {
                            auto rhsType = dynamic_cast<IRIntegerType*>(rhs->getType());
                            return IRType::getIntNTy(std::max(lhsType->getBitWidth(), rhsType->getBitWidth()));
                        }
                        case IRTypeID::FloatTyID: {
                            return IRType::getFloatTy();
                        }
                        default:
                            throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Used a type that does not support '+' '-' '*' '%' and '/' operations",
                                    info);
                    }
                    break;
                }
                case IRTypeID::FloatTyID: {
                    switch (rhs->getType()->getIRTypeID())
                    {
                        case IRTypeID::IntegerTyID: {
                            return IRType::getFloatTy();
                        }
                        case IRTypeID::FloatTyID: {
                            return IRType::getFloatTy();
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
        // TODO: Range implement
        // Value* visitRangeExprNode(NodePtr node);

        // --- Visit Statements ---
        Value* visitDeclareStmtNode(NodePtr node);
        Value* visitExprStmtNode(NodePtr node);
        Value* visitIfStmtNode(NodePtr node);
        Value* visitWhileStmtNode(NodePtr node);
        Value* visitForStmtNode(NodePtr node);
        Value* visitBlockStmtNode(NodePtr node, fzlib::String blockName);
        Value* visitFuncDefineStmtNode(NodePtr node);
        Value* visitReturnStmtNode(NodePtr node);
        Value* visitStmt(NodePtr node);
    };
}

#endif // !SAKURAE_GENERATOR_HPP
