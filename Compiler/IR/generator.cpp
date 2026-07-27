#include "generator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/model/function.hpp"
#include "Compiler/IR/model/instruction.hpp"
#include "Compiler/IR/model/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/IR/value/array.hpp"
#include "Compiler/IR/value/constant.hpp"
#include "Compiler/IR/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/magic_enum.hpp"
#include <string>
#include <variant>

namespace sakuraE::IR {
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
    }

    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto literal = Constant::getFromToken((*node)[ASTTag::Literal]->getToken());
        auto semanticType = semanticTypeForStorage(literal->getType());

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::constant,
                literal->getType(),
                semanticType,
                {literal},
                "const." + literal->toString()
            );
    }

    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        auto indexValue = visitAddExprNode((*node)[ASTTag::HeadExpr]);
        auto ty = addr->getType();

        // 检查是否为左值
        if (ty->isArray()) {
            ty = static_cast<IRArrayType*>(ty)->getElementType();
        }
        else if (ty->isString()) {
            ty = IRType::getCharTy();
        }
        else if (ty->isPointer()) {
            ty = static_cast<IRPointerType*>(ty)->getElementType();
            if (ty->getIRTypeID() == CharTyID) {}
            else goto err_case;
        }
        else if (ty->isRef()) {
            ty = static_cast<IRRefType*>(ty)->getElementType();
            if (ty->isArray()) {
                ty = static_cast<IRArrayType*>(ty)->getElementType();
            }
            else if (ty->isString()) {
                ty = IRType::getCharTy();
            }
            else if (ty->isPointer()) {
                ty = static_cast<IRPointerType*>(ty)->getElementType();
                if (ty->getIRTypeID() != CharTyID) goto err_case;
            }
            else {
                goto err_case;
            }
        }
        else {
            err_case:
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Cannot index a non-array value.",
                node->getPosInfo()
            );
        }

        // 检查是否越界

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::indexing,
                ty,
                {addr, indexValue},
                "indexing." + addr->getName()
            );
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args) {
        IRType* retType = IRType::getVoidTy();

        if (auto fn = dynamic_cast<Function*>(addr)) {
            retType = fn->getReturnType();
        }
        else {
            auto ty = addr->getType();
            if (ty->isPointer()) ty = ty->unwrapPointer();
            if (ty->getIRTypeID() == IRTypeID::FunctionTyID) {
                retType = static_cast<IRFunctionType*>(ty)->getReturnType();
            }
        }

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::call,
                retType,
                args,
                "call." + addr->getName()
            );
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node) {
        std::vector<IRValue*> params;
        for (auto argExpr : (*node)[ASTTag::Exprs]->getChildren()) {
            params.push_back(visitWholeExprNode(argExpr));
        }
        return visitCallingOpNode(addr, node, params);
    }

    IRValue* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        IRValue* currentAddr = nullptr;

        if (node->hasNode(ASTTag::Identifier)) {
            auto name = (*node)[ASTTag::Identifier]->getToken().content;
            auto ops = (*node)[ASTTag::Ops]->getChildren();

            if (!ops.empty() && ops[0]->getTag() == ASTTag::CallingOpNode) {
                std::vector<IRType*> argTypes;
                std::vector<IRValue*> argValues;
                for (auto argExpr : (*ops[0])[ASTTag::Exprs]->getChildren()) {
                    auto val = visitWholeExprNode(argExpr);
                    argValues.push_back(val);
                    argTypes.push_back(val->getType());
                }

                auto mangledName = mangleFnName(name, argTypes);

                auto symbol = curModule()->lookup(mangledName);
                IRValue* callee = nullptr;
                if (!symbol) {
                    /* 打印是通用的 RuntimeValue API。其 IR 声明以字符串重载表示，
                     * 但对于每一种源语言类型，LLVM ABI 都使用 RuntimeValue*。
                     */
                    auto* runtimeFn = curModule()->lookupRuntimeFunction(name);
                    if (runtimeFn) {
                        callee = runtimeFn;
                    }
                }
                if (!symbol) {
                    if (callee) {
                        currentAddr = visitCallingOpNode(callee, ops[0], argValues);
                        for (size_t i = 1; i < ops.size(); ++i) {
                            if (ops[i]->getTag() == ASTTag::IndexOpNode)
                                currentAddr = visitIndexOpNode(currentAddr, ops[i]);
                            else
                                currentAddr = visitCallingOpNode(currentAddr, ops[i]);
                        }
                        return currentAddr;
                    }
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Unknown identifier: " + mangledName,
                        node->getPosInfo());
                }

                currentAddr = symbol->address;
                currentAddr = visitCallingOpNode(currentAddr, ops[0], argValues);

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
            resultAddr = curFunc()
                ->curBlock()
                ->createInstruction(
                    OpKind::gmem,
                    resultAddr->getType(),
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
                            IRType::getBoolTy(),
                            TypeInfo::makeBasicTypeID(TypeID::Bool),
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
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            semanticTypeForStorage(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
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
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1)),
                            semanticTypeForStorage(handleUnlogicalBinaryCalc(resultAddr, Constant::get(1))),
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
                                    IRType::getPointerTo(resultAddr->getType()),
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
                                    IRType::getRefTo(resultAddr->getType()),
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
                            if (!resultAddr->getType()->isPointer()) {
                                throw SakuraError(
                                    OccurredTerm::IR_GENERATING,
                                    "Cannot deref a non pointer identifier.",
                                    node->getPosInfo()
                                );
                            }
                            auto load = createLoad(resultAddr, preOp.info);
                            auto loadedType = load->getType();
                            loadedType = dynamic_cast<IRPointerType*>(loadedType)->getElementType();
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::deref,
                                    loadedType,
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
                    IRType::getUInt64Ty(),
                    {},
                    "sizeof." + argType->toString()
                );
            static_cast<Instruction*>(sizeofValue)->setTypeOperand(argType);
            return sizeofValue;
        }

        std::vector<IRValue*> args;
        for (auto argNode: (*callingNode)[ASTTag::Exprs]->getChildren()) {
            args.push_back(visitWholeExprNode(argNode));
        }

        if (node->hasNode(ASTTag::Typeof)) {
            if (args.size() > 1) {
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
                    IRType::getTypeInfoTy(),
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
        else if (node->hasNode(ASTTag::InnerCallabeOpExprNode)) {
            return visitInnerCallableExprNode((*node)[ASTTag::InnerCallabeOpExprNode]);
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
                            ->createInstruction(OpKind::mul, handleUnlogicalBinaryCalc(lhs, rhs),
                                                semanticTypeForStorage(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                {lhs, rhs}, "mul");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::div, handleUnlogicalBinaryCalc(lhs, rhs),
                                                semanticTypeForStorage(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                {lhs, rhs}, "div");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::mod, handleUnlogicalBinaryCalc(lhs, rhs),
                                                semanticTypeForStorage(handleUnlogicalBinaryCalc(lhs, rhs)),
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
                            ->createInstruction(OpKind::add, handleUnlogicalBinaryCalc(lhs, rhs),
                                                semanticTypeForStorage(handleUnlogicalBinaryCalc(lhs, rhs)),
                                                {lhs, rhs}, "add");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::sub, handleUnlogicalBinaryCalc(lhs, rhs),
                                                semanticTypeForStorage(handleUnlogicalBinaryCalc(lhs, rhs)),
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
                            ->createInstruction(OpKind::lgc_ls_than, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_ls_than");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_ls_than, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_ls_than");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_mr_than, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_mr_than");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_mr_than, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_mr_than");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_equal, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_equal");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_not_equal, IRType::getBoolTy(),
                                                TypeInfo::makeBasicTypeID(TypeID::Bool),
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
        IRValue* resultAddr = createAlloca(resultAddrName, IRType::getBoolTy(),
                                           TypeInfo::makeBasicTypeID(TypeID::Bool),
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
        Symbol<IRValue*>* symbol = curFunc()->fnScope().lookup(resultAddrName);
        return curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::load, symbol->getType(),
                                            symbol->getSemanticType(),
                                            {symbol->address}, "load." + resultAddrName);
    }

    IRValue* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<IRValue*> rawArray;

        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* head = visitWholeExprNode(chain[0]);
        rawArray.push_back(head);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto element = visitWholeExprNode(chain[i]);
            if (head->getType() != element->getType()) {
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
                                         arrConstant->getType(),
                                         TypeInfo::makeArrayTypeID(head->getSemanticType(), rawArray.size()),
                                         {arrConstant},
                                        "create-array");
    }

    IRValue* IRGenerator::visitAssignExprNode(NodePtr node) {
        IRValue* resultAddr = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        IRValue* value = visitWholeExprNode((*node)[ASTTag::HeadExpr]);
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
                                IRType::getTypeInfoTy(),
                                {Constant::get(getTypeInfoFromNode(node))},
                                "constant");
    }

    // 语句

    IRValue* IRGenerator::visitDeclareStmtNode(NodePtr node) {
        auto identifier = (*node)[ASTTag::Identifier]->getToken();
        IRValue* typeInfoIRValue = nullptr;

        if (node->hasNode(ASTTag::Type)) {
            typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);
        }
        IRValue* initVal = nullptr;

        if (node->hasNode(ASTTag::AssignTerm)) {
            initVal = visitWholeExprNode((*node)[ASTTag::AssignTerm]);
        }

        if (!initVal && !typeInfoIRValue) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "A let statement cannot be used to declare a identifier without a specified type or an initial value.",
                            node->getPosInfo());
        }

        IRType* allocaTy = nullptr;
        if (initVal && !typeInfoIRValue) allocaTy = initVal->getType();
        else {
            auto inst = dynamic_cast<Instruction*>(typeInfoIRValue);
            auto typeInfoConst = static_cast<Constant*>(inst->getOperands()[0]);
            auto typeInfo = typeInfoConst->getContentValue<TypeInfo*>();
            allocaTy = typeInfo->toIRType();
        }

        TypeInfo* semanticType = initVal ? initVal->getSemanticType() : nullptr;
        if (typeInfoIRValue) {
            auto inst = dynamic_cast<Instruction*>(typeInfoIRValue);
            auto typeInfoConst = static_cast<Constant*>(inst->getOperands()[0]);
            semanticType = typeInfoConst->getContentValue<TypeInfo*>();
        }
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
                                    IRType::getVoidTy(),
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
            IRType::getInt32Ty(),
            TypeInfo::makeBasicTypeID(TypeID::Int32),
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
                IRType::getBoolTy(),
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
                IRType::getInt32Ty(),
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
                        IRType::getBoolTy(),
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
        IRType* retType = IRType::getVoidTy();
        FormalParamsDefine params;
        std::vector<TypeInfo*> paramSemanticTypes;

        if (node->hasNode(ASTTag::Args)) {
            auto typeList = (*node)[ASTTag::Args]->getChildren()[0];
            auto nameList = (*node)[ASTTag::Args]->getChildren()[1];
            for (std::size_t i = 0; i < typeList->getChildren().size(); i ++) {
                auto tyInfo = getTypeInfoFromNode(typeList->getChildren()[i]);
                IRType* argType = tyInfo->toIRType();
                paramSemanticTypes.push_back(tyInfo);
                fzlib::String argName = nameList->getChildren()[i]->getToken().content;

                params.push_back(std::make_pair<fzlib::String, IRType*>(std::move(argName), std::move(argType)));
            }
        }

        fnName = mangleFnName(fnName, params);
        IRValue* fn = curModule()->buildFunction(fnName, retType, params, node->getPosInfo());
        long initBlockIndex = curFunc()->cur();

        if (node->hasNode(ASTTag::Args)) {
            for (std::size_t i = 0; i < params.size(); ++i) {
                auto& arg = params[i];
                createParam(arg.first, arg.second, paramSemanticTypes[i], node->getPosInfo());
            }
        }

        IRValue* typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);

        // 解箱
        auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
        auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
        TypeInfo* typeInfo = typeInfoConstant->getContentValue<TypeInfo*>();

        retType = typeInfo->toIRType();

        if (fnName == "main" && !retType->isEqual(IRType::getInt32Ty())) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "'main' function must return 'i32' value.",
                (*node)[ASTTag::Type]->getPosInfo()
            );
        }

        curFunc()->setFuncDefineInfo(params, retType);

        visitBlockStmtNode((*node)[ASTTag::Block], "fn." + fnName, initBlockIndex);

        if (!curFunc()->getReturnChecker()) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A Function must have a return statement",
                (*node)[ASTTag::HeadExpr]->getPosInfo()
            );
        }
        return fn;
    };

    IRValue* IRGenerator::visitStructDefineStmtNode(NodePtr node) {
        const auto name = (*node)[ASTTag::Identifier]->getToken();
        std::vector<IRStructType::FieldInfo> members;
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

                IRStructType::FieldInfo info;
                info.name = memberToken.content;
                info.type = getTypeInfoFromNode((*member)[ASTTag::TypeModifierNode])->toIRType();
                info.info = memberToken.info;

                if (info.type->isEqual(IRType::getVoidTy())) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "A struct member cannot have type 'void'.",
                        memberToken.info
                    );
                }

                members.push_back(info);
            }
        }

        curModule()->implStruct(name.content, std::move(members), name.info);
        return nullptr;
    }

    IRValue* IRGenerator::visitReturnStmtNode(NodePtr node) {
        if (!node->hasNode(ASTTag::HeadExpr)) {
            if (!curFunc()->getReturnType()->isEqual(IRType::getVoidTy())) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "A non-void function must return a value of its declared return type.",
                    (*node)[ASTTag::HeadExpr]->getPosInfo()
                );
            }
        }
        else if (node->hasNode(ASTTag::HeadExpr) && curFunc()->getReturnType()->isEqual(IRType::getVoidTy())) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A function with a void return type cannot return a value of any type.",
                (*node)[ASTTag::HeadExpr]->getPosInfo()
            );
        }

        if (node->hasNode(ASTTag::HeadExpr)) {
            IRValue* retValue = nullptr;
            retValue = visitWholeExprNode((*node)[ASTTag::HeadExpr]);
            if (!retValue->getType()->isEqual(curFunc()->getReturnType())) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "The type of the value in a return statement must match the function's return type. Function's return type is: " +
                        curFunc()->getReturnType()->toString() +
                        ", but your given type is: " +
                        (retValue?retValue->getType()->toString():"void type"),
                    (*node)[ASTTag::HeadExpr]->getPosInfo()
                );
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
