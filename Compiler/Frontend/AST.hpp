#ifndef SAKURAE_AST_HPP
#define SAKURAE_AST_HPP

#include <iostream>
#include <map>

#include "parser_base.hpp"

#include "Compiler/Error/error.hpp"

#include "includes/magic_enum.hpp"
#include "includes/String.hpp"

namespace sakuraE {
    class Node;
    using TokenPtr = std::shared_ptr<Token>;
    enum class ASTTag {
        // Empty
        Empty, Token,
        // Expr Header
        LiteralNode, IndexOpNode, CallingOpNode, AtomIdentifierNode,
        IdentifierExprNode, PrimExprNode, MulExprNode, AddExprNode,
        LogicExprNode, BinaryExprNode, ArrayExprNode, WholeExprNode,
        BasicTypeModifierNode, ArrayTypeModifierNode, TypeModifierNode,
        AssignExprNode, RangeExprNode,
        // Stmt Header
        DeclareStmtNode, ExprStmtNode, IfStmtNode, ElseStmtNode,
        WhileStmtNode, ForStmtNode, BlockStmtNode, FuncDefineStmtNode,
        ReturnStmtNode, BreakStmtNode, ContinueStmtNode, Stmt,
        // Token
        Literal, Identifier, Symbol, Keyword,
        // Branches
        HeadExpr, Exprs, Op, Ops, PreOp,
        Types, Args, Type, AssignTerm,
        Condition, Block, Stmts
    };

    using NodePtr = std::shared_ptr<Node>;
    class Node {
        ASTTag tag;
        std::variant<std::monostate, TokenPtr> content;
        std::vector<std::pair<ASTTag, NodePtr>> children;
        PositionInfo createInfo;

        int hasSub(ASTTag t) {
            for (std::size_t i = 0; i < children.size(); i ++) {
                if (children.at(i).first == t) return i;
            }
            return -1;
        }
    public:
        Node(TokenPtr tok): tag(ASTTag::Token), content(tok) {}
        Node(TokenPtr tok, ASTTag t): tag(t), content(tok) {}
        Node(ASTTag t): tag(t) {}
        Node(): tag(ASTTag::Empty) {}

        bool isLeaf() {
            return std::holds_alternative<TokenPtr>(content);
        }

        bool hasNode(ASTTag t) {
            for (auto n: children) {
                if (n.first == t) return true;
            }
            return false;
        }

        ASTTag getTag() {
            return tag;
        }

        void setInfo(PositionInfo info) {
            createInfo = info;
        }

        void setToken(Token tok) {
            content = std::make_shared<Token>(tok);
        }

        Token getToken() {
            return *std::get<TokenPtr>(content);
        }

        const PositionInfo& getPosInfo() {
            return createInfo;
        }

        NodePtr& operator[] (ASTTag t) {
            if (hasSub(t) == -1) {
                children.push_back({t, std::make_shared<Node>(t)});
                return children.at(hasSub(t)).second;
            }
            else {
                return children.at(hasSub(t)).second;
            }
        }

        void addChild(NodePtr node) {
            if (!node) return;
            ASTTag childTag = node->getTag();
            children.push_back({childTag, node});
        }

        std::vector<NodePtr> getChildren() {
            std::vector<NodePtr> result;
            for (auto child: children) {
                result.push_back(child.second);
            }
            return result;
        }

        fzlib::String toString() {
            std::ostringstream oss;
            oss << "" << magic_enum::enum_name(tag) << ":";
            if (std::holds_alternative<TokenPtr>(content)) {
                oss << "(" << std::get<TokenPtr>(content)->content << ")";
            }
            if (!children.empty()) {
                oss << ":[";
            }
            for (std::size_t i = 0; i < children.size(); i ++) {
                if (i == children.size() - 1) {
                    oss << children.at(i).second->toString();
                } else {
                    oss << children.at(i).second->toString() << ", ";
                }
            }
            if (!children.empty()) {
                oss << "]";
            }
            return oss.str();
        }

        fzlib::String toFormatString(int depth = 0) {
            std::ostringstream oss;

            fzlib::String indent(depth * 2, ' ');
            fzlib::String child_indent((depth + 1) * 2, ' ');

            oss << magic_enum::enum_name(tag);
            if (std::holds_alternative<TokenPtr>(content)) {
                oss << "(" << std::get<TokenPtr>(content)->content << ")";
            }

            if (!children.empty()) {
                oss << ": [\n";
                for (std::size_t i = 0; i < children.size(); i++) {
                    oss << child_indent;

                    oss << children.at(i).second->toFormatString(depth + 1);

                    if (i < children.size() - 1) {
                        oss << ",";
                    }
                    oss << "\n";
                }
                oss << indent << "]";
            }

            return oss.str();
        }
    };

    class ResourceFetcher {
    public:
        virtual NodePtr genResource()=0;
    };
}

#endif
