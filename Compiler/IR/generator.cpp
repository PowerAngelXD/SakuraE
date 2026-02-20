#include "generator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/struct/function.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/IR/value/constant.hpp"
#include "Compiler/IR/value/value.hpp"
#include "includes/magic_enum.hpp"

namespace sakuraE::IR {
    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto literal = Constant::getFromToken((*node)[ASTTag::Literal]->getToken());

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::constant, 
                literal->getType(), 
                {literal}, 
                "constant"
            );
    }

    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        auto indexValue = visitAddExprNode((*node)[ASTTag::HeadExpr]);
        auto ty = addr->getType();
        if (ty->isPointer()) {
            ty = ty->unwrapPointer();
        }

        if (ty->isArray()) {
            ty = static_cast<IRArrayType*>(ty)->getElementType();
        }
        else if (ty->isPointer()) {
            ty = static_cast<IRPointerType*>(ty)->getElementType();
            if (ty->getIRTypeID() == IRTypeID::CharTyID) {}
            else if (ty->isArray()) {
                ty = static_cast<IRArrayType*>(ty)->getElementType();
            }
            else goto error_indexing;
        }
        else {
            error_indexing:
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Cannot index a non-array value.",
                node->getPosInfo()
            );
        }

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

                auto symbol = lookup(mangledName, node->getPosInfo());
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
                            {resultValue, Constant::get(1)},
                            "sub"
                        );
                    
                    return createStore(resultAddr, resultValue, preOp.info);
                    break;
                }
                case TokenType::AND: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) 
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    IRType::getPointerTo(resultAddr->getType()),
                                    {resultAddr},
                                    "gaddr." + resultAddr->getName()
                                );
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the address of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                case TokenType::KEYWORD_REF: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) 
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    IRType::getRefTo(resultAddr->getType()),
                                    {resultAddr},
                                    "gaddr.ref." + resultAddr->getName()
                                );
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the reference of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                case TokenType::MUL: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            auto load = createLoad(resultAddr, preOp.info);
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::deref,
                                    load->getType(),
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

    IRValue* IRGenerator::visitPrimExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::Literal)) {
            return visitLiteralNode((*node)[ASTTag::Literal]);
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
                            ->createInstruction(OpKind::mul, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mul");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::div, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "div");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::mod, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mod");
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
                                ->createInstruction(OpKind::add, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "add");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::sub, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "sub");
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
                                ->createInstruction(OpKind::lgc_ls_than, IRType::getBoolTy(), {lhs, rhs}, "lgc_ls_than");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_ls_than, IRType::getBoolTy(), {lhs, rhs}, "lgc_eq_ls_than");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_mr_than, IRType::getBoolTy(), {lhs, rhs}, "lgc_mr_than");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_mr_than, IRType::getBoolTy(), {lhs, rhs}, "lgc_eq_mr_than");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_equal, IRType::getBoolTy(), {lhs, rhs}, "lgc_equal");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_not_equal, IRType::getBoolTy(), {lhs, rhs}, "lgc_not_equal");
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
        IRValue* resultAddr = createAlloca(resultAddrName, IRType::getBoolTy(), lhs, node->getPosInfo());

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
                        ->createInstruction(OpKind::load, symbol->getType(), {symbol->address}, "load." + resultAddrName);
    }

    IRValue* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<IRValue*> array;
        
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* head = visitWholeExprNode(chain[0]);
        array.push_back(head);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto element = visitWholeExprNode(chain[i]);
            if (head->getType() != element->getType()) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "The types of elements in an array literal must be the same.",
                                node->getPosInfo());
            }
            array.push_back(element);
        }

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::create_array,
                                        IRType::getArrayTy(array[0]->getType(), array.size()),
                                        array,
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

    // Statements

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
        
        return createAlloca(identifier.content, allocaTy, initVal, node->getPosInfo());
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

        curFunc()->curBlock()->createFree();
        curFunc()->curBlock()->createLeaveScope();
        curFunc()->fnScope().leave();
        
        return block;
    }

    IRValue* IRGenerator::visitIfStmtNode(NodePtr node) {
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int beforeBlockIndex = curFunc()->cur();

        // if.then
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "if.then");
        int thenExitBlockIndex = curFunc()->cur();
        //

        // if.else
        IRValue* elseBlock = nullptr;
        int elseExitBlockIndex = -1;

        if (node->hasNode(ASTTag::ElseStmtNode)) {
            auto block = (*(*node)[ASTTag::ElseStmtNode])[ASTTag::Block];
            elseBlock = visitBlockStmtNode(block, "if.else");
            elseExitBlockIndex = curFunc()->cur();
        }
        //

        // if.merge
        IRValue* mergeBlock = curFunc()->buildBlock("if.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        // before -> then or else?merge
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

        // while.prep
        IRValue* prepareBlock = curFunc()->buildBlock("while.prep");
        int prepareBlockIndex = curFunc()->cur();

        curFunc()
            ->block(beforeBlockIndex)
            ->createBr(prepareBlock);
        
        curFunc()->moveCursor(prepareBlockIndex);
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int prepareExitBlockIndex = curFunc()->cur();
        //

        // while.merge
        IRValue* mergeBlock = curFunc()->buildBlock("while.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(prepareBlock, mergeBlock);

        // while.then
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "while.then");
        int thenExitBlockIndex = curFunc()->cur();
        //

            
        // then -> prep
        curFunc()
            ->block(thenExitBlockIndex)
            ->createBr(prepareBlock);
        //
        
        // prep -> merge or then
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

        // for init (in beforeBlock)
        if (node->hasNode(ASTTag::DeclareStmtNode)) {
            visitDeclareStmtNode((*node)[ASTTag::DeclareStmtNode]);
        }
        int initExitIndex = curFunc()->cur();

        // for.cond
        IRValue* condBlock = curFunc()->buildBlock("for.cond");
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int condBlockExitIndex = curFunc()->cur();
        //

        // for.body
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "for.body");
        int thenBlockExitIndex = curFunc()->cur();
        //

        // for.step
        IRValue* stepBlock = curFunc()->buildBlock("for.step");
        visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        int stepBlockExitIndex = curFunc()->cur();
        //

        // for.merge
        IRValue* mergeBlock = curFunc()->buildBlock("for.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(condBlock, mergeBlock);

        // init -> cond
        curFunc()
            ->block(initExitIndex)
            ->createBr(condBlock);
        //

        // cond -> body or merge
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

    IRValue* IRGenerator::visitFuncDefineStmtNode(NodePtr node) {
        auto fnName = (*node)[ASTTag::Identifier]->getToken().content;
        IRType* retType = IRType::getVoidTy();
        FormalParamsDefine params;

        if (node->hasNode(ASTTag::Args)) {
            auto typeList = (*node)[ASTTag::Args]->getChildren()[0];
            auto nameList = (*node)[ASTTag::Args]->getChildren()[1];
            for (std::size_t i = 0; i < typeList->getChildren().size(); i ++) {
                auto tyInfo = getTypeInfoFromNode(typeList->getChildren()[i]);
                IRType* argType = tyInfo->toIRType();
                fzlib::String argName = nameList->getChildren()[i]->getToken().content;

                params.push_back(std::make_pair<fzlib::String, IRType*>(std::move(argName), std::move(argType)));
            }
        }
        
        fnName = mangleFnName(fnName, params);
        IRValue* fn = curModule()->buildFunction(fnName, retType, params, node->getPosInfo());
        long initBlockIndex = curFunc()->cur();

        if (node->hasNode(ASTTag::Args)) {
            for (auto arg: params) {
                createParam(arg.first, arg.second, node->getPosInfo());
            }
        }

        IRValue* typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);

        // Unboxing
        auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
        auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
        TypeInfo* typeInfo = typeInfoConstant->getContentValue<TypeInfo*>();

        retType = typeInfo->toIRType();

        curFunc()->setFuncDefineInfo(params, retType);

        visitBlockStmtNode((*node)[ASTTag::Block], "fn." + fnName, initBlockIndex);

        return fn;
    };

    IRValue* IRGenerator::visitReturnStmtNode(NodePtr node) {
        IRValue* retValue = visitWholeExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                    ->curBlock()
                    ->createReturn(retValue);
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
        else if (stmt->getTag() == ASTTag::ReturnStmtNode) {
            return visitReturnStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::BreakStmtNode) {
            return visitBreakStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ContinueStmtNode) {
            return visitContinueStmtNode(stmt);
        }
        
        throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Unknown Statement to generate",
                            node->getPosInfo());
    }
}
