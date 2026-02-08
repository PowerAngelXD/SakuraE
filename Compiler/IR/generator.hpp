#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/struct/instruction.hpp"
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

        IRValue* declareParam(fzlib::String name, IRType* t, PositionInfo info) {
            auto param = curFunc()
                            ->curBlock()
                            ->createInstruction(
                                OpKind::param,
                                t,
                                {},
                                "param." + name
                            );
            curFunc()->fnScope().declare(name, param, t);

            return param;
        }

        IRValue* declareSymbol(fzlib::String name, IRValue* t, IRValue* initVal, PositionInfo info) {
            auto constInst = dynamic_cast<Instruction*>(t);
            auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
            TypeInfo* typeInfo = typeInfoConstant->getContentValue<TypeInfo*>();

            auto dType = magic_enum::enum_name(typeInfo->toIRType()->getIRTypeID());

            auto details = "Cannot declare a value to an identifier of a different value type, expect declare: "
                                    + fzlib::String(dType)
                                    + ", real: "
                                    + (initVal?fzlib::String(magic_enum::enum_name(initVal->getType()->getIRTypeID())):"null_type");
            if (initVal && initVal->getType()->getIRTypeID() != typeInfo->toIRType()->getIRTypeID()) 
                throw SakuraError(OccurredTerm::IR_GENERATING,
                            details,
                            info);

            IRValue* addr = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::declare, typeInfo->toIRType(), {initVal}, "declare." + name);

            curFunc()->fnScope().declare(name, addr, typeInfo->toIRType());

            return addr;
        }

        IRValue* declareSymbol(fzlib::String name, IRType* t, IRValue* initVal, PositionInfo info) {
            IRValue* addr = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::declare, t, {initVal}, "declare." + name);
            
            auto dType = magic_enum::enum_name(t->getIRTypeID());

            auto details = "Cannot declare a value to an identifier of a different value type, expect declare: "
                                    + fzlib::String(dType)
                                    + ", real: "
                                    + (initVal?fzlib::String(magic_enum::enum_name(initVal->getType()->getIRTypeID())):"null_type");
            if (initVal && initVal->getType()->getIRTypeID() != t->getIRTypeID()) 
                throw SakuraError(OccurredTerm::IR_GENERATING,
                            details,
                            info);

            curFunc()->fnScope().declare(name, addr, t);

            return addr;
        }

        IRValue* storeSymbol(IRValue* addr, IRValue* value, PositionInfo info) {
            if (addr->getType()->getIRTypeID() != value->getType()->getIRTypeID())
                throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Cannot assign a value to an identifier of a different value type",
                            info);
            
            return curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::assign, addr->getType(), {addr, value}, "assign." + addr->getName());
        }

        IRValue* loadSymbol(fzlib::String name, PositionInfo info = {0, 0, "Normal Load"}) {
            Symbol<IRValue*>* symbol = curFunc()->fnScope().lookup(name);

            if (symbol == nullptr)
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot find symbol: " + name,
                                info);
            
            return curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::load, symbol->getType(), {symbol->address}, "load." + name);
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

        Program getProgram() {
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
        IRValue* visitIndexOpNode(IRValue* addr, NodePtr node);
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
        IRValue* visitStmt(NodePtr node);
    };
}

#endif // !SAKURAE_GENERATOR_HPP
