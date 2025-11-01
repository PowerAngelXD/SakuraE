#ifndef T_AST_H
#define T_AST_H

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include "lexer.h" // 确保包含 lexer.h 以使用 Token

// Forward declarations of core types
class ContainableStmtNode;
class WholeExprNode;
class IdentifierExprNode;
class TypeModifierNode;
class BinaryExprNode; 
class BlockStmtNode;
class StmtNode; // 顶级语句节点

// Forward declarations of all defined AST nodes
class LiteralNode;
class IndexOpNode;
class CallingOpNode;
class AtomIdentifierNode;
class IdentifierNode;
class PrimExprNode;
class ArrayExprNode;
class AssignExprNode;

class BasicTypeModifierNode;
class ArrayTypeModifierNode;

class LetStmtNode;
class ExprStmtNode;
class ReturnStmtNode;
class IfStmtNode;
class ElseStmtNode;
class WhileStmtNode;
class ForStmtNode;
class FuncDefineStmtNode;


/**
 * @brief Base class for all AST nodes.
 */
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

// --- Type Modifiers ---

class BasicTypeModifierNode : public ASTNode {
public:
    std::shared_ptr<Token> type_name; 
    BasicTypeModifierNode(std::shared_ptr<Token> name); 
    std::string toString() const override; 
};

class ArrayTypeModifierNode : public ASTNode {
public:
    std::shared_ptr<Token> number; 
    std::shared_ptr<BasicTypeModifierNode> basic_type; 
    ArrayTypeModifierNode(std::shared_ptr<Token> num, std::shared_ptr<BasicTypeModifierNode> type); 
    std::string toString() const override; 
};

class TypeModifierNode : public ASTNode {
public:
    std::shared_ptr<ASTNode> modifier; 
    TypeModifierNode(std::shared_ptr<ASTNode> mod); 
    std::string toString() const override; 
};

// --- Expressions ---

class LiteralNode : public ASTNode {
public:
    std::shared_ptr<Token> value_token; 
    LiteralNode(std::shared_ptr<Token> val_token);
    std::string toString() const override;
};

class IndexOpNode : public ASTNode {
public:
    std::shared_ptr<WholeExprNode> index_expr;
    IndexOpNode(std::shared_ptr<WholeExprNode> expr);
    std::string toString() const override;
};

class CallingOpNode : public ASTNode {
public:
    std::vector<std::shared_ptr<WholeExprNode>> arguments;
    CallingOpNode(std::vector<std::shared_ptr<WholeExprNode>> args);
    std::string toString() const override;
};

class AtomIdentifierNode : public ASTNode {
public:
    std::shared_ptr<Token> field_token; 
    std::shared_ptr<IndexOpNode> index_op = nullptr; 
    std::shared_ptr<CallingOpNode> calling_op = nullptr; 
    AtomIdentifierNode(std::shared_ptr<Token> field, std::shared_ptr<IndexOpNode> index = nullptr, std::shared_ptr<CallingOpNode> call = nullptr); 
    std::string toString() const override; 
};

class IdentifierNode : public ASTNode {
public:
    std::shared_ptr<AtomIdentifierNode> first_atom;
    std::vector<std::shared_ptr<AtomIdentifierNode>> chain; 
    IdentifierNode(std::shared_ptr<AtomIdentifierNode> atom, std::vector<std::shared_ptr<AtomIdentifierNode>> atoms = {}); 
    std::string toString() const override; 
};

class IdentifierExprNode : public ASTNode {
public:
    std::shared_ptr<IdentifierNode> identifier;
    IdentifierExprNode(std::shared_ptr<IdentifierNode> id); 
    std::string toString() const override; 
};

class PrimExprNode : public ASTNode {
public:
    std::shared_ptr<LiteralNode> literal = nullptr;
    std::shared_ptr<IdentifierExprNode> identifier_expr = nullptr;
    std::shared_ptr<ArrayExprNode> array_expr = nullptr;
    std::shared_ptr<WholeExprNode> grouped_expr = nullptr; 

    PrimExprNode(std::shared_ptr<LiteralNode> lit); 
    PrimExprNode(std::shared_ptr<IdentifierExprNode> id_expr);
    PrimExprNode(std::shared_ptr<ArrayExprNode> arr);
    PrimExprNode(std::shared_ptr<WholeExprNode> group);
    std::string toString() const override;
};

class BinaryExprNode : public ASTNode {
public:
    struct ChainLink {
        std::shared_ptr<Token> op_token; 
        std::shared_ptr<PrimExprNode> rhs;
        // 声明: toString 移到 .cpp
        std::string toString() const;
    };

    std::shared_ptr<PrimExprNode> lhs;
    std::vector<ChainLink> chain;
    BinaryExprNode(std::shared_ptr<PrimExprNode> left, std::vector<ChainLink> links = {});
    std::string toString() const override;
};

class ArrayExprNode : public ASTNode {
public:
    std::shared_ptr<TypeModifierNode> type_modifier;
    std::vector<std::shared_ptr<WholeExprNode>> elements;
    ArrayExprNode(std::shared_ptr<TypeModifierNode> type, std::vector<std::shared_ptr<WholeExprNode>> elems = {});
    std::string toString() const override;
};

class AssignExprNode : public ASTNode {
public:
    std::shared_ptr<IdentifierExprNode> target;
    std::shared_ptr<Token> assign_op; 
    std::shared_ptr<WholeExprNode> value;
    AssignExprNode(std::shared_ptr<IdentifierExprNode> t, std::shared_ptr<Token> op, std::shared_ptr<WholeExprNode> v);
    std::string toString() const override;
};

class WholeExprNode : public ASTNode {
public:
    std::shared_ptr<BinaryExprNode> bin_expr = nullptr;
    std::shared_ptr<PrimExprNode> prim_expr = nullptr;
    std::shared_ptr<AssignExprNode> assign_expr = nullptr;

    WholeExprNode(std::shared_ptr<ASTNode> expr);
    std::string toString() const override;
};

// --- Statements ---

class LetStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<Token> field; 
    std::shared_ptr<TypeModifierNode> type_modifier = nullptr; 
    std::shared_ptr<WholeExprNode> initial_value = nullptr; 
    std::shared_ptr<Token> semicolon;
    LetStmtNode(std::shared_ptr<Token> m, std::shared_ptr<Token> f, std::shared_ptr<TypeModifierNode> t, std::shared_ptr<WholeExprNode> v, std::shared_ptr<Token> sc);
    std::string toString() const override; 
};

class ExprStmtNode : public ASTNode {
public:
    std::shared_ptr<ASTNode> expression; 
    std::shared_ptr<Token> semicolon;
    ExprStmtNode(std::shared_ptr<ASTNode> expr, std::shared_ptr<Token> sc);
    std::string toString() const override; 
};

class ReturnStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<WholeExprNode> expression = nullptr; 
    std::shared_ptr<Token> semicolon;
    ReturnStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> e, std::shared_ptr<Token> sc);
    std::string toString() const override;
};

class BlockStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> left_brace; 
    std::vector<std::shared_ptr<ContainableStmtNode>> statements;
    std::shared_ptr<Token> right_brace; 
    BlockStmtNode(std::shared_ptr<Token> lb, std::vector<std::shared_ptr<ContainableStmtNode>> stmts, std::shared_ptr<Token> rb);
    std::string toString() const override;
};

class IfStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<WholeExprNode> condition;
    std::shared_ptr<ASTNode> true_stmt; 
    std::shared_ptr<ASTNode> else_stmt = nullptr; 
    IfStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<ASTNode> true_s, std::shared_ptr<ASTNode> else_s = nullptr);
    std::string toString() const override;
};

class ElseStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<ASTNode> false_stmt; 
    ElseStmtNode(std::shared_ptr<Token> m, std::shared_ptr<ASTNode> false_s);
    std::string toString() const override;
};

class WhileStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<WholeExprNode> condition;
    std::shared_ptr<ASTNode> body_stmt; 
    WhileStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<ASTNode> body);
    std::string toString() const override;
};

class ForStmtNode : public ASTNode {
public:
    std::shared_ptr<Token> mark; 
    std::shared_ptr<LetStmtNode> init_stmt;
    std::shared_ptr<WholeExprNode> condition; 
    std::shared_ptr<WholeExprNode> update_expr; 
    std::shared_ptr<ASTNode> body_stmt; 
    ForStmtNode(std::shared_ptr<Token> m, std::shared_ptr<LetStmtNode> init, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<WholeExprNode> update, std::shared_ptr<ASTNode> body);
    std::string toString() const override;
};

class FuncDefineStmtNode : public ASTNode {
public:
    struct Param {
        std::string field; 
        std::shared_ptr<TypeModifierNode> type;
        // 声明: toString 移到 .cpp
        std::string toString() const;
    };
    std::shared_ptr<Token> mark; 
    std::shared_ptr<Token> function_name_token; 
    std::vector<Param> parameters;
    std::shared_ptr<TypeModifierNode> return_type;
    std::shared_ptr<BlockStmtNode> body;
    FuncDefineStmtNode(std::shared_ptr<Token> m, std::shared_ptr<Token> name, std::vector<Param> params, std::shared_ptr<TypeModifierNode> ret_type, std::shared_ptr<BlockStmtNode> b);
    std::string toString() const override;
};

class ContainableStmtNode : public ASTNode {
public:
    // 可嵌套的语句类型
    std::shared_ptr<LetStmtNode> let_stmt = nullptr;
    std::shared_ptr<IfStmtNode> if_stmt = nullptr;
    std::shared_ptr<ElseStmtNode> else_stmt = nullptr;
    std::shared_ptr<WhileStmtNode> while_stmt = nullptr;
    std::shared_ptr<ForStmtNode> for_stmt = nullptr;
    std::shared_ptr<ExprStmtNode> expr_stmt = nullptr;
    std::shared_ptr<ReturnStmtNode> return_stmt = nullptr;
    std::shared_ptr<BlockStmtNode> block_stmt = nullptr;

    // 声明: 构造函数
    ContainableStmtNode(std::shared_ptr<LetStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<IfStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<ElseStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<WhileStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<ForStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<ExprStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<ReturnStmtNode> stmt);
    ContainableStmtNode(std::shared_ptr<BlockStmtNode> stmt);

    std::string toString() const override;
};


// --- StmtNode (顶级语句节点) ---

class StmtNode : public ASTNode {
public:
    std::shared_ptr<ContainableStmtNode> containable_stmt = nullptr;
    std::shared_ptr<FuncDefineStmtNode> fn_define_stmt = nullptr;

    // 声明: 构造函数
    StmtNode(std::shared_ptr<ContainableStmtNode> containable);
    StmtNode(std::shared_ptr<FuncDefineStmtNode> fn_define);
    
    std::string toString() const override;
};


#endif // T_AST_H