#include "lexer.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "includes/magic_enum.hpp" // Assumed path

// --- Token Implementations (从 .h 移动) ---

Token::Token(TokenType t, const std::string& c, int l, int col, const std::string& det)
    : content(c), type(t) {
    info.line = l;
    info.column = col;
    info.details = det;
}

std::string Token::typeToString() const {
    return std::string(magic_enum::enum_name(type));
}

std::string Token::toString() const {
    return "<" + content + ", " + typeToString() + ">";
}

// --- Lexer Implementations (构造函数从 .h 移动，其余为原有实现) ---

Lexer::Lexer(const std::string& source) 
    : source_code(source), current_pos(0), current_line(1), current_column(1) {}


char Lexer::peek(int offset) const {
    if (current_pos + offset >= source_code.length()) {
        return '\0';
    }
    return source_code[current_pos + offset];
}

char Lexer::next() {
    char c = peek();
    if (c != '\0') {
        current_pos++;
        if (c == '\n') {
            current_line++;
            current_column = 1; 
        } else {
            current_column++;
        }
    }
    return c;
}

void Lexer::skip() {
    while (true) {
        char c = peek();
        
        if (std::isspace(c)) {
            next();
            continue;
        }

        if (c == '/' && peek(1) == '/') {
            next(); next();
            while (peek() != '\n' && peek() != '\0') {
                next();
            }
            continue; 
        }

        break; 
    }
}

bool Lexer::isKeyword(const std::string& content) const {
    // 假设 keywords 列表在 lexer.h 中
    return std::find(keywords.begin(), keywords.end(), content) != keywords.end();
}

Token Lexer::makeIdentifierOrKeyword() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;

    if (!std::isalpha(peek()) && peek() != '_') {
        // Should not happen if called correctly
        return Token(TokenType::UNKNOWN, "", start_line, start_column, "Expected identifier or keyword start.");
    }

    while (std::isalnum(peek()) || peek() == '_') {
        content += next();
    }

    TokenType type;
    std::string details;

    if (isKeyword(content)) {
        type = TokenType::KEYWORD;
        details = content;
    } else {
        type = TokenType::IDENTIFIER;
        details = "identifier";
    }

    return Token(type, content, start_line, start_column, details);
}

Token Lexer::makeNumberLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;
    TokenType type = TokenType::LITERAL;
    std::string details = "integer";
    bool has_decimal = false;

    while (std::isdigit(peek()) || (!has_decimal && peek() == '.' && std::isdigit(peek(1)))) {
        if (peek() == '.') {
            has_decimal = true;
            details = "float";
        }
        content += next();
    }
    
    return Token(type, content, start_line, start_column, details);
}

Token Lexer::makeSymbol() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;
    TokenType type = TokenType::SYMBOL;
    std::string details;

    content += next(); // Consume the first character

    // 检查双字符运算符
    char next_c = peek();
    if ((content == "=" && next_c == '=') ||
        (content == "!" && next_c == '=') ||
        (content == "<" && next_c == '=') ||
        (content == ">" && next_c == '=') ||
        (content == "|" && next_c == '|') ||
        (content == "&" && next_c == '&') ||
        (content == "-" && next_c == '>') ) 
    {
        content += next();
    } else if (content == "\"") {
        // 处理字符串字面量
        type = TokenType::LITERAL;
        details = "string";
        content.pop_back(); // 移除开头的双引号
        
        while (peek() != '\"' && peek() != '\0' && peek() != '\n') {
            content += next();
        }
        
        if (peek() == '\"') {
            next(); // 消耗结尾的双引号
            content = "\"" + content + "\""; // 重新包裹
        } else {
            // 报告未闭合的字符串错误
            type = TokenType::UNKNOWN;
            details = "Unclosed string literal";
            return Token(type, content, start_line, start_column, details);
        }
    }
    
    // 检查单字符符号（假设其他未匹配的都是单字符符号）
    switch (content[0]) {
        case '+':
        case '-':
        case '<':
        case '>':
        case '=':
        case '!':
        case '|':
        case '&':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case ';':
        case ':':
        case ',':
        case '.':
            break;
        case '*':
        case '/':
            // 如果不是双字符（如 /**/ 注释已在 skip() 中处理），则为符号
            if (content.length() == 1) break; 
            [[fallthrough]];
        default:
            if (content.length() == 1) {
                type = TokenType::UNKNOWN;
                details = "Unrecognized character: " + content;
            } else {
                // 如果是双字符符号，类型保持 SYMBOL
            }
            break;
    }
    
    if (type != TokenType::UNKNOWN && type != TokenType::LITERAL) {
        details = std::string(magic_enum::enum_name(type));
    }


    return Token(type, content, start_line, start_column, details);
}


std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (peek() != '\0') {
        skip(); 
        if (peek() == '\0') break; 
        char c = peek();
        Token token;
        if (std::isalpha(c) || c == '_') {
            token = makeIdentifierOrKeyword();
        } else if (std::isdigit(c)) {
            token = makeNumberLiteral();
        } else {
            token = makeSymbol();
        }
        
        tokens.push_back(token);
    }
    
    tokens.push_back(Token(TokenType::__EOF, "", current_line, current_column, "End of File"));
    return tokens;
}