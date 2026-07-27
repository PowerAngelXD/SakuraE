#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/model/function.hpp"
#include "Compiler/IR/model/instruction.hpp"
#include "Compiler/IR/model/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/IR/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/String.hpp"
#include "includes/magic_enum.hpp"
#include "model/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/value/constant.hpp"
#include "Compiler/Frontend/lexer.h"

#include <algorithm>
#include <map>
#include <numbers>
#include <stdexcept>
#include <vector>



namespace sakuraE::IR {
    class IRGenerator {
        Program program;
        std::vector<NodePtr> parsedStatements;
        bool hasGenerated = false;

        TypeInfo* semanticTypeForStorage(IRType* type) {
            switch (type->getIRTypeID()) {
                case Integer32TyID: return TypeInfo::makeBasicTypeID(TypeID::Int32);
                case Integer64TyID: return TypeInfo::makeBasicTypeID(TypeID::Int64);
                case UInteger32TyID: return TypeInfo::makeBasicTypeID(TypeID::UInt32);
                case UInteger64TyID: return TypeInfo::makeBasicTypeID(TypeID::UInt64);
                case Float32TyID: return TypeInfo::makeBasicTypeID(TypeID::Float32);
                case Float64TyID: return TypeInfo::makeBasicTypeID(TypeID::Float64);
                case BoolTyID: return TypeInfo::makeBasicTypeID(TypeID::Bool);
                case CharTyID: return TypeInfo::makeBasicTypeID(TypeID::Char);
                case StringTyID: return TypeInfo::makeBasicTypeID(TypeID::String);
                default: return nullptr;
            }
        }

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
            // 这里不应对 Pointer 类型执行 Unwrap，否则会导致 LLVM IR 生成 store i32 i32，形成非法赋值

                return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::load,
                        addr->getType(),
                        addr->getSemanticType(),
                        {addr},
                        "load." + addr->getName());
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

                if (inst->getKind() == OpKind::deref) {
                    if (!addr->getType()->isEqual(value->getType())) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot assign a value of a different type from the original. Expected to assign '" +
                                    value->getType()->toString() + "' to '" + addr->getType()->toString() +"'",
                                info);
                    }
                }
                else {
                    if (!addr->getType()->isEqual(value->getType())) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot assign a value of a different type from the original. Expected to assign '" +
                                    value->getType()->toString() + "' to '" + addr->getType()->toString() +"'",
                                info);
                    }
                }

                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::store,
                                                addr->getType(),
                                                addr->getSemanticType(),
                                                {addr, value},
                                                "store." + addr->getName());
            }
            else {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "An L-value is required as the left operand of an assignment.",
                                    info);
            }
        }

        IRValue* createParam(fzlib::String name, IRType* ty, TypeInfo* semanticType, PositionInfo info) {
            IRType* finalType = ty;

            auto param = curFunc()
                            ->curBlock()
                            ->createInstruction(
                                OpKind::param,
                                finalType,
                                semanticType,
                                {},
                                "param." + name
                            );
            curFunc()->fnScope().declare(name, param, finalType, semanticType);

            return param;
        }

        IRValue* createAlloca(fzlib::String n, IRType* ty, TypeInfo* semanticType, IRValue* initVal, PositionInfo info) {
            if (initVal && !ty->isEqual(initVal->getType())) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot declare a variable with a type that differs from the assigned value's type. Target is: " + ty->toString() + ", but actually is: " + initVal->getType()->toString(),
                                info);
            }

            auto addr =  curFunc()
                ->curBlock()
                ->createInstruction(OpKind::create_alloca,
                                    ty,
                                    semanticType,
                                    {initVal?initVal:(ty->isComplexType()?nullptr:Constant::getDefault(ty, info))},
                                    "create_alloca." + n);

            curFunc()->fnScope().declare(n, addr, ty, semanticType);

            return addr;
        }

        bool isManagedHeapObjectType(IRType* ty) {
            if (!ty) {
                return false;
            }

            /* 这里明确区分语言层 string object 与原生 char*。
             * 只有 GC 托管对象本身，才属于“不允许暴露稳定内部地址”的对象。
             */
            if (ty->isString() || ty->isArray()) {
                return true;
            }

            if (ty->isRef()) {
                auto* refTy = static_cast<IRRefType*>(ty);
                return isManagedHeapObjectType(refTy->getElementType());
            }

            return false;
        }

        bool isInteriorGCManagedLValue(IRValue* value) {
            auto* inst = dynamic_cast<Instruction*>(value);
            if (!inst || !inst->isLValue()) {
                return false;
            }

            switch (inst->getKind()) {
                case OpKind::indexing: {
                    return isManagedHeapObjectType(inst->arg(0)->getType());
                }
                case OpKind::gmem: {
                    /* 预留给后续 struct object：
                     * 一旦 gmem 的 base 成为 GC 托管对象，这里会统一拦截 field interior address。
                     */
                    return isManagedHeapObjectType(inst->arg(0)->getType());
                }
                default:
                    return false;
            }
        }

        void ensureStableAddressableLValue(IRValue* value, PositionInfo info, bool isRefOp) {
            if (!isInteriorGCManagedLValue(value)) {
                return;
            }

            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                isRefOp
                    ? "Interior address of a GC-managed object cannot be used as a stable language-level reference."
                    : "Interior address of a GC-managed object cannot be taken as a stable language-level pointer.",
                info
            );
        }

        std::uint64_t getArrayDimension(NodePtr node) {
            if (node && node->getTag() == ASTTag::LiteralNode) {
                auto token = (*node)[ASTTag::Literal]->getToken();
                if (token.type != TokenType::INT_N) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Array dimension must be an integer literal.",
                        token.info
                    );
                }

                auto constant = Constant::getFromToken(token);
                switch (constant->getType()->getIRTypeID()) {
                    case IRTypeID::Integer32TyID: {
                        auto value = constant->getContentValue<std::int32_t>();
                        if (value > 0) return static_cast<std::uint64_t>(value);
                        break;
                    }
                    case IRTypeID::Integer64TyID: {
                        auto value = constant->getContentValue<std::int64_t>();
                        if (value > 0) return static_cast<std::uint64_t>(value);
                        break;
                    }
                    case IRTypeID::UInteger32TyID:
                        return constant->getContentValue<std::uint32_t>();
                    case IRTypeID::UInteger64TyID:
                        return constant->getContentValue<std::uint64_t>();
                    default:
                        break;
                }

                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "Array dimension must be greater than zero.",
                    token.info
                );
            }

            if (node) {
                auto children = node->getChildren();
                if (children.size() == 1) {
                    return getArrayDimension(children[0]);
                }
            }

            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Array dimension must be a compile-time integer literal.",
                node ? node->getPosInfo() : PositionInfo{0, 0, "Array dimension"}
            );
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
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                        );
                }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Int32);
                        break;
                    }
                    case TokenType::TYPE_I64:    {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                                OccurredTerm::IR_GENERATING,
                                "Cannot wrap Basic Type as Nullable Type!",
                                (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Int64);
                        break;
                    }
                    case TokenType::TYPE_UI32:   {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                                OccurredTerm::IR_GENERATING,
                                "Cannot wrap Basic Type as Nullable Type!",
                                (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::UInt32);
                        break;
                    }
                    case TokenType::TYPE_UI64:   {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::UInt64);
                        break;
                    }
                    case TokenType::TYPE_F32:    {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Float32);
                        break;
                    }
                    case TokenType::TYPE_F64:    {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Float64);
                        break;
                    }
                    case TokenType::TYPE_CHAR:   {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Char);
                        break;
                    }
                    case TokenType::TYPE_BOOL:   {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Bool);
                        break;
                    }
                    case TokenType::TYPE_STRING: {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::String);
                        break;
                    }
                    case TokenType::TYPE_VOID: {
                        if (node->hasNode(ASTTag::NullableTag)) {
                            throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Cannot wrap Basic Type as Nullable Type!",
                            (*node)[ASTTag::NullableTag]->getPosInfo()
                            );
                        }
                        resultTyInfo = TypeInfo::makeBasicTypeID(TypeID::Void);
                        break;
                    }
                    case TokenType::IDENTIFIER: {
                        auto* structType = curModule()->lookupStructType(token.content);
                        if (!structType) {
                            throw SakuraError(
                                OccurredTerm::IR_GENERATING,
                                "Unknown struct type: '" + token.content + "'",
                                token.info);
                        }
                        resultTyInfo = TypeInfo::makeStructTypeID(structType);
                        break;
                    }
                    default:
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Unsupported type modifier: '" + token.content + "'",
                            token.info);
                }
            }

            if (node->getTag() == ASTTag::ArrayTypeModifierNode) {
                auto headType = getTypeInfoFromNode((*node)[ASTTag::HeadExpr]);
                auto dims = (*node)[ASTTag::Exprs]->getChildren();

                TypeInfo* currentType = headType;
                for (auto it = dims.rbegin(); it != dims.rend(); it ++) {
                    currentType = TypeInfo::makeArrayTypeID(
                        currentType,
                        getArrayDimension(*it)
                    );
                }

                if (node->hasNode(ASTTag::NullableTag)) {
                    currentType = TypeInfo::wrapTypeAsNullable(currentType, (*node)[ASTTag::NullableTag]->getPosInfo());
                }

                resultTyInfo = currentType;
            }

            // 指针或引用
            if (node->hasNode(ASTTag::Op)) {
                // 判断是否为引用类型信息
                resultTyInfo = TypeInfo::makeRefTypeID(resultTyInfo);

                if (node->hasNode(ASTTag::NullableTag)) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot wrap Ref Type as Nullable Type!",
                        (*node)[ASTTag::NullableTag]->getPosInfo()
                    );
                }
            }
            else if (node->hasNode(ASTTag::Ops)) {
                // 判断是否为指针类型信息
                auto ptrDepth = (*node)[ASTTag::Ops]->getChildren().size();
                for (std::size_t i = 0; i < ptrDepth; i ++)
                    resultTyInfo = TypeInfo::makePointerTypeID(resultTyInfo);

                if (node->hasNode(ASTTag::NullableTag)) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot wrap Pointer Type as Nullable Type!",
                        (*node)[ASTTag::NullableTag]->getPosInfo()
                    );
                }
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

        bool isSizeofSupportedType(IRType* ty) {
            if (!ty) return false;

            switch (ty->getIRTypeID()) {
                case IRTypeID::CharTyID:
                case IRTypeID::BoolTyID:
                case IRTypeID::Integer32TyID:
                case IRTypeID::Integer64TyID:
                case IRTypeID::UInteger32TyID:
                case IRTypeID::UInteger64TyID:
                case IRTypeID::Float32TyID:
                case IRTypeID::Float64TyID:
                case IRTypeID::PointerTyID:
                    return true;
                default:
                    return false;
            }
        }

        void validateSizeofType(IRType* ty, PositionInfo info) {
            if (!isSizeofSupportedType(ty)) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'sizeof' only supports char, i32, i64, ui32, ui64, bool, f32, f64, and pointer types.",
                    info
                );
            }
        }

        IRType* inferExprType(NodePtr node, PositionInfo info = {0, 0, "sizeof"}) {
            if (!node) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                  "Cannot infer the type of an empty expression.",
                                  info);
            }

            if (node->getTag() == ASTTag::WholeExprNode) {
                if (node->hasNode(ASTTag::AddExprNode))
                    return inferExprType((*node)[ASTTag::AddExprNode], info);
                if (node->hasNode(ASTTag::BinaryExprNode))
                    return inferExprType((*node)[ASTTag::BinaryExprNode], info);
                if (node->hasNode(ASTTag::ArrayExprNode))
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                      "'sizeof' does not support array expressions.",
                                      node->getPosInfo());
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                  "'sizeof' does not support assignment expressions.",
                                  node->getPosInfo());
            }

            if (node->getTag() == ASTTag::AddExprNode ||
                node->getTag() == ASTTag::MulExprNode) {
                auto exprs = (*node)[ASTTag::Exprs]->getChildren();
                IRType* result = inferExprType(exprs.front(), info);
                for (std::size_t i = 1; i < exprs.size(); ++i) {
                    auto rhs = inferExprType(exprs[i], info);
                    auto lhsRank = rankList.find(result->getIRTypeID());
                    auto rhsRank = rankList.find(rhs->getIRTypeID());
                    if (lhsRank == rankList.end() || rhsRank == rankList.end()) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' expression contains an unsupported arithmetic type.",
                                          node->getPosInfo());
                    }
                    switch (std::max(lhsRank->second, rhsRank->second)) {
                        case 1: result = IRType::getBoolTy(); break;
                        case 2: result = IRType::getCharTy(); break;
                        case 3: result = IRType::getUInt32Ty(); break;
                        case 4: result = IRType::getInt32Ty(); break;
                        case 5: result = IRType::getUInt64Ty(); break;
                        case 6: result = IRType::getInt64Ty(); break;
                        case 7: result = IRType::getFloat32Ty(); break;
                        case 8: result = IRType::getFloat64Ty(); break;
                        default:
                            throw SakuraError(OccurredTerm::IR_GENERATING,
                                              "Internal error: unhandled sizeof expression type.",
                                              node->getPosInfo());
                    }
                }
                return result;
            }

            if (node->getTag() == ASTTag::LogicExprNode ||
                node->getTag() == ASTTag::BinaryExprNode) {
                auto exprs = (*node)[ASTTag::Exprs]->getChildren();
                IRType* result = inferExprType(exprs.front(), info);
                if (node->hasNode(ASTTag::Ops)) {
                    for (auto expr : exprs)
                        inferExprType(expr, info);
                    return IRType::getBoolTy();
                }
                return result;
            }

            if (node->getTag() == ASTTag::PrimExprNode) {
                if (node->hasNode(ASTTag::Literal)) {
                    auto literal = Constant::getFromToken(
                        (*(*node)[ASTTag::Literal])[ASTTag::Literal]->getToken());
                    return literal->getType();
                }
                if (node->hasNode(ASTTag::Identifier)) {
                    auto identifier = (*node)[ASTTag::Identifier];
                    if (identifier->hasNode(ASTTag::PreOp) ||
                        identifier->hasNode(ASTTag::Op)) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' does not evaluate side-effecting expressions.",
                                          identifier->getPosInfo());
                    }
                    auto chain = (*identifier)[ASTTag::Exprs]->getChildren();
                    if (chain.size() != 1) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' only supports a single variable expression.",
                                          identifier->getPosInfo());
                    }
                    auto atom = chain[0];
                    if (atom->hasNode(ASTTag::Ops) &&
                        !(*atom)[ASTTag::Ops]->getChildren().empty()) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' does not support indexed or called variables.",
                                          identifier->getPosInfo());
                    }
                    if (!atom->hasNode(ASTTag::Identifier)) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' could not resolve the variable identifier.",
                                          identifier->getPosInfo());
                    }
                    return lookup((*atom)[ASTTag::Identifier]->getToken().content,
                                  identifier->getPosInfo())->getType();
                }
                if (node->hasNode(ASTTag::InnerCallabeOpExprNode)) {
                    auto inner = (*node)[ASTTag::InnerCallabeOpExprNode];
                    if (inner->hasNode(ASTTag::Sizeof))
                        return IRType::getUInt64Ty();
                    return IRType::getTypeInfoTy();
                }
                return inferExprType((*node)[ASTTag::HeadExpr], info);
            }

            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "'sizeof' cannot infer the type of this expression.",
                              node->getPosInfo());
        }

        // 用于获取非逻辑二元运算的结果类型
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
        IRGenerator(fzlib::String name): program(name) {}

        void startGenerate(const fzlib::String& source, fzlib::String moduleName);

        const std::vector<NodePtr>& getParsedStatements() const {
            return parsedStatements;
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
                }
                else if (c == '}') {
                    result += "\n";
                    indent--;
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                    result += "}\n";
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                }
                else if (c == ';') {
                    result += ";\n";
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                }
                else if (c == '@') {
                    if (result.len() > 0 && result[result.len() - 1] != ' ') {
                        result += "\n";
                        for (int j = 0; j < indent * 2; j++) result += ' ';
                    }
                    result += c;
                }
                else {
                    result += c;
                }
            }
            return result;
        }

        // --- Expression ---
        IRValue* visitLiteralNode(NodePtr node);
        IRValue* visitIndexOpNode(IRValue* addr, NodePtr node);
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args);
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node);
        IRValue* visitAtomIdentifierNode(NodePtr node);
        IRValue* visitIdentifierExprNode(NodePtr node);
        IRValue* visitInnerCallableExprNode(NodePtr node);
        IRValue* visitPrimExprNode(NodePtr node);
        IRValue* visitMulExprNode(NodePtr node);
        IRValue* visitAddExprNode(NodePtr node);
        IRValue* visitLogicExprNode(NodePtr node);
        IRValue* visitBinaryExprNode(NodePtr node);
        IRValue* visitArrayExprNode(NodePtr node);
        IRValue* visitWholeExprNode(NodePtr node);
        IRValue* visitTypeModifierNode(NodePtr node);
        IRValue* visitAssignExprNode(NodePtr node);
        /* TODO：实现范围表达式
         * IRValue* visitRangeExprNode(NodePtr node);
         */

        // --- Statement ---
        IRValue* visitDeclareStmtNode(NodePtr node);
        IRValue* visitExprStmtNode(NodePtr node);
        IRValue* visitIfStmtNode(NodePtr node);
        IRValue* visitWhileStmtNode(NodePtr node);
        IRValue* visitForStmtNode(NodePtr node);
        IRValue* visitRepeatStmtNode(NodePtr node);
        IRValue* visitMatchStmtNode(NodePtr node);
        IRValue* visitBlockStmtNode(NodePtr node, fzlib::String blockName, long beforeBlock = -1);
        IRValue* visitFuncDefineStmtNode(NodePtr node);
        IRValue* visitStructDefineStmtNode(NodePtr node);
        IRValue* visitReturnStmtNode(NodePtr node);
        IRValue* visitBreakStmtNode(NodePtr node);
        IRValue* visitContinueStmtNode(NodePtr node);
        IRValue* visitStmt(NodePtr node);
    };
}

#endif /* !SAKURAE_GENERATOR_HPP */
