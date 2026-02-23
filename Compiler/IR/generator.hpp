#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/struct/function.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/IR/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/String.hpp"
#include "includes/magic_enum.hpp"
#include "struct/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/value/constant.hpp"
#include "Compiler/Frontend/lexer.h"
#include <algorithm>



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
                        addr->getType(),
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

                if (!addr->getType()->isEqual(value->getType())) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Cannot assign a value of a different type from the original. Expected to assign '" +
                                value->getType()->toString() + "' to '" + addr->getType()->toString() +"'",
                            info);
                }

                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::store,
                                                addr->getType(),
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

            auto addr =  curFunc()
                ->curBlock()
                ->createInstruction(OpKind::create_alloca,
                                    ty,
                                    {initVal?initVal:(ty->isComplexType()?nullptr:Constant::getDefault(ty, info))},
                                    "create_alloca." + n);

            curFunc()->fnScope().declare(n, addr, ty);

            return addr;
        }

        TypeInfo* getTypeInfoFromNode(sakuraE::NodePtr node) {
            TypeInfo* resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Null);

            if (node->getTag() == ASTTag::TypeModifierNode) {
                if (node->hasNode(ASTTag::BasicTypeModifierNode)) {
                    resultTyInfo = getTypeInfoFromNode((*node)[ASTTag::BasicTypeModifierNode]);
                }
                else if (node->hasNode(ASTTag::ArrayTypeModifierNode)) {
                    resultTyInfo = getTypeInfoFromNode((*node)[ASTTag::ArrayTypeModifierNode]);
                }
            }

            if (node->getTag() == ASTTag::BasicTypeModifierNode) {
                auto kwNode = (*node)[ASTTag::Keyword];
                auto token = kwNode->getToken();

                switch (token.type) {
                    case TokenType::TYPE_I32:    {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Int32);
                        break;
                    }
                    case TokenType::TYPE_I64:    {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Int64);
                        break;
                    }
                    case TokenType::TYPE_UI32:   {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::UInt32);
                        break;
                    }
                    case TokenType::TYPE_UI64:   {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::UInt64);
                        break;
                    }
                    case TokenType::TYPE_F32:    {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Float32);
                        break;
                    }
                    case TokenType::TYPE_F64:    {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Float64);
                        break;
                    }
                    case TokenType::TYPE_CHAR:   {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Char);
                        break;
                    }
                    case TokenType::TYPE_BOOL:   {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Bool);
                        break;
                    }
                    case TokenType::TYPE_STRING: {
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::String);
                        break;
                    }
                    default:
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Custom);
                }
            }

            if (node->getTag() == ASTTag::ArrayTypeModifierNode) {
                auto headType = getTypeInfoFromNode((*node)[ASTTag::HeadExpr]);
                auto dims = (*node)[ASTTag::Exprs]->getChildren();

                TypeInfo* currentType = headType;
                for (auto it = dims.rbegin(); it != dims.rend(); it ++) {
                    std::vector<TypeInfo*> elements;
                    elements.push_back(currentType);
                    currentType = TypeInfo::makeArrayTypeID(elements);
                }

                resultTyInfo = currentType;
            }

            // Ptr or Ref

            if (node->hasNode(ASTTag::Op)) {
                // Is ref type info
                resultTyInfo = TypeInfo::makeRefTypeID(resultTyInfo);
            }
            else if (node->hasNode(ASTTag::Ops)) {
                // Is ptr type info
                auto ptrDepth = (*node)[ASTTag::Ops]->getChildren().size();
                for (std::size_t i = 0; i < ptrDepth; i ++)
                    resultTyInfo = TypeInfo::makePointerTypeID(resultTyInfo);
            }

            return resultTyInfo;
        }

        fzlib::String mangleFnName(fzlib::String n, FormalParamsDefine args) {
            fzlib::String result = n;
            for (auto ty: args) {
                result += "_" + ty.second->toString();
            }
            return result;
        }

        fzlib::String mangleFnName(fzlib::String n, std::vector<IRType*> args) {
            fzlib::String result = n;
            for (auto ty: args) {
                result += "_" + ty->toString();
            }
            return result;
        }

        // Used to obtain the type of the result from a non-logical binary operation
        IRType* handleUnlogicalBinaryCalc(IRValue* lhs, IRValue* rhs, PositionInfo info = {0, 0, "Normal Calc"}) {
            auto lTy = lhs->getType();
            auto rTy = rhs->getType();

            auto lIt = rankList.find(lTy->getIRTypeID());
            auto rIt = rankList.find(rTy->getIRTypeID());

            if (lIt == rankList.end() || rIt == rankList.end()) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                        "Types '" + lTy->toString() + "' and '" + rTy->toString() +
                        "' do not support '+', '-', '*', '%', and '/' operations",
                        info);
            }

            int resultRank = std::max(lIt->second, rIt->second);
            switch (resultRank) {
                case 1: return IRType::getBoolTy();
                case 2: return IRType::getCharTy();
                case 3: return IRType::getUInt32Ty();
                case 4: return IRType::getInt32Ty();
                case 5: return IRType::getUInt64Ty();
                case 6: return IRType::getInt64Ty();
                case 7: return IRType::getFloat32Ty();
                case 8: return IRType::getFloat64Ty();
                default: break;
            }

            throw SakuraError(OccurredTerm::IR_GENERATING, "Internal error: unhandled type rank", info);
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
        IRValue* visitIndexOpNode(IRValue* addr, NodePtr node);
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args);
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
