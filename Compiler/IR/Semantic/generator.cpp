#include "generator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include "Compiler/IR/Semantic/model/instruction.hpp"
#include "Compiler/IR/Semantic/model/scope.hpp"
#include "Compiler/IR/Backend/type/type.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
#include "Compiler/IR/Semantic/value/array.hpp"
#include "Compiler/IR/Semantic/value/constant.hpp"
#include "Compiler/IR/Semantic/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/magic_enum.hpp"
#include <string>
#include <variant>

namespace sakuraE::IR {
    static bool isNullLiteralExpression(const NodePtr& node) {
        if (!node) return false;
        if (node->getTag() == ASTTag::LiteralNode && node->hasNode(ASTTag::Literal)) {
            return (*node)[ASTTag::Literal]->getToken().type == TokenType::NULL_LITERAL;
        }
        const auto children = node->getChildren();
        return children.size() == 1 && isNullLiteralExpression(children.front());
    }

    void IRGenerator::startGenerate(const fzlib::String& source, fzlib::String moduleName) {
        if (hasGenerated) {
            throw std::logic_error("IRGenerator supports one source generation per instance");
        }

        program.buildModule(moduleName, {1, 1, "Start of the whole program"});
        hasGenerated = true;
        parsedStatements.clear();

        sakuraE::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        TokenIter current = tokens.cbegin();
        while (current->type != TokenType::_EOF_) {
            auto result = StatementParser::parse(current, tokens.cend());
            if (result.status == ParseStatus::FAILED) {
                if (!result.err) {
                    throw std::runtime_error("Error: Parse failed with NULL error object at token: ");
                }
                throw *result.err;
            }

            parsedStatements.push_back(result.val->genResource());
            current = result.end;
        }

        for (const auto& node : parsedStatements) {
            NodePtr statement = node->getTag() == ASTTag::Stmt ? (*node)[ASTTag::Stmt] : node;
            if (statement->getTag() != ASTTag::StructDefineStmtNode) {
                continue;
            }

            const auto nameToken = (*statement)[ASTTag::Identifier]->getToken();
            curModule()->declareStruct(nameToken.content, nameToken.info);
        }

        for (const auto& node : parsedStatements) {
            visitStmt(node);
        }
        for (const auto& node : parsedStatements) {
            NodePtr statement = node->getTag() == ASTTag::Stmt ? (*node)[ASTTag::Stmt] : node;
            if (statement->getTag() == ASTTag::StructDefineStmtNode) {
                const auto token = (*statement)[ASTTag::Identifier]->getToken();
                const auto* declaration = curModule()->lookupStruct(token.content);
                auto* type = program.getContext().typeInfoPool().makeStructTypeID(declaration->getDeclId());
                program.validateSemanticType(type, token.info);
            }
        }
    }

    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto token = (*node)[ASTTag::Literal]->getToken();
        if (token.type == TokenType::NULL_LITERAL) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "null requires a target nullable struct type.",
                token.info);
        }

        auto literal = Constant::getFromToken(token);
        auto semanticType = semanticTypeOf(literal);

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::constant,
                lowerStorageType(semanticType),
                semanticType,
                {literal},
                "const." + literal->toString()
            );
    }

    IRValue* IRGenerator::materializeNull(TypeInfo* targetType, PositionInfo info) {
        if (!targetType) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null requires a target type.", info);
        }
        if (!targetType->isNullable()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null can only initialize a nullable type.", info);
        }
        const auto* baseType = targetType->getBase();
        if (!baseType->isStruct() && !baseType->isArray()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null can only initialize a nullable struct or array type.", info);
        }

        auto* constant = Constant::getNullHandle(lowerStorageType(targetType), info);
        return curFunc()->curBlock()->createInstruction(
            OpKind::constant,
            lowerStorageType(targetType),
            targetType,
            {constant},
            "null"
        );
    }

    IRValue* IRGenerator::visitWholeExprNode(NodePtr node, TypeInfo* expectedType) {
        if (!isNullLiteralExpression(node)) return visitWholeExprNode(node);
        NodePtr current = node;
        while (current->getTag() != ASTTag::LiteralNode) {
            current = current->getChildren().front();
        }
        return materializeNull(expectedType,
                               (*current)[ASTTag::Literal]->getToken().info);
    }

    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        auto* indexValue = visitAddExprNode((*node)[ASTTag::HeadExpr]);
        const auto* indexType = indexValue->getSemanticType();
        if (!indexType || (indexType->getTypeID() != Int32 && indexType->getTypeID() != Int64
            && indexType->getTypeID() != UInt32 && indexType->getTypeID() != UInt64)) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Array index must be an integer.", node->getPosInfo());
        }
        const auto* base = addr->getSemanticType();
        if (base && base->isRef()) base = base->getPointeeType();
        TypeInfo* element = nullptr;
        if (base && base->isArray()) element = base->getElementType();
        else if (base && base->isPointer()) element = base->getPointeeType();
        else if (base && base->getTypeID() == String) {
            element = program.getContext().typeInfoPool().makeBasicTypeID(Char);
        }
        if (!element || element->getTypeID() == Void) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Cannot index this semantic type.", node->getPosInfo());
        }
        return curFunc()->curBlock()->createInstruction(
            OpKind::indexing, lowerStorageType(element), element, {addr, indexValue}, "indexing." + addr->getName());
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args) {
        const auto* type = addr->getSemanticType();
        const auto* signature = type ? type->getFunctionTypeInfo() : nullptr;
        if (!signature) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Value is not semantically callable.", node->getPosInfo());
        }
        auto* function = dynamic_cast<Function*>(addr);
        const bool boxedPrint = function && (function->getRawName() == "print" || function->getRawName() == "println");
        if (signature->getArgCount() != args.size()) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Function argument count does not match its declaration.", node->getPosInfo());
        }
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (!args[index]->getSemanticType()
                || (!boxedPrint && !isAssignableTo(args[index]->getSemanticType(), signature->getArgTypes()[index]))) {
                throw SakuraError(OccurredTerm::IR_GENERATING, "Function argument has an incompatible semantic type.", node->getPosInfo());
            }
        }
        auto* result = signature->getReturnType();
        return curFunc()->curBlock()->createInstruction(
            OpKind::call, lowerStorageType(result), result, args, "call." + addr->getName());
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node) {
        std::vector<IRValue*> arguments;
        const auto* type = addr->getSemanticType();
        const auto* signature = type ? type->getFunctionTypeInfo() : nullptr;
        if (!signature) throw SakuraError(OccurredTerm::IR_GENERATING, "Value is not semantically callable.", node->getPosInfo());
        auto expressions = (*node)[ASTTag::Exprs]->getChildren();
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            arguments.push_back(visitWholeExprNode(expressions[index],
                index < signature->getArgCount() ? signature->getArgTypes()[index] : nullptr));
        }
        return visitCallingOpNode(addr, node, arguments);
    }

    IRValue* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        IRValue* currentAddr = nullptr;

        if (node->hasNode(ASTTag::Identifier)) {
            auto name = (*node)[ASTTag::Identifier]->getToken().content;
            auto ops = (*node)[ASTTag::Ops]->getChildren();

            if (!ops.empty() && ops[0]->getTag() == ASTTag::CallingOpNode) {
                std::vector<IRValue*> argValues;
                auto argExprs = (*ops[0])[ASTTag::Exprs]->getChildren();
                auto candidates = curModule()->findFunctionCandidates(name, argExprs.size());
                const auto* expected = candidates.size() == 1 ? candidates[0]->getFunctionTypeInfo() : nullptr;
                for (std::size_t index = 0; index < argExprs.size(); ++index) {
                    argValues.push_back(visitWholeExprNode(argExprs[index],
                        expected ? expected->getArgTypes()[index] : nullptr));
                }
                Function* callee = nullptr;
                std::size_t bestScore = argValues.size() + 1;
                bool ambiguous = false;
                for (auto* candidate : candidates) {
                    const auto* signature = candidate->getFunctionTypeInfo();
                    if (!signature) continue;
                    std::size_t score = 0;
                    bool matches = true;
                    for (std::size_t index = 0; index < argValues.size(); ++index) {
                        auto* actual = argValues[index]->getSemanticType();
                        auto* required = signature->getArgTypes()[index];
                        if (!isAssignableTo(actual, required)) { matches = false; break; }
                        if (actual != required) ++score;
                    }
                    if (!matches) continue;
                    if (score < bestScore) { callee = candidate; bestScore = score; ambiguous = false; }
                    else if (score == bestScore) ambiguous = true;
                }
                if (ambiguous) throw SakuraError(OccurredTerm::IR_GENERATING, "Ambiguous semantic function overload.", node->getPosInfo());
                if (!callee) callee = curModule()->lookupRuntimeFunction(name);
                if (!callee) throw SakuraError(OccurredTerm::IR_GENERATING, "No compatible function overload: " + name, node->getPosInfo());
                currentAddr = visitCallingOpNode(callee, ops[0], argValues);

                for (size_t i = 1; i < ops.size(); ++i) {
                    if (ops[i]->getTag() == ASTTag::IndexOpNode)
                        currentAddr = visitIndexOpNode(currentAddr, ops[i]);
                    else
                        currentAddr = visitCallingOpNode(currentAddr, ops[i]);
                }
                return currentAddr;
            }
            else {
                currentAddr = lookup(name, node->getPosInfo())->address;
            }
        }
        else if (node->hasNode(ASTTag::IdentifierExprNode)) {
            currentAddr = visitIdentifierExprNode((*node)[ASTTag::IdentifierExprNode]);
        }

        if (node->hasNode(ASTTag::Ops)) {
            auto ops = (*node)[ASTTag::Ops]->getChildren();
            for (auto op : ops) {
                if (op->getTag() == ASTTag::IndexOpNode)
                    currentAddr = visitIndexOpNode(currentAddr, op);
                else if (op->getTag() == ASTTag::CallingOpNode)
                    currentAddr = visitCallingOpNode(currentAddr, op);
            }
        }

        return currentAddr;
    }

    IRValue* IRGenerator::visitIdentifierExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* resultAddr = visitAtomIdentifierNode(chain[0]);
        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto name = (*chain[i])[ASTTag::Identifier]->getToken().content;

            auto* baseType = resultAddr->getSemanticType();
            if (!baseType || !baseType->getBase()->isStruct()) {
                throw SakuraError(OccurredTerm::IR_GENERATING, "Member access requires a struct type.", chain[i]->getPosInfo());
            }
            TypeInfo* resultSemanticTy = nullptr;
            if (const auto* decl = program.resolveStruct(*baseType->getBase()->getStructDeclId());
                decl && decl->find(name)) {
                resultSemanticTy = decl->find(name)->type;
            }

            if (!resultSemanticTy) throw SakuraError(OccurredTerm::IR_GENERATING, "Unknown struct member: " + name, chain[i]->getPosInfo());

            resultAddr = curFunc()
                ->curBlock()
                ->createInstruction(
                    OpKind::gmem,
                    lowerStorageType(resultSemanticTy),
                    resultSemanticTy,
                    {resultAddr, Constant::get(name, (*chain[i])[ASTTag::Identifier]->getToken().info)},
                    "gmem." + name
                );

            if (chain[i]->hasNode(ASTTag::Ops)) {
                for (auto op: (*chain[i])[ASTTag::Ops]->getChildren()) {
                    switch (op->getTag()) {
                        case ASTTag::IndexOpNode: {
                            resultAddr = visitIndexOpNode(resultAddr, op);
                            break;
                        }
                        case ASTTag::CallingOpNode: {
                            resultAddr = visitCallingOpNode(resultAddr, op);
                            break;
                        }
                        default: break;
                    }
                }
            }
        }

        IRValue* resultValue = resultAddr;
        if (node->hasNode(ASTTag::PreOp)) {
            auto preOp = (*node)[ASTTag::PreOp]->getToken();
            switch (preOp.type) {
                case TokenType::LGC_NOT: {
                    resultValue = createLoad(resultAddr, preOp.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::lgc_not,
                            lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                            program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                            {resultValue},
                            "lgc_not." + resultValue->getName()
                        );
                    break;
                }
                case TokenType::AINC: {
                    resultValue = createLoad(resultAddr, preOp.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::add,
                            lowerStorageType(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            {resultValue, Constant::get(1)},
                            "add"
                        );

                    return createStore(resultAddr, resultValue, preOp.info);
                    break;
                }
                case TokenType::SDEC: {
                    resultValue = createLoad(resultAddr, preOp.info);
                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::sub,
                            lowerStorageType(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            {resultValue, Constant::get(1)},
                            "sub"
                        );

                    return createStore(resultAddr, resultValue, preOp.info);
                    break;
                }
                case TokenType::AND: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            ensureStableAddressableLValue(resultAddr, node->getPosInfo(), false);
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    lowerStorageType(program.getContext().typeInfoPool().makePointerTypeID(resultAddr->getSemanticType())),
                                    program.getContext().typeInfoPool().makePointerTypeID(resultAddr->getSemanticType()),
                                    {resultAddr},
                                    "gaddr." + resultAddr->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the address of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                case TokenType::KEYWORD_REF: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            ensureStableAddressableLValue(resultAddr, node->getPosInfo(), true);
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    lowerStorageType(program.getContext().typeInfoPool().makeRefTypeID(resultAddr->getSemanticType())),
                                    program.getContext().typeInfoPool().makeRefTypeID(resultAddr->getSemanticType()),
                                    {resultAddr},
                                    "gaddr.ref." + resultAddr->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the reference of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                case TokenType::MUL: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            if (!resultAddr->getSemanticType() || !resultAddr->getSemanticType()->isPointer()) {
                                throw SakuraError(
                                    OccurredTerm::IR_GENERATING,
                                    "Cannot deref a non pointer identifier.",
                                    node->getPosInfo()
                                );
                            }
                            auto load = createLoad(resultAddr, preOp.info);
                            auto* pointee = resultAddr->getSemanticType()->getPointeeType();
                            if (pointee->getTypeID() == Void) throw SakuraError(OccurredTerm::IR_GENERATING, "Cannot dereference void pointer.", node->getPosInfo());
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::deref,
                                    lowerStorageType(pointee),
                                    pointee,
                                    {load},
                                    "deref." + load->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the address of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                default:
                    break;
            }
        }

        if (node->hasNode(ASTTag::Op)) {
            auto op = (*node)[ASTTag::Op]->getToken();
            switch (op.type) {
                case TokenType::AINC: {
                    resultValue = createLoad(resultAddr, op.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::add,
                            lowerStorageType(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            {resultValue, Constant::get(1)},
                            "add"
                        );

                    createStore(resultAddr, resultValue, op.info);
                    break;
                }
                case TokenType::SDEC: {
                    resultValue = createLoad(resultAddr, op.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::sub,
                            lowerStorageType(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            {resultValue, Constant::get(1)},
                            "sub"
                        );

                    createStore(resultAddr, resultValue, op.info);
                    break;
                }
                default:
                    break;
            }
        }

        return resultValue;
    }

    IRValue* IRGenerator::visitInnerCallableExprNode(NodePtr node) {
        auto callingNode = (*node)[ASTTag::CallingOpNode];

        if (node->hasNode(ASTTag::Sizeof)) {
            auto exprs = (*callingNode)[ASTTag::Exprs]->getChildren();
            if (exprs.size() != 1) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'sizeof' operator requires exactly one argument!",
                    node->getPosInfo()
                );
            }

            auto argType = inferExprType(exprs.front(), exprs.front()->getPosInfo());
            validateSizeofType(argType, exprs.front()->getPosInfo());
            auto sizeofValue = curFunc()->
                curBlock()->
                createInstruction(
                    OpKind::_sizeof,
                    lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64)),
                    program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64),
                    {},
                    "sizeof"
                );
            static_cast<Instruction*>(sizeofValue)->setTypeOperand(lowerStorageType(argType));
            return sizeofValue;
        }

        std::vector<IRValue*> args;
        for (auto argNode: (*callingNode)[ASTTag::Exprs]->getChildren()) {
            args.push_back(visitWholeExprNode(argNode));
        }

        if (node->hasNode(ASTTag::Typeof)) {
            if (args.size() != 1) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'typeof' Operator only support one argument!",
                    node->getPosInfo()
                );
            }

            return curFunc()->
                curBlock()->
                createInstruction(
                    OpKind::_typeof,
                    lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::TypeDescriptor)),
                    program.getContext().typeInfoPool().makeBasicTypeID(TypeDescriptor),
                    args,
                    "typeof"
                );
        }
        else throw SakuraError(
            OccurredTerm::IR_GENERATING,
            "Unknown InnerCallable Operator",
            node->getPosInfo()
        );
    }

    IRValue* IRGenerator::visitPrimExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::Literal)) {
            return visitLiteralNode((*node)[ASTTag::Literal]);
        }
        else if (node->hasNode(ASTTag::InnerCallableOpExprNode)) {
            return visitInnerCallableExprNode((*node)[ASTTag::InnerCallableOpExprNode]);
        }
        else if (node->hasNode(ASTTag::Identifier)) {
            auto result = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
            if (auto inst = dynamic_cast<Instruction*>(result)) {
                if (inst->isLValue()) return createLoad(result, node->getPosInfo());
            }
            return result;
        }
        else {
            return visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        }
    }

    IRValue* IRGenerator::visitMulExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* lhs = visitPrimExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitPrimExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::MUL: {
                        lhs = curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::mul, lowerStorageType(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "mul");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::div, lowerStorageType(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "div");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::mod, lowerStorageType(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "mod");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    IRValue* IRGenerator::visitAddExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* lhs = visitMulExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitMulExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::ADD: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::add, lowerStorageType(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "add");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::sub, lowerStorageType(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "sub");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    IRValue* IRGenerator::visitLogicExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* lhs = visitAddExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitAddExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_LS_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_ls_than, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_ls_than");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_ls_than, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_ls_than");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_mr_than, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_mr_than");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_mr_than, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_mr_than");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_equal, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_equal");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_not_equal, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_not_equal");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    IRValue* IRGenerator::visitBinaryExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* lhs = visitLogicExprNode(chain[0]);

        if (!node->hasNode(ASTTag::Ops)) {
            return lhs;
        }

        static int binaryID = 0;
        fzlib::String resultAddrName = "tbv." + std::to_string(binaryID);
        binaryID ++;
        IRValue* resultAddr = createAlloca(resultAddrName, lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool)),
                                           program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                           lhs, node->getPosInfo());

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();
            long beforeBlockIndex = curFunc()->cur();
            IRValue* mergeBlock = curFunc()->buildBlock("short.cur.merge");
            long shortCurBlockIndex = curFunc()->cur();
            for (std::size_t i = 1; i < chain.size(); i ++) {
                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_AND: {
                        static int andRhsBlockID = 0;
                        IRValue* rhsBlock = curFunc()->buildBlock("and.rhs" + std::to_string(andRhsBlockID));
                        andRhsBlockID ++;
                        long rhsBlockIndex = curFunc()->cur();

                        curFunc()->moveCursor(rhsBlockIndex);
                        IRValue* rhs = visitLogicExprNode(chain[i]);

                        curFunc()
                            ->block(beforeBlockIndex)
                            ->createCondBr(lhs, rhsBlock, mergeBlock);

                        createStore(resultAddr, rhs, opChain[i - 1]->getToken().info);

                        curFunc()
                            ->block(rhsBlockIndex)
                            ->createBr(mergeBlock);

                        beforeBlockIndex = rhsBlockIndex;
                        break;
                    }
                    case TokenType::LGC_OR: {
                        static int orRhsBlockID = 0;
                        IRValue* rhsBlock = curFunc()->buildBlock("or.rhs" + std::to_string(orRhsBlockID));
                        orRhsBlockID ++;
                        long rhsBlockIndex = curFunc()->cur();

                        curFunc()->moveCursor(rhsBlockIndex);
                        IRValue* rhs = visitLogicExprNode(chain[i]);

                        curFunc()
                            ->block(beforeBlockIndex)
                            ->createCondBr(lhs, mergeBlock, rhsBlock);

                        createStore(resultAddr, rhs, opChain[i - 1]->getToken().info);

                        curFunc()
                            ->block(rhsBlockIndex)
                            ->createBr(mergeBlock);

                        beforeBlockIndex = rhsBlockIndex;
                        break;
                    }
                    default:
                        break;
                }
            }
            curFunc()->moveCursor(shortCurBlockIndex);
        }
        return curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::load, lowerStorageType(resultAddr->getSemanticType()),
                                            resultAddr->getSemanticType(),
                                            {resultAddr}, "load." + resultAddrName);
    }

    IRValue* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<IRValue*> rawArray;

        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* head = visitWholeExprNode(chain[0]);
        rawArray.push_back(head);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto element = visitWholeExprNode(chain[i]);
            if (!isAssignableTo(element->getSemanticType(), head->getSemanticType())) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "The types of elements in an array literal must be the same.",
                                node->getPosInfo());
            }
            rawArray.push_back(element);
        }

        IRArray* irArr = IRArray::createArray(rawArray, node->getPosInfo());
        Constant* arrConstant = Constant::get(irArr, irArr->getInfo());

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::create_array,
                                         lowerStorageType(program.getContext().typeInfoPool().makeArrayTypeID(head->getSemanticType(), rawArray.size())),
                                         program.getContext().typeInfoPool().makeArrayTypeID(head->getSemanticType(), rawArray.size()),
                                         {arrConstant},
                                        "create-array");
    }

    IRValue* IRGenerator::visitAssignExprNode(NodePtr node) {
        IRValue* resultAddr = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        IRValue* value = visitWholeExprNode((*node)[ASTTag::HeadExpr], resultAddr->getSemanticType());
        auto op = (*node)[ASTTag::Op]->getToken();

        IRValue* resultValue = resultAddr;

        switch (op.type) {
            case TokenType::ASSIGN_OP: {
                resultValue = createStore(resultAddr, value, op.info);
                break;
            }
            case TokenType::ADD_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::add,
                        lowerStorageType(handleUnlogicalBinaryCalc(resultValue, value)),
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "add"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::SUB_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::sub,
                        lowerStorageType(handleUnlogicalBinaryCalc(resultValue, value)),
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "sub"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::MUL_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::mul,
                        lowerStorageType(handleUnlogicalBinaryCalc(resultValue, value)),
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "mul"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::DIV_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::div,
                        lowerStorageType(handleUnlogicalBinaryCalc(resultValue, value)),
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "div"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            default:
                break;
        }

        return resultValue;
    }

    IRValue* IRGenerator::visitWholeExprNode(NodePtr node) {
        if (!node) return nullptr;

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

    IRValue* IRGenerator::visitTypeModifierNode(NodePtr node) {
        return curFunc()
            ->curBlock()
            ->createInstruction(OpKind::constant,
                                lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::TypeDescriptor)),
                                program.getContext().typeInfoPool().makeBasicTypeID(TypeDescriptor),
                                {Constant::get(getTypeInfoFromNode(node))},
                                "constant");
    }

    // 语句

    IRValue* IRGenerator::visitDeclareStmtNode(NodePtr node) {
        auto identifier = (*node)[ASTTag::Identifier]->getToken();
        IRValue* typeInfoIRValue = nullptr;
        TypeInfo* declaredSemanticType = nullptr;

        if (node->hasNode(ASTTag::Type)) {
            typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);
            auto* inst = dynamic_cast<Instruction*>(typeInfoIRValue);
            auto* typeInfoConst = static_cast<Constant*>(inst->getOperands()[0]);
            declaredSemanticType = typeInfoConst->getContentValue<TypeInfo*>();
        }
        IRValue* initVal = nullptr;

        if (node->hasNode(ASTTag::AssignTerm)) {
            initVal = visitWholeExprNode(
                (*node)[ASTTag::AssignTerm], declaredSemanticType);
        }

        if (!initVal && !typeInfoIRValue) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "A let statement cannot be used to declare a identifier without a specified type or an initial value.",
                            node->getPosInfo());
        }

        TypeInfo* semanticType = declaredSemanticType
            ? declaredSemanticType
            : (initVal ? initVal->getSemanticType() : nullptr);
        IRType* allocaTy = lowerStorageType(semanticType);
        return createAlloca(identifier.content, allocaTy, semanticType, initVal, node->getPosInfo());
    }

    IRValue* IRGenerator::visitExprStmtNode(NodePtr node) {
        if (node->hasNode(ASTTag::IdentifierExprNode)) {
            return visitIdentifierExprNode((*node)[ASTTag::IdentifierExprNode]);
        }
        else
            return visitAssignExprNode((*node)[ASTTag::AssignExprNode]);
    }

    IRValue* IRGenerator::visitBlockStmtNode(NodePtr node, fzlib::String blockName, long beforeBlock) {
        IRValue* block = curFunc()->buildBlock(blockName);

        if (beforeBlock != -1) {
            curFunc()
                ->block(beforeBlock)
                ->createInstruction(OpKind::br,
                                    lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Void)),
                                    {block},
                                    "br." + blockName);
        }

        curFunc()->fnScope().enter();
        curFunc()->curBlock()->createEnterScope();

        curFunc()->moveCursor(curFunc()->cur());

        for (auto stmt: (*node)[ASTTag::Stmts]->getChildren()) {
            visitStmt(stmt);
        }


        curFunc()->curBlock()->createLeaveScope();
        curFunc()->fnScope().leave();

        return block;
    }

    IRValue* IRGenerator::visitIfStmtNode(NodePtr node) {
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int beforeBlockIndex = curFunc()->cur();

        // if.then：条件为真时执行的基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "if.then");
        int thenExitBlockIndex = curFunc()->cur();
        //

        // if.else：条件为假时执行的基本块
        IRValue* elseBlock = nullptr;
        int elseExitBlockIndex = -1;

        if (node->hasNode(ASTTag::ElseStmtNode)) {
            auto block = (*(*node)[ASTTag::ElseStmtNode])[ASTTag::Block];
            elseBlock = visitBlockStmtNode(block, "if.else");
            elseExitBlockIndex = curFunc()->cur();
        }
        //

        // if.merge：条件分支汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("if.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        // before -> then 或 else/merge
        curFunc()
            ->block(beforeBlockIndex)
            ->createCondBr(cond, thenBlock, (elseBlock?elseBlock:mergeBlock));
        //

        // then -> merge
        curFunc()
            ->block(thenExitBlockIndex)
            ->createBr(mergeBlock);
        //

        // else -> merge
        if (elseBlock) {
            curFunc()
            ->block(elseExitBlockIndex)
            ->createBr(mergeBlock);
        }
        //

        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }


    IRValue* IRGenerator::visitWhileStmtNode(NodePtr node) {
        int beforeBlockIndex = curFunc()->cur();

        // while.prep：循环条件准备基本块
        IRValue* prepareBlock = curFunc()->buildBlock("while.prep");
        int prepareBlockIndex = curFunc()->cur();

        curFunc()
            ->block(beforeBlockIndex)
            ->createBr(prepareBlock);

        curFunc()->moveCursor(prepareBlockIndex);
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int prepareExitBlockIndex = curFunc()->cur();
        //

        // while.merge：循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("while.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(prepareBlock, mergeBlock);

        // while.then：循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "while.then");
        int thenExitBlockIndex = curFunc()->cur();
        //


        // then -> prep
        curFunc()
            ->block(thenExitBlockIndex)
            ->createBr(prepareBlock);
        //

        // prep -> merge 或 then
        curFunc()
            ->block(prepareExitBlockIndex)
            ->createCondBr(cond, thenBlock, mergeBlock);
        //
        curFunc()->leaveLoop();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    IRValue* IRGenerator::visitForStmtNode(NodePtr node) {
        curFunc()->fnScope().enter();

        // for 初始化（位于 beforeBlock 中）
        if (node->hasNode(ASTTag::DeclareStmtNode)) {
            visitDeclareStmtNode((*node)[ASTTag::DeclareStmtNode]);
        }
        int initExitIndex = curFunc()->cur();

        // for.cond：循环条件基本块
        IRValue* condBlock = curFunc()->buildBlock("for.cond");
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int condBlockExitIndex = curFunc()->cur();
        //

        // for.body：循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "for.body");
        int thenBlockExitIndex = curFunc()->cur();
        //

        // for.step：循环步进基本块
        IRValue* stepBlock = curFunc()->buildBlock("for.step");
        visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        int stepBlockExitIndex = curFunc()->cur();
        //

        // for.merge：循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("for.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(condBlock, mergeBlock);

        // init -> cond
        curFunc()
            ->block(initExitIndex)
            ->createBr(condBlock);
        //

        // cond -> body 或 merge
        curFunc()
            ->block(condBlockExitIndex)
            ->createCondBr(cond, thenBlock, mergeBlock);
        //

        // body -> step
        curFunc()
            ->block(thenBlockExitIndex)
            ->createBr(stepBlock);
        //

        // step -> cond
        curFunc()
            ->block(stepBlockExitIndex)
            ->createBr(condBlock);
        //

        curFunc()->leaveLoop();
        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    IRValue* IRGenerator::visitRepeatStmtNode(NodePtr node) {
        curFunc()->fnScope().enter();

        int beforeBlockIndex = curFunc()->cur();

        // repeat.prepare：重复循环准备基本块
        IRValue* prepareBlock = curFunc()->buildBlock("repeat.prepare");
        static int repeat_counter = 1;
        IRValue* counter = createAlloca(
            "$repeat_counter." + std::to_string(repeat_counter),
            lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32)),
            program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32),
            Constant::get((int)0), node->getPosInfo()
        );
        IRValue* limitValue = visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        int prepareBlockExitIndex = curFunc()->cur();
        //

        // repeat.cond：重复循环条件基本块
        IRValue* condBlock = curFunc()->buildBlock("repeat.cond");
        IRValue* counterLoadVal = createLoad(counter, node->getPosInfo());
        IRValue* condResult = curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::lgc_ls_than,
                lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(Bool)),
                program.getContext().typeInfoPool().makeBasicTypeID(Bool),
                {counterLoadVal, limitValue},
                "lgc_ls_than"
            );
        int condBlockExitIndex = curFunc()->cur();
        //

        // repeat.then：重复循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "repeat.then");
        int thenBlockExitIndex = curFunc()->cur();
        //

        // repeat.step：重复循环步进基本块
        IRValue* stepBlock = curFunc()->buildBlock("repeat.step");
        IRValue* stepValue = curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::add,
                lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(Int32)),
                program.getContext().typeInfoPool().makeBasicTypeID(Int32),
                {counterLoadVal, Constant::get((int)1)},
                "add"
            );
        createStore(counter, stepValue, node->getPosInfo());
        curFunc()
            ->curBlock()
            ->createBr(condBlock);
        //

        // repeat.merge：重复循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("repeat.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(condBlock, mergeBlock);

        // before -> prepare
        curFunc()
            ->block(beforeBlockIndex)
            ->createBr(prepareBlock);
        //

        // cond -> then 或 merge
        curFunc()
            ->block(condBlockExitIndex)
            ->createCondBr(condResult, thenBlock, mergeBlock);
        //

        // then -> step
        curFunc()
            ->block(thenBlockExitIndex)
            ->createBr(stepBlock);
        //

        // prepare -> cond
        curFunc()
            ->block(prepareBlockExitIndex)
            ->createBr(condBlock);
        //

        curFunc()->leaveLoop();
        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);

        return mergeBlock;
    }

    IRValue* IRGenerator::visitMatchStmtNode(NodePtr node) {
        IRValue* identifier = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        IRValue* idenValue = createLoad(identifier, node->getPosInfo());

        std::vector<IRValue*> caseBlocks;
        std::vector<std::tuple<int, IRValue*, IRValue*>> caseBlockPairs;
        IRValue* defaultThenBlock = nullptr;

        std::vector<NodePtr> cases = (*node)[ASTTag::Cases]->getChildren();

        int beforeBlockIndex = curFunc()->cur();

        IRValue* mergeBlock = curFunc()->buildBlock("match.merge");
        int mergeBlockExitIndex = curFunc()->cur();

        bool hasDefault = false;
        for (std::size_t i = 0; i < cases.size(); i ++) {
            static int matchCaseIndex = 0;

            auto cs = cases[i];
            if (cs->hasNode(ASTTag::Default)) {
                if (i != cases.size() - 1) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot put default block to the middle of the cases.",
                        cs->getPosInfo()
                    );
                }
                hasDefault = true;
                defaultThenBlock = visitBlockStmtNode((*cs)[ASTTag::Block], "match.default");
                curFunc()
                    ->curBlock()
                    ->createBr(mergeBlock);
            }
            else if (cs->hasNode(ASTTag::HeadExpr)) {
                IRValue* caseBlock = curFunc()->buildBlock("match.case." + std::to_string(matchCaseIndex));
                IRValue* targetValue = visitWholeExprNode((*cs)[ASTTag::HeadExpr]);
                IRValue* condResult = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::lgc_equal,
                        lowerStorageType(program.getContext().typeInfoPool().makeBasicTypeID(Bool)),
                        program.getContext().typeInfoPool().makeBasicTypeID(Bool),
                        {idenValue, targetValue},
                        "lgc_equal"
                    );
                int caseBlockExitIndex = curFunc()->cur();
                caseBlocks.push_back(caseBlock);

                IRValue* thenBlock = visitBlockStmtNode((*cs)[ASTTag::Block], "match.then." + std::to_string(matchCaseIndex));
                int thenBlockExitIndex = curFunc()->cur();

                caseBlockPairs.emplace_back(caseBlockExitIndex, condResult, thenBlock);

                curFunc()
                    ->block(thenBlockExitIndex)
                    ->createBr(mergeBlock);
            }
            matchCaseIndex ++;
        }

        if (!hasDefault)
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Match Statement must have 'default' case.",
                node->getPosInfo()
            );

        for (std::size_t i = 0; i < caseBlockPairs.size(); i ++) {
            int caseBlockExitIndex = std::get<0>(caseBlockPairs[i]);
            IRValue* condResult = std::get<1>(caseBlockPairs[i]);
            IRValue* thenBlock = std::get<2>(caseBlockPairs[i]);

            if (i != cases.size() - 2)
                curFunc()
                    ->block(caseBlockExitIndex)
                    ->createCondBr(condResult, thenBlock, caseBlocks[i + 1]);
            else
                curFunc()
                    ->block(caseBlockExitIndex)
                    ->createCondBr(condResult, thenBlock, defaultThenBlock);
        }

        if (caseBlockPairs.empty()) {
            curFunc()
                ->block(beforeBlockIndex)
                ->createBr(defaultThenBlock);
        }
        else {
            curFunc()
                ->block(beforeBlockIndex)
                ->createBr(caseBlocks[0]);
        }

        curFunc()->moveCursor(mergeBlockExitIndex);

        return mergeBlock;
    }

    IRValue* IRGenerator::visitFuncDefineStmtNode(NodePtr node) {
        auto fnName = (*node)[ASTTag::Identifier]->getToken().content;
        TypeInfo* semanticReturnType = getTypeInfoFromNode((*node)[ASTTag::Type]);
        IRType* retType = lowerStorageType(semanticReturnType);
        FormalParamsDefine params;
        std::vector<TypeInfo*> paramSemanticTypes;

        if (node->hasNode(ASTTag::Args)) {
            auto typeList = (*node)[ASTTag::Args]->getChildren()[0];
            auto nameList = (*node)[ASTTag::Args]->getChildren()[1];
            for (std::size_t i = 0; i < typeList->getChildren().size(); i ++) {
                auto tyInfo = getTypeInfoFromNode(typeList->getChildren()[i]);
                IRType* argType = lowerStorageType(tyInfo);
                paramSemanticTypes.push_back(tyInfo);
                fzlib::String argName = nameList->getChildren()[i]->getToken().content;

                params.push_back(std::make_pair<fzlib::String, IRType*>(std::move(argName), std::move(argType)));
            }
        }

        fnName = mangleFnName(fnName, paramSemanticTypes);
        IRValue* fn = curModule()->buildFunction(
            fnName, retType, params, node->getPosInfo(),
            paramSemanticTypes, semanticReturnType);
        long initBlockIndex = curFunc()->cur();

        if (node->hasNode(ASTTag::Args)) {
            for (std::size_t i = 0; i < params.size(); ++i) {
                auto& arg = params[i];
                createParam(arg.first, arg.second, paramSemanticTypes[i], node->getPosInfo());
            }
        }

        if (fnName == "main" && semanticReturnType->getTypeID() != Int32) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "'main' function must return 'i32' value.",
                (*node)[ASTTag::Type]->getPosInfo()
            );
        }

        curFunc()->setFuncDefineInfo(
            params, retType, paramSemanticTypes, semanticReturnType);

        visitBlockStmtNode((*node)[ASTTag::Block], "fn." + fnName, initBlockIndex);

        if (!curFunc()->getReturnChecker()) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A Function must have a return statement",
                node->getPosInfo()
            );
        }
        return fn;
    };

    IRValue* IRGenerator::visitStructDefineStmtNode(NodePtr node) {
        const auto name = (*node)[ASTTag::Identifier]->getToken();
        std::vector<SemanticField> members;
        std::map<fzlib::String, Constant*> defaultValues;
        std::map<fzlib::String, bool> memberNames;
        if (node->hasNode(ASTTag::Members)) {
            for (auto& member: (*node)[ASTTag::Members]->getChildren()) {
                const auto memberToken = (*member)[ASTTag::Identifier]->getToken();
                if (!memberNames.emplace(memberToken.content, true).second) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Duplicate struct member: '" + memberToken.content + "'",
                        memberToken.info
                    );
                }

                SemanticField info;
                info.name = memberToken.content;
                info.type = getTypeInfoFromNode((*member)[ASTTag::TypeModifierNode]);
                info.info = memberToken.info;

                Constant* defaultValue = nullptr;

                if (member->hasNode(ASTTag::WholeExprNode)) {
                    auto defaultExpr = (*member)[ASTTag::WholeExprNode];
                    if (!isNullLiteralExpression(defaultExpr)) {
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Struct member defaults must be literal values.",
                            defaultExpr->getPosInfo());
                    }
                    const auto* baseType = info.type->getBase();
                    if (!info.type->isNullable() ||
                        (!baseType->isStruct() && !baseType->isArray())) {
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "null can only default-initialize a nullable struct or array member.",
                            defaultExpr->getPosInfo());
                    }
                    defaultValue = Constant::getNullHandle(lowerStorageType(info.type), defaultExpr->getPosInfo());
                }

                if (info.type->getBase()->isBasic() && info.type->getBase()->getTypeID() == TypeID::Void) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "A struct member cannot have type 'void'.",
                        memberToken.info
                    );
                }

                members.push_back(info);
                if (defaultValue) {
                    defaultValues.emplace(info.name, defaultValue);
                }
            }
        }

        curModule()->implStruct(name.content, std::move(members), std::move(defaultValues), name.info);
        return nullptr;
    }

    IRValue* IRGenerator::visitReturnStmtNode(NodePtr node) {
        if (!node->hasNode(ASTTag::HeadExpr)) {
            if (curFunc()->getSemanticReturnType()->getTypeID() != Void) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "A non-void function must return a value of its declared return type.",
                    node->getPosInfo()
                );
            }
        }
        else if (node->hasNode(ASTTag::HeadExpr) && curFunc()->getSemanticReturnType()->getTypeID() == Void) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A function with a void return type cannot return a value of any type.",
                (*node)[ASTTag::HeadExpr]->getPosInfo()
            );
        }

        if (node->hasNode(ASTTag::HeadExpr)) {
            IRValue* retValue = nullptr;
            retValue = visitWholeExprNode(
                (*node)[ASTTag::HeadExpr],
                curFunc()->getSemanticReturnType());
            if (!isAssignableTo(retValue->getSemanticType(),
                                curFunc()->getSemanticReturnType())) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "The semantic type of the returned value is incompatible with the function return type.",
                    (*node)[ASTTag::HeadExpr]->getPosInfo());
            }
            curFunc()->setReturnChecker(true);
            return curFunc()
                        ->curBlock()
                        ->createReturn(retValue);
        }
        else {
            curFunc()->setReturnChecker(true);
            return curFunc()
                        ->curBlock()
                        ->createReturn();
        }
    }

    IRValue* IRGenerator::visitBreakStmtNode(NodePtr node) {
        if (curFunc()->isLookEmpty()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Break Statement used out of loop",
                            node->getPosInfo());
        }

        IRValue* target = curFunc()->getLoopTop().breakTarget;
        return curFunc()->curBlock()->createBr(target);
    }

    IRValue* IRGenerator::visitContinueStmtNode(NodePtr node) {
        if (curFunc()->isLookEmpty()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Continue Statement used out of loop",
                            node->getPosInfo());
        }

        IRValue* target = curFunc()->getLoopTop().continueTarget;
        return curFunc()->curBlock()->createBr(target);
    }

    IRValue* IRGenerator::visitStmt(NodePtr node) {
        NodePtr stmt;
        if (node->getTag() == ASTTag::Stmt)
            stmt = (*node)[ASTTag::Stmt];
        else
            stmt = node;

        if (stmt->getTag() == ASTTag::DeclareStmtNode) {
            return visitDeclareStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ExprStmtNode) {
            return visitExprStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::IfStmtNode) {
            return visitIfStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::WhileStmtNode) {
            return visitWhileStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ForStmtNode) {
            return visitForStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::BlockStmtNode) {
            return visitBlockStmtNode(stmt, "blockStmt");
        }
        else if (stmt->getTag() == ASTTag::FuncDefineStmtNode) {
            return visitFuncDefineStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::StructDefineStmtNode) {
            return visitStructDefineStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ReturnStmtNode) {
            return visitReturnStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::BreakStmtNode) {
            return visitBreakStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::RepeatStmtNode) {
            return visitRepeatStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::MatchStmtNode) {
            return visitMatchStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ContinueStmtNode) {
            return visitContinueStmtNode(stmt);
        }

        throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Unknown Statement to generate",
                            node->getPosInfo());
    }
}
