#include "parser.hpp"

sakuraE::NodePtr sakuraE::LiteralParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::LiteralNode);

    (*root)[ASTTag::Literal] = std::visit([&](auto& ptr) -> NodePtr {
        root->setInfo(ptr->token->info);
        std::shared_ptr<Token> tok = ptr->token;
        return std::make_shared<Node>(tok);
    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::IndexOpParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::IndexOpNode);

    root->setInfo(std::get<0>(getTuple())->token->info);
    (*root)[ASTTag::HeadExpr] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::CallingOpParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::CallingOpNode);

    root->setInfo(std::get<0>(getTuple())->token->info);
    if (std::get<1>(getTuple())->isEmpty()) return root;
    else {
        (*root)[ASTTag::Exprs]->addChild(std::get<1>(getTuple())->
                                        getClosure().at(0)->
                                        genResource());

        auto closure = std::get<2>(getTuple());
        if (closure->isEmpty()) return root;
        else {
            for (auto unit: closure->getClosure()) {
                (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
            }
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::AtomIdentifierExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::AtomIdentifierNode);

    root->setInfo(std::get<0>(getTuple())->token->info);
    (*root)[ASTTag::Identifier] = std::make_shared<Node>(std::get<0>(getTuple())->token);
    
    auto closure = std::get<1>(getTuple());
    if (closure->isEmpty()) return root; // No operator existing, return
    else {
        // Generating operators
        for (auto unit: closure->getClosure()) {
            auto op = std::visit([](auto& ptr) -> NodePtr {
                return ptr->genResource();
            }, unit->option());
            (*root)[ASTTag::Ops]->addChild(op);
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::IdentifierExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::IdentifierExprNode);
    auto not_op = std::get<0>(getTuple());
    
    // pre op
    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        
        if constexpr (!std::is_same_v<VarType, std::shared_ptr<NullParser>>) {
            std::shared_ptr<Token> tok = var->token;

            (*root)[ASTTag::PreOp] = std::make_shared<Node>(tok);
        }
    }, std::get<0>(getTuple())->option());

    (*root)[ASTTag::Exprs]->addChild(std::get<1>(getTuple())->genResource());
    root->setInfo((*root)[ASTTag::Exprs]->getChildren()[0]->getPosInfo());

    auto subs = std::get<2>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
        }
    }

    // after op
    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        
        if constexpr (!std::is_same_v<VarType, std::shared_ptr<NullParser>>) {
            if (root->hasNode(ASTTag::Op))
                throw SakuraError(OccurredTerm::PARSER,
                                "Operator appeared previously.",
                                root->getPosInfo());
            std::shared_ptr<Token> tok = var->token;

            (*root)[ASTTag::Op] = std::make_shared<Node>(tok);
        }
    }, std::get<3>(getTuple())->option());

    return root;
}

sakuraE::NodePtr sakuraE::PrimExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::PrimExprNode);
    
    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<LiteralParser>>) {
            (*root)[ASTTag::Literal] = var->genResource();
            root->setInfo((*root)[ASTTag::Literal]->getPosInfo());
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<IdentifierExprParser>>) {
            (*root)[ASTTag::Identifier] = var->genResource();
            root->setInfo((*root)[ASTTag::Identifier]->getPosInfo());
        }
        else {
            root->setInfo(std::get<0>(var->getTuple())->token->info);
            (*root)[ASTTag::HeadExpr] = std::get<1>(var->getTuple())->genResource();
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::MulExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::MulExprNode);

    auto first = std::get<0>(getTuple())->genResource();
    root->setInfo(first->getPosInfo());
    (*root)[ASTTag::Exprs]->addChild(first);

    auto subs = std::get<1>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
            (*root)[ASTTag::Ops]->addChild(std::visit([=](auto& var) -> NodePtr {
                std::shared_ptr<Token> tok = var->token;
                return std::make_shared<Node>(tok);
            },std::get<0>(unit->getTuple())->option()));
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::AddExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::AddExprNode);

    auto first = std::get<0>(getTuple())->genResource();
    root->setInfo(first->getPosInfo());
    (*root)[ASTTag::Exprs]->addChild(first);

    auto subs = std::get<1>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
            (*root)[ASTTag::Ops]->addChild(std::visit([=](auto& var) -> NodePtr {
                std::shared_ptr<Token> tok = var->token;
                return std::make_shared<Node>(tok);
            },std::get<0>(unit->getTuple())->option()));
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::LogicExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::LogicExprNode);

    auto first = std::get<0>(getTuple())->genResource();
    root->setInfo(first->getPosInfo());
    (*root)[ASTTag::Exprs]->addChild(first);

    auto subs = std::get<1>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
            (*root)[ASTTag::Ops]->addChild(std::visit([=](auto& var) -> NodePtr {
                std::shared_ptr<Token> tok = var->token;
                return std::make_shared<Node>(tok);
            },std::get<0>(unit->getTuple())->option()));
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::BinaryExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::BinaryExprNode);

    auto first = std::get<0>(getTuple())->genResource();
    root->setInfo(first->getPosInfo());
    (*root)[ASTTag::Exprs]->addChild(first);

    auto subs = std::get<1>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
            (*root)[ASTTag::Ops]->addChild(std::visit([=](auto& var) -> NodePtr {
                std::shared_ptr<Token> tok = var->token;
                return std::make_shared<Node>(tok);
            },std::get<0>(unit->getTuple())->option()));
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::ArrayExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ArrayExprNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    auto checker_expr = std::get<1>(getTuple());
    if (checker_expr->isMatch()) {
        for (auto unit: checker_expr->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(unit->genResource());
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::AssignExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::AssignExprNode);

    auto id = std::get<0>(getTuple())->genResource();
    root->setInfo(id->getPosInfo());

    (*root)[ASTTag::Identifier] = id;
    
    std::visit([&](auto& var) {
        (*root)[ASTTag::Op] = std::make_shared<Node>(var->token);
    }, std::get<1>(getTuple())->option());

    (*root)[ASTTag::HeadExpr] = std::get<2>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::WholeExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::WholeExprNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        auto res = var->genResource();
        root->setInfo(res->getPosInfo());

        if constexpr (std::is_same_v<VarType, std::shared_ptr<AddExprParser>>) {
            (*root)[ASTTag::AddExprNode] = res;
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<BinaryExprParser>>) {
            (*root)[ASTTag::BinaryExprNode] = res;
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<ArrayExprParser>>) {
            (*root)[ASTTag::ArrayExprNode] = res;
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<AssignExprParser>>) {
            (*root)[ASTTag::AssignExprNode] = res;
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::BasicTypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::BasicTypeModifierNode);

    std::visit([&](auto& ptr) {
        root->setInfo(ptr->token->info);
    }, option());

    (*root)[ASTTag::Keyword] = std::visit([=](auto& ptr) -> NodePtr {
        std::shared_ptr<Token> tok = ptr->token;
        return std::make_shared<Node>(tok);
    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::ArrayTypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ArrayTypeModifierNode);

    auto head = std::get<0>(getTuple())->genResource();
    root->setInfo(head->getPosInfo());

    for (auto dimension: std::get<1>(getTuple())->getClosure()) {
        (*root)[ASTTag::Exprs]->addChild(std::get<1>(dimension->getTuple())->genResource());
    }
    
    (*root)[ASTTag::HeadExpr] = head;

    return root;
}

sakuraE::NodePtr sakuraE::TypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::TypeModifierNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        auto res = var->genResource();
        root->setInfo(res->getPosInfo());

        if constexpr (std::is_same_v<VarType, std::shared_ptr<BasicTypeModifierParser>>) {
            (*root)[ASTTag::BasicTypeModifierNode] = res;
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<ArrayTypeModifierParser>>) {
            (*root)[ASTTag::ArrayTypeModifierNode] = res;
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::RangeExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::RangeExprNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<ArrayExprParser>>) {
            (*root)[ASTTag::ArrayExprNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<IdentifierExprParser>>) {
            (*root)[ASTTag::Identifier] = var->genResource();
        }

    }, std::get<1>(getTuple())->option());

    return root;
}

// Stmt

sakuraE::NodePtr sakuraE::DeclareStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::DeclareStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);
    bool hasTypeStriction = false;

    (*root)[ASTTag::Identifier] = std::make_shared<Node>(std::get<1>(getTuple())->token);
    // If type striction existed, generate it
    if (!std::get<2>(getTuple())->isEmpty()) {
        (*root)[ASTTag::Type] = std::get<1>(std::get<2>(getTuple())->getClosure().at(0)->getTuple())->genResource();
        hasTypeStriction = true;
    }

    if (std::get<3>(getTuple())->isEmpty() && !hasTypeStriction) {
        auto info = (*root)[ASTTag::Identifier]->getToken().info;
        sutils::reportError(OccurredTerm::PARSER, 
                            "A DeclareStatement must have an initialization declaration if no type constraint is specified.",
                            info);
    }
    else if (std::get<3>(getTuple())->isEmpty() && hasTypeStriction) {}
    else {
        (*root)[ASTTag::AssignTerm] = std::get<1>(std::get<3>(getTuple())->getClosure().at(0)->getTuple())->genResource();
    }

    return root;
}

sakuraE::NodePtr sakuraE::ExprStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ExprStmtNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        auto res = var->genResource();
        root->setInfo(res->getPosInfo());
        
        if constexpr (std::is_same_v<VarType, std::shared_ptr<IdentifierExprParser>>) {
            (*root)[ASTTag::IdentifierExprNode] = res;
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<AssignExprParser>>) {
            (*root)[ASTTag::AssignExprNode] = res;
        }
    }, std::get<0>(getTuple())->option());

    return root;
}

sakuraE::NodePtr sakuraE::IfStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::IfStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    (*root)[ASTTag::Condition] = std::get<2>(getTuple())->genResource();
    (*root)[ASTTag::Block] = std::get<4>(getTuple())->genResource();

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        
        if constexpr (std::is_same_v<VarType, std::shared_ptr<ElseStmtParser>>) {
            (*root)[ASTTag::ElseStmtNode] = var->genResource();
        }
    }, std::get<5>(getTuple())->option());

    return root;
}

sakuraE::NodePtr sakuraE::ElseStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ElseStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    (*root)[ASTTag::Block] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::WhileStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::WhileStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    (*root)[ASTTag::Condition] = std::get<2>(getTuple())->genResource();
    (*root)[ASTTag::Block] = std::get<4>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::ForStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ForStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<TraditionalConditionChain>>) {
            (*root)[ASTTag::DeclareStmtNode] = std::get<0>(var->getTuple())->genResource();
            (*root)[ASTTag::Condition] = std::get<1>(var->getTuple())->genResource();
            (*root)[ASTTag::HeadExpr] = std::get<3>(var->getTuple())->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<RangeConditionChain>>) {
            (*root)[ASTTag::Identifier] = std::make_shared<Node>(std::get<1>(var->getTuple())->token);
            if (!std::get<2>(var->getTuple())->isEmpty()) {
                (*root)[ASTTag::Type] = std::get<1>(std::get<2>(var->getTuple())->getClosure().at(0)->getTuple())->genResource();
            }
            (*root)[ASTTag::AssignTerm] = std::get<4>(var->getTuple())->genResource();
        }

    }, std::get<2>(getTuple())->option());

    (*root)[ASTTag::Block] = std::get<4>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::BlockStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::BlockStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    for (auto stmt: std::get<1>(getTuple())->getClosure()) {
        auto stmt_node = std::visit([](auto& ptr) -> NodePtr {
            return ptr->genResource();
        }, stmt->option());
        (*root)[ASTTag::Stmts]->addChild(stmt_node);
    }

    return root;
}

sakuraE::NodePtr sakuraE::FuncDefineStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::FuncDefineStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    (*root)[ASTTag::Identifier] = std::make_shared<Node>(std::get<1>(getTuple())->token);

    if (std::get<3>(getTuple())->isMatch()) {
        for (std::size_t i = 0; i < std::get<3>(getTuple())->getClosure().size(); i ++) {
            auto arg = std::get<3>(getTuple())->getClosure()[i];

            auto type = std::get<2>(arg->getTuple())->genResource();
            auto name = std::make_shared<Node>(std::get<0>(arg->getTuple())->token);

            (*(*root)[ASTTag::Args])[ASTTag::Type]->addChild(type);
            (*(*root)[ASTTag::Args])[ASTTag::Identifier]->addChild(name);
        }
    }

    (*root)[ASTTag::Type] = std::get<6>(getTuple())->genResource();
    (*root)[ASTTag::Block] = std::get<7>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::ReturnStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ReturnStmtNode);
    root->setInfo(std::get<0>(getTuple())->token->info);

    (*root)[ASTTag::HeadExpr] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::StatementParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::Stmt);

    std::visit([&](auto& var) {
        auto res = var->genResource();
        root->setInfo(res->getPosInfo());
        (*root)[ASTTag::Stmt] = res;
    }, option());

    return root;
}