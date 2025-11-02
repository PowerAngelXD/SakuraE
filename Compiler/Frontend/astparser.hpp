#ifndef SAKORAE_ASTPARSER_HPP
#define SAKORAE_ASTPARSER_HPP
#include <memory>

#include "astbase.hpp"

namespace sakoraE {
    class NodeFormatter {
    public:
        virtual const std::string& toString() {}
    };

    // Forward declare
    class AddExprNode: NodeFormatter;
    //

    // <Literal>
    class LiteralNode: NodeFormatter, 
    OptionsNode<
        BasicNode<TokenType::INT_N>,
        BasicNode<TokenType::FLOAT_N>,
        BasicNode<TokenType::STRING>,
        BasicNode<TokenType::BOOL_CONST>,
        BasicNode<TokenType::CHAR>
    > 
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsNode::check(begin, end);
        }

        static Result<LiteralNode> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsNode::parse(begin, end);
            return Result<LiteralNode>(result.status, 
                                        std::static_pointer_cast<std::shared_ptr<LiteralNode>>(result.val), 
                                        result.end);
        }

        const std::string& toString() override {
            return "LiteralNode: {" + std::get<this->index()>(this->child()).token->toString() + "}";
        }
    };

    // IndexOp -> '[' AddExpr ']'
    class IndexOpNode: NodeFormatter,
    ConnectionNode<
        DiscardNode<TokenType::LEFT_SQUARE_BRACKET>,
        AddExprNode,
        DiscardNode<TokenType::RIGHT_SQUARE_BRACKET>
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionNode::check(begin, end);
        }

        static Result<IndexOpNode> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionNode::parse(begin, end);
            return Result<IndexOpNode>(result.status, 
                                        std::static_pointer_cast<std::shared_ptr<IndexOpNode>>(result.val), 
                                        result.end);
        }

        const std::string& toString() override {
            return "IndexOpNode: {" + std::get<1>(this->getChildren())->toString() + "}";
        }
    };

    
}

#endif