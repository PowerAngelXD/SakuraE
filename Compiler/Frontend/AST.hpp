#ifndef SAKORAE_AST_HPP
#define SAKORAE_AST_HPP

#include <iostream>

#include "parser_base.hpp"

namespace sakoraE {
    class NodeFormatter {
        virtual std::string toString()=0;
    };

    class IToken: NodeFormatter {};
    class IOperator: NodeFormatter {};
    class IExpr: NodeFormatter {};
    class IStatement: NodeFormatter {};

    using TokenPtr = std::shared_ptr<Token>;

    struct AddExprNode;

    struct LiteralNode: public IToken {
        TokenPtr literal = nullptr;

        std::string toString() override;
    };

    struct IndexOpNode: public IOperator {
        std::shared_ptr<AddExprNode> index = nullptr;

        std::string toString() override;
    };
}

#endif