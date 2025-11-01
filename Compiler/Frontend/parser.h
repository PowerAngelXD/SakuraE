#ifndef T_PARSER_H
#define T_PARSER_H

#include "lexer.h"
#include "ast.h"
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>

class Parser {
public:
    // 声明: 构造函数 (从 .h 移动)
    Parser(const std::string& source);
    
    // 主解析函数保持声明
    std::vector<std::shared_ptr<StmtNode>> parseProgram();

private:
    Lexer lexer;
    std::vector<Token> tokens;
    std::size_t cursor;

    // --- Utility functions for token management ---
    Token peek() const;
    Token next();
    std::shared_ptr<Token> eat(TokenType type, const std::string& error_msg);
    std::shared_ptr<Token> eat(const std::string& content, const std::string& error_msg);
    void reportError(const std::string& message) const;
    
    // --- Predicate functions (isXXXNode) ---
    bool isLiteralNode() const;
    bool isIdentifierExprStart() const;
    bool isPrimExprStart() const;
    bool isAssignExprStart();
    bool isArrayExprStart() const;
    // 声明: 表达式起始判断 (从 .h 移动)
    bool isWholeExprStart() const; 
    
    // Statement Predicates (声明)
    bool isLetStmtStart() const;
    bool isExprStmtStart() const;
    bool isReturnStmtStart() const;
    bool isBlockStmtStart() const;
    bool isIfStmtStart() const;
    bool isElseStmtStart() const;
    bool isWhileStmtStart() const;
    bool isForStmtStart() const;
    bool isFuncDefineStmtStart() const;
    
    // TDOP helper
    int getPrecedence(const std::string& op) const;
    bool isBinaryOp() const;
    
    // --- Expression Parsing ---
    std::shared_ptr<LiteralNode> parseLiteralNode();
    std::shared_ptr<IndexOpNode> parseIndexOpNode();
    std::shared_ptr<CallingOpNode> parseCallingOpNode();
    std::shared_ptr<AtomIdentifierNode> parseAtomIdentifierNode();
    std::shared_ptr<IdentifierNode> parseIdentifierNode();
    std::shared_ptr<IdentifierExprNode> parseIdentifierExprNode();
    std::shared_ptr<PrimExprNode> parsePrimExprNode();
    
    // BinaryExpression Parser
    std::shared_ptr<ASTNode> parseBinaryExprNode(std::shared_ptr<PrimExprNode> lhs, int min_precedence); 

    std::shared_ptr<ArrayExprNode> parseArrayExprNode();
    std::shared_ptr<AssignExprNode> parseAssignExprNode(std::shared_ptr<IdentifierExprNode> target); 
    
    std::shared_ptr<WholeExprNode> parseWholeExprNode();

    // --- Type Modifier Parsing ---
    std::shared_ptr<BasicTypeModifierNode> parseBasicTypeModifierNode();
    std::shared_ptr<ArrayTypeModifierNode> parseArrayTypeModifierNode();
    std::shared_ptr<TypeModifierNode> parseTypeModifierNode();
    
    // --- Statement Parsing ---
    std::shared_ptr<ContainableStmtNode> parseContainableStmtNode(); 
    std::shared_ptr<StmtNode> parseStmt(); 
    
    std::shared_ptr<LetStmtNode> parseLetStmtNode();
    std::shared_ptr<ExprStmtNode> parseExprStmtNode();
    std::shared_ptr<ReturnStmtNode> parseReturnStmtNode();
    std::shared_ptr<BlockStmtNode> parseBlockStmtNode();
    std::shared_ptr<IfStmtNode> parseIfStmtNode();
    std::shared_ptr<ElseStmtNode> parseElseStmtNode();
    std::shared_ptr<WhileStmtNode> parseWhileStmtNode();
    std::shared_ptr<ForStmtNode> parseForStmtNode();
    std::shared_ptr<FuncDefineStmtNode> parseFuncDefineStmtNode();
};

#endif // T_PARSER_H