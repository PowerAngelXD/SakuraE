#ifndef T_LEXER_H
#define T_LEXER_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "includes/magic_enum.hpp" // Assumed path

enum class TokenType {
    IDENTIFIER,
    LITERAL,
    KEYWORD,
    SYMBOL,
    __EOF,
    UNKNOWN
};

struct DebugInfo {
    int line;     
    int column;   
    std::string details;
};

class Token {
public:
    DebugInfo info;      // DebugInfo
    std::string content; // Raw content of the token
    TokenType type;      // Type of the token

    // 声明: 构造函数
    Token(TokenType t = TokenType::UNKNOWN, 
          const std::string& c = "", 
          int l = 0, 
          int col = 0, 
          const std::string& det = "");

    // 声明: toString 实现
    std::string typeToString() const;
    std::string toString() const;
};

class Lexer {
public:
    // 声明: 构造函数
    Lexer(const std::string& source);
    std::vector<Token> tokenize();

private:
    std::string source_code;        
    size_t current_pos;             
    int current_line;               
    int current_column;             
    
    const std::vector<std::string> keywords = {
        "if", "else", "while", "for", "func", "int", "return", "let", "const", "range"
    };

    char peek(int offset = 0) const; 
    char next(); 
    void skip();
    bool isKeyword(const std::string& content) const;
    Token makeIdentifierOrKeyword(); 
    Token makeNumberLiteral(); 
    Token makeSymbol(); 
};

#endif // T_LEXER_H