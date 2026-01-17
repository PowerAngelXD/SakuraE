#include "generator.hpp"

namespace sakuraE::IR {
    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto literal = Constant::getFromToken((*node)[ASTTag::Literal]->getToken());

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::constant, literal->getType(), {literal}, "constant");
    }

    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        IRValue* indexResult = visitAddExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::indexing, indexResult->getType(), {addr, indexResult}, "indexing");
    }

    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node) {
        std::vector<IRValue*> params = {addr};

        for (auto argExpr: (*node)[ASTTag::Exprs]->getChildren()) {
            params.push_back(visitWholeExprNode(argExpr));
        }

        return curFunc()
                ->curBlock()
                ->createInstruction(OpKind::call, IRType::getInt32Ty(), {params}, "call");
    }

    IRValue* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        IRValue* result = loadSymbol((*node)[ASTTag::Identifier]->getToken().content);

        if (node->hasNode(ASTTag::Ops)) {
            for (auto op: (*node)[ASTTag::Ops]->getChildren()) {
                switch (op->getTag()) {
                    case ASTTag::IndexOpNode: {
                        result = visitIndexOpNode(result, op);
                        break;
                    }
                    case ASTTag::CallingOpNode: {
                        result = visitCallingOpNode(result, op);
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
                            result = visitCallingOpNode(result, op);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

        return result;
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
                            ->createInstruction(OpKind::mul, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mul-tmp");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::div, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "div-tmp");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::mod, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "mod-tmp");
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
                                ->createInstruction(OpKind::add, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "add-tmp");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::sub, handleUnlogicalBinaryCalc(lhs, rhs), {lhs, rhs}, "sub-tmp");
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
                                ->createInstruction(OpKind::lgc_ls_than, IRType::getBoolTy(), {lhs, rhs}, "lgc-ls-than-tmp");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_ls_than, IRType::getBoolTy(), {lhs, rhs}, "lgc-eq-ls-than-tmp");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_mr_than, IRType::getBoolTy(), {lhs, rhs}, "lgc-mr-than-tmp");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_eq_mr_than, IRType::getBoolTy(), {lhs, rhs}, "lgc-eq-mr-than-tmp");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_equal, IRType::getBoolTy(), {lhs, rhs}, "lgc-equal-tmp");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_not_equal, IRType::getBoolTy(), {lhs, rhs}, "lgc-not-equal-tmp");
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

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitLogicExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_AND: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_and, IRType::getBoolTy(), {lhs, rhs}, "lgc-and-tmp");
                        break;
                    }
                    case TokenType::LGC_OR: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::lgc_or, IRType::getBoolTy(), {lhs, rhs}, "lgc-or-tmp");
                        break;
                    }
                    default: 
                        break;
                }
            }
        }
        return lhs;
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
        IRValue* expr = visitWholeExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::assign,
                                        expr->getType(),
                                        {symbol, expr},
                                        "assign-" + symbol->getName());
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
        IRType* type = nullptr;

        if (node->hasNode(ASTTag::Type)) {
            IRValue* typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);

            // Unboxing
            auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
            auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
            TypeInfo* typeInfo = typeInfoConstant->getIRValue<TypeInfo*>();

            type = typeInfo->toIRType();
        }

        IRValue* initVal = visitWholeExprNode((*node)[ASTTag::AssignTerm]);

        if (!type) {
            type = initVal->getType();
        }

        return declareSymbol(identifier.content, type, initVal);
    }

    IRValue* IRGenerator::visitExprStmtNode(NodePtr node) {
        if (node->hasNode(ASTTag::IdentifierExprNode)) {
            return visitIdentifierExprNode((*node)[ASTTag::IdentifierExprNode]);
        }
        else
            return visitAssignExprNode((*node)[ASTTag::AssignExprNode]);
    }

    IRValue* IRGenerator::visitBlockStmtNode(NodePtr node, fzlib::String blockName) {
        IRValue* block = curFunc()->buildBlock(blockName);
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
            elseBlock = visitBlockStmtNode((*(*(*node)[ASTTag::ElseStmtNode])[ASTTag::ElseStmtNode])[ASTTag::Block], "if.else");
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
            ->createInstruction(OpKind::cond_br,
                                IRType::getVoidTy(),
                                {cond, thenBlock, (elseBlock?elseBlock:mergeBlock)},
                                "cond-br.if.then.else?merge");
        //

        // then -> merge
        curFunc()
            ->block(thenExitBlockIndex)
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {mergeBlock},
                                "br.if.merge");
        //
        
        // else -> merge
        if (elseBlock) {
            curFunc()
            ->block(elseExitBlockIndex)
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {mergeBlock},
                                "br.if.merge");
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
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {prepareBlock},
                                "br.while.before.prep");
        
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
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {prepareBlock},
                                "br.while.prep");
        //
        
        // prep -> merge or then
        curFunc()
            ->block(prepareExitBlockIndex)
            ->createInstruction(OpKind::cond_br,
                                IRType::getVoidTy(),
                                {cond, thenBlock, mergeBlock},
                                "cond-br.while.then.merge");
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
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {condBlock},
                                "br.for.cond");
        //

        // cond -> body or merge
        curFunc()
            ->block(condBlockExitIndex)
            ->createInstruction(OpKind::cond_br,
                                IRType::getVoidTy(),
                                {cond, thenBlock, mergeBlock},
                                "cond-br.for.then.merge");
        //

        // body -> step
        curFunc()
            ->block(thenBlockExitIndex)
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {stepBlock},
                                "br.for.step");
        //

        // step -> cond
        curFunc()
            ->block(stepBlockExitIndex)
            ->createInstruction(OpKind::br,
                                IRType::getVoidTy(),
                                {condBlock},
                                "br.for.cond");
        //

        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    IRValue* IRGenerator::visitFuncDefineStmtNode(NodePtr node) {
        auto fnName = (*node)[ASTTag::Identifier]->getToken().content;
        FormalParamsDefine params;

        if (node->hasNode(ASTTag::Args)) {
            for (auto arg: (*node)[ASTTag::Args]->getChildren()) {
                IRValue* typeInfoIRValue = visitTypeModifierNode((*arg)[ASTTag::Type]);

                // Unboxing
                auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
                auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
                TypeInfo* typeInfo = typeInfoConstant->getIRValue<TypeInfo*>();

                IRType* argType = typeInfo->toIRType();

                fzlib::String argName = (*arg)[ASTTag::Identifier]->getToken().content;

                params.push_back(std::make_pair<fzlib::String, IRType*>(std::move(argName), std::move(argType)));
            }
        }

        IRValue* typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);

        // Unboxing
        auto constInst = dynamic_cast<Instruction*>(typeInfoIRValue);
        auto typeInfoConstant = dynamic_cast<Constant*>(constInst->getOperands()[0]);
        TypeInfo* typeInfo = typeInfoConstant->getIRValue<TypeInfo*>();

        IRType* retType = typeInfo->toIRType();

        IRValue* fn = curModule()->buildFunction(fnName, retType, params, node->getPosInfo());

        visitBlockStmtNode((*node)[ASTTag::Block], "fn." + fnName);

        return fn;
    };

    IRValue* IRGenerator::visitReturnStmtNode(NodePtr node) {
        IRValue* retValue = visitWholeExprNode((*node)[ASTTag::HeadExpr]);

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::ret,
                                        retValue->getType(),
                                        {retValue},
                                        "ret");
    }

    IRValue* IRGenerator::visitStmt(NodePtr node) {
        NodePtr stmt = (*node)[ASTTag::Stmt];
        
        if (stmt->hasNode(ASTTag::DeclareStmtNode)) {
            return visitDeclareStmtNode((*stmt)[ASTTag::DeclareStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::ExprStmtNode)) {
            return visitExprStmtNode((*stmt)[ASTTag::ExprStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::IfStmtNode)) {
            return visitIfStmtNode((*stmt)[ASTTag::IfStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::WhileStmtNode)) {
            return visitWhileStmtNode((*stmt)[ASTTag::WhileStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::ForStmtNode)) {
            return visitForStmtNode((*stmt)[ASTTag::ForStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::BlockStmtNode)) {
            return visitBlockStmtNode((*stmt)[ASTTag::BlockStmtNode], "block");
        }
        else if (stmt->hasNode(ASTTag::FuncDefineStmtNode)) {
            return visitFuncDefineStmtNode((*stmt)[ASTTag::FuncDefineStmtNode]);
        }
        else if (stmt->hasNode(ASTTag::ReturnStmtNode)) {
            return visitReturnStmtNode((*stmt)[ASTTag::ReturnStmtNode]);
        }
        
        throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Unknown Statement to generate",
                            node->getPosInfo());
    }
}