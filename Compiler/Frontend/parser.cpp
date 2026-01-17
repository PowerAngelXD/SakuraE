#include "parser.hpp"

sakuraE::NodePtr sakuraE::LiteralParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::LiteralNode);

    (*root)[ASTTag::Literal] = std::visit([](auto& ptr) -> NodePtr {
        std::shared_ptr<Token> tok = ptr->token;
        return std::make_shared<Node>(tok);
    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::IndexOpParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::IndexOpNode);

    (*root)[ASTTag::HeadExpr] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::CallingOpParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::CallingOpNode);

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
    
    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        
        if constexpr (std::is_same_v<VarType, std::shared_ptr<TokenParser<TokenType::LGC_NOT>>>) {
            std::shared_ptr<Token> tok = var->token;

            (*root)[ASTTag::Op] = std::make_shared<Node>(tok);
        }
    }, std::get<0>(getTuple())->option());

    (*root)[ASTTag::Exprs]->addChild(std::get<1>(getTuple())->genResource());
    
    auto subs = std::get<2>(getTuple());
    if (!subs->isEmpty()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(std::get<1>(unit->getTuple())->genResource());
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::PrimExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::PrimExprNode);
    
    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<LiteralParser>>) {
            (*root)[ASTTag::Literal] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<IdentifierExprParser>>) {
            (*root)[ASTTag::Identifier] = var->genResource();
        }
        else {
            (*root)[ASTTag::HeadExpr] = std::get<1>(var->getTuple())->genResource();
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::MulExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::MulExprNode);

    (*root)[ASTTag::Exprs]->addChild(std::get<0>(getTuple())->genResource());

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

    (*root)[ASTTag::Exprs]->addChild(std::get<0>(getTuple())->genResource());

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

    (*root)[ASTTag::Exprs]->addChild(std::get<0>(getTuple())->genResource());

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

    (*root)[ASTTag::Exprs]->addChild(std::get<0>(getTuple())->genResource());

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

    auto checker_expr = std::get<1>(getTuple());
    if (checker_expr->isMatch()) {
        (*root)[ASTTag::Exprs]->addChild(checker_expr->getClosure().at(0)->genResource());
    }

    auto subs = std::get<1>(getTuple());
    std::cout << subs->isMatch() << std::endl;
    if (subs->isMatch()) {
        for (auto unit: subs->getClosure()) {
            (*root)[ASTTag::Exprs]->addChild(unit->genResource());
        }
    }

    return root;
}

sakuraE::NodePtr sakuraE::AssignExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::AssignExprNode);

    (*root)[ASTTag::Identifier] = std::get<0>(getTuple())->genResource();
    (*root)[ASTTag::Op] = std::make_shared<Node>(std::get<1>(getTuple())->token);
    (*root)[ASTTag::HeadExpr]->addChild(std::get<2>(getTuple())->genResource());

    return root;
}

sakuraE::NodePtr sakuraE::WholeExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::WholeExprNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<AddExprParser>>) {
            (*root)[ASTTag::AddExprNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<BinaryExprParser>>) {
            (*root)[ASTTag::BinaryExprNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<ArrayExprParser>>) {
            (*root)[ASTTag::ArrayExprNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<AssignExprParser>>) {
            (*root)[ASTTag::AssignExprNode] = var->genResource();
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::BasicTypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::BasicTypeModifierNode);

    (*root)[ASTTag::Keyword] = std::visit([=](auto& ptr) -> NodePtr {
        std::shared_ptr<Token> tok = ptr->token;
        return std::make_shared<Node>(tok);
    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::ArrayTypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ArrayTypeModifierNode);

    for (auto dimension: std::get<1>(getTuple())->getClosure()) {
        (*root)[ASTTag::Exprs]->addChild(std::get<1>(dimension->getTuple())->genResource());
    }
    
    (*root)[ASTTag::HeadExpr] = std::get<0>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::TypeModifierParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::TypeModifierNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;

        if constexpr (std::is_same_v<VarType, std::shared_ptr<BasicTypeModifierParser>>) {
            (*root)[ASTTag::BasicTypeModifierNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<ArrayTypeModifierParser>>) {
            (*root)[ASTTag::ArrayTypeModifierNode] = var->genResource();
        }

    }, option());

    return root;
}

sakuraE::NodePtr sakuraE::RangeExprParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::RangeExprNode);

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
    else {
        (*root)[ASTTag::AssignTerm] = std::get<1>(std::get<3>(getTuple())->getClosure().at(0)->getTuple())->genResource();
    }

    return root;
}

sakuraE::NodePtr sakuraE::ExprStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ExprStmtNode);

    std::visit([&](auto& var) {
        using VarType = std::decay_t<decltype(var)>;
        
        if constexpr (std::is_same_v<VarType, std::shared_ptr<IdentifierExprParser>>) {
            (*root)[ASTTag::IdentifierExprNode] = var->genResource();
        }
        else if constexpr (std::is_same_v<VarType, std::shared_ptr<AssignExprParser>>) {
            (*root)[ASTTag::AssignExprNode] = var->genResource();
        }
    }, std::get<0>(getTuple())->option());

    return root;
}

sakuraE::NodePtr sakuraE::IfStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::IfStmtNode);

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

    (*root)[ASTTag::Block] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::WhileStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::WhileStmtNode);

    (*root)[ASTTag::Condition] = std::get<2>(getTuple())->genResource();
    (*root)[ASTTag::Block] = std::get<4>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::ForStmtParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::ForStmtNode);

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

    (*root)[ASTTag::Identifier] = std::make_shared<Node>(std::get<1>(getTuple())->token);

    if (!std::get<3>(getTuple())->isMatch()) {
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

    (*root)[ASTTag::HeadExpr] = std::get<1>(getTuple())->genResource();

    return root;
}

sakuraE::NodePtr sakuraE::StatementParser::genResource() {
    NodePtr root = std::make_shared<Node>(ASTTag::Stmt);

    std::visit([&](auto& var) {
        (*root)[ASTTag::Stmt] = var->genResource();
    }, option());

    return root;
}