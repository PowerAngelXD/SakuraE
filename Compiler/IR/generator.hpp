#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/IR/value/value.hpp"
#include "includes/String.hpp"
#include "includes/magic_enum.hpp"
#include "struct/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/value/constant.hpp"
#include "Compiler/Frontend/lexer.h"



namespace sakuraE::IR {
    class IRGenerator {
        Program program;

        Symbol<IRValue*>* lookup(fzlib::String n, PositionInfo info) {
            auto result = curFunc()->fnScope().lookup(n);
            if (!result) {
                result = curModule()->lookup(n);
            }

            if (!result) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                    "Unknown identifier: " + n,
                    info);
            }
            return result;
        }
        
        IRValue* createLoad(IRValue* addr, PositionInfo info) {
            if (auto inst = dynamic_cast<Instruction*>(addr)) {
                if (!inst->isLValue()) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                        "An L-value is required as the left operand of an assignment.",
                        info);
                }
                
                return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::load,
                        addr->getType()->unboxComplex(),
                        {addr},
                        "load" + addr->getName());
            }
            throw SakuraError(OccurredTerm::IR_GENERATING,
                "An L-value is required as the left operand of an assignment.",
                info);
        }

        IRValue* createStore(IRValue* addr, IRValue* value, PositionInfo info) {
            if (auto inst = dynamic_cast<Instruction*>(addr)) {
                if (!inst->isLValue()) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "An L-value is required as the left operand of an assignment.",
                                    info);
                }

                auto pureAddrType = addr->getType()->unboxComplex();

                if (!pureAddrType->isEqual(value->getType())) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Cannot assign a value of a different type from the original.",
                            info);
                }

                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::store,
                                                pureAddrType,
                                                {addr, value},
                                                "store." + addr->getName());
            }
            else {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "An L-value is required as the left operand of an assignment.",
                                    info);
            }
        }

        IRValue* createParam(fzlib::String name, IRType* ty, PositionInfo info) {
            IRType* finalType = ty;

            if (ty->isComplexType()) {
                finalType = IRType::getPointerTo(ty);
            }

            auto param = curFunc()
                            ->curBlock()
                            ->createInstruction(
                                OpKind::param,
                                finalType,
                                {},
                                "param." + name
                            );
            curFunc()->fnScope().declare(name, param, finalType);

            return param;
        }

        IRValue* createAlloca(fzlib::String n, IRType* ty, IRValue* initVal, PositionInfo info) {
            if (initVal && !ty->isEqual(initVal->getType())) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot declare a variable with a type that differs from the assigned value's type.",
                                info);
            }

            IRType* finalType = ty;

            if (ty->isComplexType()) {
                finalType = IRType::getPointerTo(ty);
            }

            auto addr =  curFunc()
                ->curBlock()
                ->createInstruction(OpKind::create_alloca,
                                    finalType,
                                    {initVal?initVal:(finalType->isComplexType()?nullptr:Constant::getDefault(finalType, info))},
                                    "create_alloca." + n);

            curFunc()->fnScope().declare(n, addr, finalType);

            return addr;
        }

        // Used to obtain the type of the result from a non-logical binary operation
        IRType* handleUnlogicalBinaryCalc(IRValue* lhs, IRValue* rhs, PositionInfo info = {0, 0, "Normal Calc"}) {
            switch (lhs->getType()->getIRTypeID()) {
                case IRTypeID::Integer32TyID: {
                    auto lhsType = dynamic_cast<IRIntegerType*>(lhs->getType());
                    switch (rhs->getType()->getIRTypeID())
                    {
                        case IRTypeID::Integer32TyID: {
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
                        case IRTypeID::Integer32TyID: {
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

        Function* curFunc() {
            return program.curMod()->curFunc();
        }

        Module* curModule() {
            return program.curMod();
        }
    public:
        IRGenerator(fzlib::String name): program(name) {
            program.buildModule(name, {1, 1, "Start of the whole program"});
        }

        Program& getProgram() {
            return program;
        }

        fzlib::String toFormatString() {
            fzlib::String raw = program.toString();
            fzlib::String result;
            int indent = 0;
            
            for (std::size_t i = 0; i < raw.len(); i++) {
                char c = raw[i];
                if (c == '{') {
                    result += " {\n";
                    indent++;
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                } else if (c == '}') {
                    result += "\n";
                    indent--;
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                    result += "}";
                } else if (c == ';') {
                    result += ";\n";
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                } else if (c == ':') {
                    if (result.len() > 0 && result[result.len() - 1] != ' ') {
                        result += "\n";
                        for (int j = 0; j < indent * 2; j++) result += ' ';
                    }
                    result += c;
                } else {
                    result += c;
                }
            }
            return result;
        }

        // --- Visit Expressions ---
        IRValue* visitLiteralNode(NodePtr node);
        IRValue* visitIndexOpNode(IRValue* addr, fzlib::String target, NodePtr node);
        IRValue* visitCallingOpNode(IRValue* addr, fzlib::String target, NodePtr node);
        IRValue* visitAtomIdentifierNode(NodePtr node);
        IRValue* visitIdentifierExprNode(NodePtr node);
        IRValue* visitPrimExprNode(NodePtr node);
        IRValue* visitMulExprNode(NodePtr node);
        IRValue* visitAddExprNode(NodePtr node);
        IRValue* visitLogicExprNode(NodePtr node);
        IRValue* visitBinaryExprNode(NodePtr node);
        IRValue* visitArrayExprNode(NodePtr node);
        IRValue* visitWholeExprNode(NodePtr node);
        IRValue* visitBasicTypeModifierNode(NodePtr node);
        IRValue* visitArrayTypeModifierNode(NodePtr node);
        IRValue* visitTypeModifierNode(NodePtr node);
        IRValue* visitAssignExprNode(NodePtr node);
        // TODO: Range implement
        // IRValue* visitRangeExprNode(NodePtr node);

        // --- Visit Statements ---
        IRValue* visitDeclareStmtNode(NodePtr node);
        IRValue* visitExprStmtNode(NodePtr node);
        IRValue* visitIfStmtNode(NodePtr node);
        IRValue* visitWhileStmtNode(NodePtr node);
        IRValue* visitForStmtNode(NodePtr node);
        IRValue* visitBlockStmtNode(NodePtr node, fzlib::String blockName, long beforeBlock = -1);
        IRValue* visitFuncDefineStmtNode(NodePtr node);
        IRValue* visitReturnStmtNode(NodePtr node);
        IRValue* visitBreakStmtNode(NodePtr node);
        IRValue* visitContinueStmtNode(NodePtr node);
        IRValue* visitStmt(NodePtr node);
    };
}

#endif // !SAKURAE_GENERATOR_HPP
