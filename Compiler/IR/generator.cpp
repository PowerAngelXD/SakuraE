#include "generator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/type/type.hpp"

namespace sakuraE::IR {
    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto literal = Constant::getFromToken((*node)[ASTTag::Literal]->getToken());

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::constant, literal->getType(), {literal}, "constant");
    }

    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        IRValue* indexResult = visitAddExprNode((*node)[ASTTag::HeadExpr]);
        auto elementType = dynamic_cast<IRArrayType*>(addr->getType())->getElementType();

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::indexing, elementType, {addr, indexResult}, "indexing");
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, fzlib::String target, NodePtr node) {
        std::vector<IRValue*> params = {addr};

        for (auto argExpr: (*node)[ASTTag::Exprs]->getChildren()) {
            params.push_back(visitWholeExprNode(argExpr));
        }

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::call, IRType::getInt32Ty(), params, "call." + target);
    }

    IRValue* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        auto targetFn = (*node)[ASTTag::Identifier]->getToken().content;
        IRValue* result = loadSymbol(targetFn);

        if (node->hasNode(ASTTag::Ops)) {
            for (auto op: (*node)[ASTTag::Ops]->getChildren()) {
                switch (op->getTag()) {
                    case ASTTag::IndexOpNode: {
                        result = visitIndexOpNode(result, op);
                        break;
                    }
                    case ASTTag::CallingOpNode: {
                        result = visitCallingOpNode(result, targetFn, op);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return result;
    }

    IRValue* IRGenerator::visitIdentifierExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* result = visitAtomIdentifierNode(chain[0]);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            fzlib::String memberName = (*chain[i])[ASTTag::Identifier]->getToken().content;
            result = curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::gmem, result->getType(), {result, Constant::get(memberName)}, "gmem");
            
            if (chain[i]->hasNode(ASTTag::Ops)) {
                for (auto op: (*chain[i])[ASTTag::Ops]->getChildren()) {
                    switch (op->getTag()) {
                        case ASTTag::IndexOpNode: {
                            result = visitIndexOpNode(result, op);
                            break;
                        }
                        case ASTTag::CallingOpNode: {
                            result = visitCallingOpNode(result, memberName, op);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

        if (node->hasNode(ASTTag::PreOp)) {
            auto preOp = (*node)[ASTTag::PreOp]->getToken();
            switch (preOp.type)
            {
                case TokenType::LGC_NOT:
                    return curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_not,
                                                    IRType::getBoolTy(),
                                                    {result},
                                                    "lgc-not");
                case TokenType::AINC: {
                    curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::add,
                                            handleUnlogicalBinaryCalc(result, Constant::get(1)),
                                            {result, Constant::get(1)},
                                            "add");
                    return result;
                }

                case TokenType::SDEC: {
                    curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::sub,
                                            handleUnlogicalBinaryCalc(result, Constant::get(1)),
                                            {result, Constant::get(1)},
                                            "sub");
                    return result;
                }
                                
                default:
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown operator.",
                                node->getPosInfo());
            }
        }
        else if (node->hasNode(ASTTag::Op)) {
            auto Op = (*node)[ASTTag::Op]->getToken();
            switch (Op.type)
            {
                case TokenType::AINC: {
                    return curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::add,
                                                handleUnlogicalBinaryCalc(result, Constant::get(1)),
                                                {result, Constant::get(1)},
                                                "add");
                }

                case TokenType::SDEC: {
                    return curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::sub,
                                                    handleUnlogicalBinaryCalc(result, Constant::get(1)),
                                                    {result, Constant::get(1)},
                                                    "sub");
                }
                                
                default:
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown operator.",
                                node->getPosInfo());
            }
        }
        else return result;
    }

    IRValue* IRGenerator::visitPrimExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::Literal)) {
            return visitLiteralNode((*node)[ASTTag::Literal]);
        }
        else if (node->hasNode(ASTTag::Identifier)) {
            return visitIdentifierExprNode((*node)[ASTTag::Identifier]);
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

        if (!chain[0]->hasNode(ASTTag::Ops)) {
            return lhs;
        }

        static int binaryID = 0;
        fzlib::String resultAddrName = "tbv." + std::to_string(binaryID);
        binaryID ++;
        
        IRValue* resultAddr = declareSymbol(resultAddrName, IRType::getBoolTy(), lhs, node->getPosInfo());

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
                            ->createCondBr(rhs, rhsBlock, mergeBlock);

                        storeSymbol(resultAddr, rhs, opChain[i - 1]->getToken().info);

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
                            ->createCondBr(rhs, mergeBlock, rhsBlock);

                        storeSymbol(resultAddr, rhs, opChain[i - 1]->getToken().info);
                            
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
        return loadSymbol(resultAddrName);
    }

    IRValue* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<IRValue*> array;
        
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        for (auto expr: chain) {
            array.push_back(visitWholeExprNode(expr));
        }

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::create_array,
                                        IRType::getArrayTy(array[0]->getType(), array.size()),
                                        array,
                                        "create-array");
    }

    IRValue* IRGenerator::visitAssignExprNode(NodePtr node) {
        IRValue* symbol = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        auto assignOp = (*node)[ASTTag::Op]->getToken().type;
        IRValue* expr = visitWholeExprNode((*node)[ASTTag::HeadExpr]);

        switch (assignOp)
        {
            case TokenType::ASSIGN_OP:
                return storeSymbol(symbol, expr, (*node)[ASTTag::Op]->getToken().info);
                
            case TokenType::ADD_ASSIGN: {
                IRValue* result = curFunc()
                                        ->curBlock()
                                        ->createInstruction(OpKind::add,
                                                            handleUnlogicalBinaryCalc(symbol, expr),
                                                            {symbol, expr},
                                                            "add");
                return storeSymbol(symbol, result, (*node)[ASTTag::Op]->getToken().info);
            }
            case TokenType::SUB_ASSIGN: {
                IRValue* result = curFunc()
                                        ->curBlock()
                                        ->createInstruction(OpKind::sub,
                                                            handleUnlogicalBinaryCalc(symbol, expr),
                                                            {symbol, expr},
                                                            "sub");
                return storeSymbol(symbol, result, (*node)[ASTTag::Op]->getToken().info);
            }
            case TokenType::MUL_ASSIGN: {
                IRValue* result = curFunc()
                                        ->curBlock()
                                        ->createInstruction(OpKind::mul,
                                                            handleUnlogicalBinaryCalc(symbol, expr),
                                                            {symbol, expr},
                                                            "mul");
                return storeSymbol(symbol, result, (*node)[ASTTag::Op]->getToken().info);
            }
            case TokenType::DIV_ASSIGN: {
                IRValue* result = curFunc()
                                        ->curBlock()
                                        ->createInstruction(OpKind::div,
                                                            handleUnlogicalBinaryCalc(symbol, expr),
                                                            {symbol, expr},
                                                            "div");
                return storeSymbol(symbol, result, (*node)[ASTTag::Op]->getToken().info);
            }
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown assign operator.",
                                node->getPosInfo());
        }
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

    IRValue* IRGenerator::visitBasicTypeModifierNode(NodePtr node) {
        switch ((*node)[ASTTag::Keyword]->getToken().type) {
            case TokenType::TYPE_INT:
                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::constant,
                                                IRType::getTypeInfoTy(),
                                                {Constant::get(TypeInfo::makeTypeID(TypeID::Int32))},
                                                "constant");
            case TokenType::TYPE_FLOAT:
                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::constant,
                                                IRType::getTypeInfoTy(),
                                                {Constant::get(TypeInfo::makeTypeID(TypeID::Float))},
                                                "constant");     
            case TokenType::TYPE_STRING:
                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::constant,
                                                IRType::getTypeInfoTy(),
                                                {Constant::get(TypeInfo::makeTypeID(TypeID::String))},
                                                "constant");
            case TokenType::TYPE_BOOL:
                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::constant,
                                                IRType::getTypeInfoTy(),
                                                {Constant::get(TypeInfo::makeTypeID(TypeID::Bool))},
                                                "constant");
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Unknown TypeID",
                                    node->getPosInfo());
        }
    }

    IRValue* IRGenerator::visitArrayTypeModifierNode(NodePtr node) {
        std::vector<IRValue*> result;
        auto headType = visitBasicTypeModifierNode((*node)[ASTTag::HeadExpr]);
        result.push_back(headType);

        auto dimensions = (*node)[ASTTag::Exprs]->getChildren();

        for (auto addexpr: dimensions) {
            result.push_back(visitAddExprNode(addexpr));
        }

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::constant,
                                        IRType::getTypeInfoTy(),
                                        result,
                                        "constant");
    }

    IRValue* IRGenerator::visitTypeModifierNode(NodePtr node) {
        if (node->hasNode(ASTTag::BasicTypeModifierNode)) {
            return visitBasicTypeModifierNode((*node)[ASTTag::BasicTypeModifierNode]);
        }
        else 
            return visitArrayTypeModifierNode((*node)[ASTTag::ArrayTypeModifierNode]);
    }

    // Statements

    IRValue* IRGenerator::visitDeclareStmtNode(NodePtr node) {
        auto identifier = (*node)[ASTTag::Identifier]->getToken();
        IRValue* typeInfoIRValue = nullptr;

        if (node->hasNode(ASTTag::Type)) {
            typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);
        }

        IRValue* initVal = visitWholeExprNode((*node)[ASTTag::AssignTerm]);
        
        if (typeInfoIRValue)
            return declareSymbol(identifier.content, typeInfoIRValue, initVal, node->getPosInfo());
        else
            return declareSymbol(identifier.content, initVal->getType(), initVal, node->getPosInfo());
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
        
        curFunc()->moveCursor(curFunc()->cur());

        for (auto stmt: (*node)[ASTTag::Stmts]->getChildren()) {
            visitStmt(stmt);
        }

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

        // while.then
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "while.then");
        int thenExitBlockIndex = curFunc()->cur();
        //

        // while.merge
        IRValue* mergeBlock = curFunc()->buildBlock("while.merge");
        int mergeBlockIndex = curFunc()->cur();
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

        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    IRValue* IRGenerator::visitFuncDefineStmtNode(NodePtr node) {
        auto fnName = (*node)[ASTTag::Identifier]->getToken().content;
        IRType* retType = IRType::getVoidTy();
        FormalParamsDefine params;

        IRValue* fn = curModule()->buildFunction(fnName, retType, params, node->getPosInfo());
        long initBlockIndex = curFunc()->cur();

        if (node->hasNode(ASTTag::Args)) {
            auto typeList = (*node)[ASTTag::Args]->getChildren()[0];
            auto nameList = (*node)[ASTTag::Args]->getChildren()[1];
            for (std::size_t i = 0; i < typeList->getChildren().size(); i ++) {
                IRValue* typeInfoIRValue = visitTypeModifierNode(typeList->getChildren()[i]);

                // Unboxing
                auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
                auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
                TypeInfo* typeInfo = typeInfoConstant->getContentValue<TypeInfo*>();

                IRType* argType = typeInfo->toIRType();

                fzlib::String argName = nameList->getChildren()[i]->getToken().content;

                params.push_back(std::make_pair<fzlib::String, IRType*>(std::move(argName), std::move(argType)));
                declareSymbol(argName, argType, nullptr, nameList->getChildren()[i]->getPosInfo());
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
        
        throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Unknown Statement to generate",
                            node->getPosInfo());
    }
}