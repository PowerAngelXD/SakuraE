#include "ast.h"
#include <utility>

// --- Type Modifiers ---

BasicTypeModifierNode::BasicTypeModifierNode(std::shared_ptr<Token> name) 
    : type_name(std::move(name)) {}
std::string BasicTypeModifierNode::toString() const { 
    return type_name->content; 
}

ArrayTypeModifierNode::ArrayTypeModifierNode(std::shared_ptr<Token> num, std::shared_ptr<BasicTypeModifierNode> type)
    : number(std::move(num)), basic_type(std::move(type)) {}
std::string ArrayTypeModifierNode::toString() const { 
    return "[" + number->content + "]" + basic_type->toString(); 
}

TypeModifierNode::TypeModifierNode(std::shared_ptr<ASTNode> mod) 
    : modifier(std::move(mod)) {}
std::string TypeModifierNode::toString() const { 
    return modifier->toString(); 
}

// --- Expressions ---

LiteralNode::LiteralNode(std::shared_ptr<Token> val_token) 
    : value_token(std::move(val_token)) {}
std::string LiteralNode::toString() const { 
    return value_token->content; 
}

IndexOpNode::IndexOpNode(std::shared_ptr<WholeExprNode> expr) 
    : index_expr(std::move(expr)) {}
std::string IndexOpNode::toString() const { 
    return "[" + index_expr->toString() + "]"; 
}

CallingOpNode::CallingOpNode(std::vector<std::shared_ptr<WholeExprNode>> args) 
    : arguments(std::move(args)) {}
std::string CallingOpNode::toString() const {
    std::string s = "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        s += arguments[i]->toString();
        if (i < arguments.size() - 1) s += ", ";
    }
    s += ")";
    return s;
}

AtomIdentifierNode::AtomIdentifierNode(std::shared_ptr<Token> field, std::shared_ptr<IndexOpNode> index, std::shared_ptr<CallingOpNode> call)
    : field_token(std::move(field)), index_op(std::move(index)), calling_op(std::move(call)) {}
std::string AtomIdentifierNode::toString() const {
    std::string s = field_token->content;
    if (index_op) {
        s += index_op->toString();
    }
    if (calling_op) {
        s += calling_op->toString();
    }
    return s;
}

IdentifierNode::IdentifierNode(std::shared_ptr<AtomIdentifierNode> atom, std::vector<std::shared_ptr<AtomIdentifierNode>> atoms)
    : first_atom(std::move(atom)), chain(std::move(atoms)) {}
std::string IdentifierNode::toString() const {
    std::string s = first_atom->toString();
    for (const auto& atom : chain) {
        s += "." + atom->toString();
    }
    return s;
}

IdentifierExprNode::IdentifierExprNode(std::shared_ptr<IdentifierNode> id)
    : identifier(std::move(id)) {}
std::string IdentifierExprNode::toString() const {
    return identifier->toString();
}

PrimExprNode::PrimExprNode(std::shared_ptr<LiteralNode> lit) 
    : literal(std::move(lit)) {}
PrimExprNode::PrimExprNode(std::shared_ptr<IdentifierExprNode> id_expr) 
    : identifier_expr(std::move(id_expr)) {}
PrimExprNode::PrimExprNode(std::shared_ptr<WholeExprNode> group) 
    : grouped_expr(std::move(group)) {}
PrimExprNode::PrimExprNode(std::shared_ptr<ArrayExprNode> arr)
    : array_expr(std::move(arr)) {}

std::string PrimExprNode::toString() const {
    if (literal) {
        return "PrimExprNode: {" + literal->toString() + "}";
    }
    else if (identifier_expr) {
        return "PrimExprNode: {" + identifier_expr->toString() + "}";
    }
    else if (array_expr) {
        return "PrimExprNode: {" + array_expr->toString() + "}";
    }
    else if (grouped_expr) {
        return "PrimExprNode: {" + grouped_expr->toString() + "}";
    }
    return "PrimExprNode(Empty)";
}

// BinaryExprNode Link toString Implementation (从 .h 移动)
std::string BinaryExprNode::ChainLink::toString() const {
    return op_token->content + rhs->toString();
}

BinaryExprNode::BinaryExprNode(std::shared_ptr<PrimExprNode> left, std::vector<ChainLink> links)
    : lhs(std::move(left)), chain(std::move(links)) {}

std::string BinaryExprNode::toString() const {
    std::string s = lhs->toString();
    for (const auto& link : chain) {
        s += " " + link.toString();
    }
    return "BinaryExprNode: {" + s + "}";
}

ArrayExprNode::ArrayExprNode(std::shared_ptr<TypeModifierNode> type, std::vector<std::shared_ptr<WholeExprNode>> elems)
    : type_modifier(std::move(type)), elements(std::move(elems)) {}
std::string ArrayExprNode::toString() const {
    std::string s = type_modifier->toString() + " {";
    for (size_t i = 0; i < elements.size(); ++i) {
        s += elements[i]->toString();
        if (i < elements.size() - 1) s += ", ";
    }
    s += "}";
    return s;
}

AssignExprNode::AssignExprNode(std::shared_ptr<IdentifierExprNode> t, std::shared_ptr<Token> op, std::shared_ptr<WholeExprNode> v)
    : target(std::move(t)), assign_op(std::move(op)), value(std::move(v)) {}
std::string AssignExprNode::toString() const {
    return target->toString() + " " + assign_op->content + " " + value->toString();
}

WholeExprNode::WholeExprNode(std::shared_ptr<ASTNode> expr) {
    if (std::dynamic_pointer_cast<BinaryExprNode>(expr)) {
        bin_expr = std::static_pointer_cast<BinaryExprNode>(expr);
    }
    else if (std::dynamic_pointer_cast<PrimExprNode>(expr)) {
        prim_expr = std::static_pointer_cast<PrimExprNode>(expr);
    }
    else if (std::dynamic_pointer_cast<AssignExprNode>(expr)) {
        assign_expr = std::static_pointer_cast<AssignExprNode>(expr);
    }
}
std::string WholeExprNode::toString() const {
    if (bin_expr)
        return "WholeExprNode: {" + bin_expr->toString() + "}";
    else if (prim_expr)
        return "WholeExprNode: {" + prim_expr->toString() + "}";
    else if (assign_expr)
        return "WholeExprNode: {" + assign_expr->toString() + "}";
    else
        return "WholeExprNode: {Empty}";
}


// --- Statements ---

LetStmtNode::LetStmtNode(std::shared_ptr<Token> m, std::shared_ptr<Token> f, std::shared_ptr<TypeModifierNode> t, std::shared_ptr<WholeExprNode> v, std::shared_ptr<Token> sc)
    : mark(std::move(m)), field(std::move(f)), type_modifier(std::move(t)), initial_value(std::move(v)), semicolon(std::move(sc)) {}
std::string LetStmtNode::toString() const { 
    std::string s = mark->content + " " + field->content;
    if (type_modifier) {
        s += ": " + type_modifier->toString();
    }
    if (initial_value) {
        s += " = " + initial_value->toString();
    }
    s += semicolon->content;
    return s;
}

ExprStmtNode::ExprStmtNode(std::shared_ptr<ASTNode> expr, std::shared_ptr<Token> sc)
    : expression(std::move(expr)), semicolon(std::move(sc)) {}
std::string ExprStmtNode::toString() const { 
    return expression->toString() + semicolon->content; 
}

ReturnStmtNode::ReturnStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> e, std::shared_ptr<Token> sc)
    : mark(std::move(m)), expression(std::move(e)), semicolon(std::move(sc)) {}
std::string ReturnStmtNode::toString() const {
    std::string s = mark->content;
    if (expression) {
        s += " " + expression->toString();
    }
    s += semicolon->content;
    return s;
}

BlockStmtNode::BlockStmtNode(std::shared_ptr<Token> lb, std::vector<std::shared_ptr<ContainableStmtNode>> stmts, std::shared_ptr<Token> rb)
    : left_brace(std::move(lb)), statements(std::move(stmts)), right_brace(std::move(rb)) {}
std::string BlockStmtNode::toString() const {
    std::string s = left_brace->content + "\n";
    for (const auto& stmt : statements) {
        s += "  " + stmt->toString() + "\n";
    }
    s += right_brace->content;
    return s;
}

IfStmtNode::IfStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<ASTNode> true_s, std::shared_ptr<ASTNode> else_s)
    : mark(std::move(m)), condition(std::move(cond)), true_stmt(std::move(true_s)), else_stmt(std::move(else_s)) {}
std::string IfStmtNode::toString() const {
    std::string s = mark->content + " (" + condition->toString() + ") " + true_stmt->toString();
    if (else_stmt) {
        s += " " + else_stmt->toString();
    }
    return s;
}

ElseStmtNode::ElseStmtNode(std::shared_ptr<Token> m, std::shared_ptr<ASTNode> false_s)
    : mark(std::move(m)), false_stmt(std::move(false_s)) {}
std::string ElseStmtNode::toString() const {
    return mark->content + " " + false_stmt->toString();
}

WhileStmtNode::WhileStmtNode(std::shared_ptr<Token> m, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<ASTNode> body)
    : mark(std::move(m)), condition(std::move(cond)), body_stmt(std::move(body)) {}
std::string WhileStmtNode::toString() const {
    return "while (" + condition->toString() + ") " + body_stmt->toString();
}

ForStmtNode::ForStmtNode(std::shared_ptr<Token> m, std::shared_ptr<LetStmtNode> init, std::shared_ptr<WholeExprNode> cond, std::shared_ptr<WholeExprNode> update, std::shared_ptr<ASTNode> body)
    : mark(std::move(m)), init_stmt(std::move(init)), condition(std::move(cond)), update_expr(std::move(update)), body_stmt(std::move(body)) {}
std::string ForStmtNode::toString() const {
    std::string init_str = init_stmt->toString();
    // Remove trailing ';' for the for loop init part.
    if (!init_str.empty() && init_str.back() == ';') {
        init_str.pop_back(); 
    }
    
    std::string update_str = update_expr ? update_expr->toString() : "";
    
    return "for (" + init_str + "; " + condition->toString() + "; " + update_str + ") " + body_stmt->toString();
}

// FuncDefineStmtNode Param toString Implementation (从 .h 移动)
std::string FuncDefineStmtNode::Param::toString() const {
    return field + ": " + type->toString();
}

FuncDefineStmtNode::FuncDefineStmtNode(std::shared_ptr<Token> m, std::shared_ptr<Token> name, std::vector<Param> params, std::shared_ptr<TypeModifierNode> ret_type, std::shared_ptr<BlockStmtNode> b)
    : mark(std::move(m)), function_name_token(std::move(name)), parameters(std::move(params)), return_type(std::move(ret_type)), body(std::move(b)) {}
std::string FuncDefineStmtNode::toString() const {
    std::string s = mark->content + " " + function_name_token->content + "(";
    for (size_t i = 0; i < parameters.size(); ++i) {
        s += parameters[i].toString();
        if (i < parameters.size() - 1) s += ", ";
    }
    s += ") -> " + return_type->toString() + " " + body->toString();
    return s;
}


// --- ContainableStmtNode Implementations ---

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<LetStmtNode> stmt)
    : let_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<IfStmtNode> stmt)
    : if_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<ElseStmtNode> stmt)
    : else_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<WhileStmtNode> stmt)
    : while_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<ForStmtNode> stmt)
    : for_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<ExprStmtNode> stmt)
    : expr_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<ReturnStmtNode> stmt)
    : return_stmt(std::move(stmt)) {}

ContainableStmtNode::ContainableStmtNode(std::shared_ptr<BlockStmtNode> stmt)
    : block_stmt(std::move(stmt)) {}

std::string ContainableStmtNode::toString() const {
    if (let_stmt) {
        return let_stmt->toString();
    }
    if (if_stmt) {
        return if_stmt->toString();
    }
    if (else_stmt) {
        return else_stmt->toString();
    }
    if (while_stmt) {
        return while_stmt->toString();
    }
    if (for_stmt) {
        return for_stmt->toString();
    }
    if (expr_stmt) {
        return expr_stmt->toString();
    }
    if (return_stmt) {
        return return_stmt->toString();
    }
    if (block_stmt) {
        return block_stmt->toString();
    }
    return "ContainableStmtNode (Empty)";
}


// --- StmtNode Implementations (新增) ---

StmtNode::StmtNode(std::shared_ptr<ContainableStmtNode> containable) 
    : containable_stmt(std::move(containable)) {}

StmtNode::StmtNode(std::shared_ptr<FuncDefineStmtNode> fn_define) 
    : fn_define_stmt(std::move(fn_define)) {}

std::string StmtNode::toString() const {
    if (containable_stmt) {
        return containable_stmt->toString();
    }
    if (fn_define_stmt) {
        return fn_define_stmt->toString();
    }
    return "StmtNode (Empty)";
}