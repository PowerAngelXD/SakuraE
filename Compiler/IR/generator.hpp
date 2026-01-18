#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "struct/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/value/constant.hpp"

namespace sakuraE::IR {
    class IRGenerator {
        Program program;

        Scope* currentScope = nullptr;

        IRValue* declareSymbol(fzlib::String name, IRType* t, IRValue* initVal = nullptr) {
            IRValue* addr = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::declare, IRType::getVoidTy(), {Constant::get(t), initVal}, "declare." + name);
            
            curFunc()->fnScope().declare(name, addr, t);

            return addr;
        }

        IRValue* loadSymbol(fzlib::String name, PositionInfo info = {0, 0, "Normal Load"}) {
            Symbol* symbol = curFunc()->fnScope().lookup(name);

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
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node);
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
        IRValue* visitBlockStmtNode(NodePtr node, fzlib::String blockName);
        IRValue* visitFuncDefineStmtNode(NodePtr node);
        IRValue* visitReturnStmtNode(NodePtr node);
        IRValue* visitStmt(NodePtr node);
    };
}

#endif // !SAKURAE_GENERATOR_HPP
