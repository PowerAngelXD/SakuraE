#include "lexer.h"

sakoraE::Token::Token(TokenType t, const std::string& c, int l, int col, const std::string& det)
    : content(c), type(t) {
    info.line = l;
    info.column = col;
    info.details = det;
}

std::string sakoraE::Token::typeToString() const {
    return std::string(magic_enum::enum_name(type));
}

std::string sakoraE::Token::toString() const {
    return "<" + content + ", " + typeToString() + ">";
}

// --- Lexer Implementations (构造函数从 .h 移动，其余为原有实现) ---

sakoraE::Lexer::Lexer(const std::string& source) 
    : source_code(source), current_pos(0), current_line(1), current_column(1) {}


char sakoraE::Lexer::peek(int offset) const {
    if (current_pos + offset >= source_code.length()) {
        return '\0';
    }
    return source_code[current_pos + offset];
}

char sakoraE::Lexer::next() {
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

void sakoraE::Lexer::skip() {
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

bool sakoraE::Lexer::isKeyword(const std::string& content) const {
    return std::find(keywords.begin(), keywords.end(), content) != keywords.end();
}

bool sakoraE::Lexer::isTypeField(const std::string &content) const {
    return std::find(typeFields.begin(), typeFields.end(), content) != typeFields.end();
}

sakoraE::TokenType sakoraE::Lexer::str2KeywordType(std::string content) const {
    std::transform(content.begin(), content.end(), content.begin(), ::toupper);
    return magic_enum::enum_cast<TokenType>("KEYWORD_" + content).value();
}

sakoraE::TokenType sakoraE::Lexer::str2TypeField(std::string content) const {
    std::transform(content.begin(), content.end(), content.begin(), ::toupper);
    return magic_enum::enum_cast<TokenType>("TYPE_" + content).value();
}

sakoraE::Token sakoraE::Lexer::makeIdentifierOrKeyword() {
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
        if (content == "true" || content == "false") {
            type = TokenType::BOOL_CONST;
            details = content;
        }
        else {
            type = str2KeywordType(content);
            details = content;
        }
    }
    else if (isTypeField(content)) {
        type = str2TypeField(content);
        details = content;
    } 
    else {
        type = TokenType::IDENTIFIER;
        details = "identifier";
    }

    return Token(type, content, start_line, start_column, details);
}

sakoraE::Token sakoraE::Lexer::makeNumberLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;
    TokenType type = TokenType::INT_N;
    std::string details = "integer";
    bool has_decimal = false;

    while (std::isdigit(peek()) || (!has_decimal && peek() == '.' && std::isdigit(peek(1)))) {
        if (peek() == '.') {
            has_decimal = true;
            type = TokenType::FLOAT_N;
            details = "float";
        }
        content += next();
    }
    
    return Token(type, content, start_line, start_column, details);
}

sakoraE::Token sakoraE::Lexer::makeCharLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;
    TokenType type = TokenType::CHAR;
    next();

    if (peek(1) != '\'') {
        sutils::reportError(OccurredTerm::LEXER, "Char Literal must have only one character", PositionInfo{start_line, start_column, "Error"});
    }

    content = next();

    next();

    return Token(type, content, start_line, start_column);
}

sakoraE::Token sakoraE::Lexer::makeStringLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    TokenType type = TokenType::STRING;
    std::string details = "string";
    std::string content;
    content.pop_back();

    while (peek() != '\"' && peek() != '\0' && peek() != '\n') {
        content += next();
    }

    if (peek() == '\"') {
        next(); 
        content = "\"" + content + "\"";
    }
    else {
        sutils::reportError(OccurredTerm::LEXER, "Unclosed string literal", PositionInfo{start_line, start_column, "Error"});
    }

    return Token(type, content, start_line, start_column, details);
}

sakoraE::Token sakoraE::Lexer::makeSymbol() {
    int start_line = current_line;
    int start_column = current_column;
    std::string content;
    TokenType type = TokenType::UNKNOWN;
    std::string details;
    
    switch (peek()) {
        case '+':
            if (peek(1) == '=') {
                type = TokenType::ADD_ASSIGN;
                content = "+=";
                next(); next();
            }
            else {
                type = TokenType::ADD;
                content = "+";
                next();
            }
            break;
        case '-':
            if (peek(1) == '=') {
                type = TokenType::SUB_ASSIGN;
                content = "-=";
                next(); next();
            }
            else {
                type = TokenType::SUB;
                content = "-";
                next();
            }
            break;
        case '*':
            if (peek(1) == '=') {
                type = TokenType::MUL_ASSIGN;
                content = "*=";
                next(); next();
            }
            else {
                type = TokenType::MUL;
                content = "*";
                next();
            }
            break;
        case '/':
            if (peek(1) == '=') {
                type = TokenType::DIV_ASSIGN;
                content = "/=";
                next(); next();
            }
            else {
                type = TokenType::DIV;
                content = "/";
                next();
            }
            break;
        case '<':
            if (peek(1) == '=' && peek(2) == '>') {
                type = TokenType::SPACE_SHIP;
                content = "<=>";
                next(); next(); next();
            }
            else if (peek(1) == '=') {
                type = TokenType::LGC_LSEQU_THAN;
                content = "<=";
                next(); next();
            }
            else {
                type = TokenType::LGC_LS_THAN;
                content = "<";
                next();
            }
            break;
        case '>':
            if (peek(1) == '=') {
                type = TokenType::LGC_MREQU_THAN;
                content = ">=";
                next(); next();
            }
            else {
                type = TokenType::LGC_MR_THAN;
                content = ">";
                next();
            }
            break;
        case '=':
            if (peek(1) == '=') {
                type = TokenType::LGC_EQU;
                content = "==";
                next(); next();
            }
            else {
                type = TokenType::ASSIGN_OP;
                content = "=";
                next();
            }
            break;
        case '!':
            if (peek(1) == '=') {
                type = TokenType::LGC_NOT_EQU;
                content = "!=";
                next(); next();
            }
            else {
                type = TokenType::LGC_NOT;
                content = "!";
                next();
            }
            break;
        case '|':
            if (peek(1) == '|') {
                type = TokenType::LGC_OR;
                content = "||";
                next(); next();
            }
            else {
                type = TokenType::OR;
                content = "|";
                next();
            }
            break;
        case '&':
            if (peek(1) == '&') {
                type = TokenType::LGC_AND;
                content = "&&";
                next(); next();
            }
            else {
                type = TokenType::AND;
                content = "&";
                next();
            }
            break;
        case '[':
            type = TokenType::LEFT_SQUARE_BRACKET;
            content = "[";
            next();
            break;
        case ']':
            type = TokenType::RIGHT_SQUARE_BRACKET;
            content = "]";
            next();
            break;
        case '(':
            type = TokenType::LEFT_PAREN;
            content = "(";
            next();
            break;
        case ')':
            type = TokenType::RIGHT_PAREN;
            content = ")";
            next();
            break;
        case '{':
            type = TokenType::LEFT_BRACKET;
            content = "{";
            next();
            break;
        case '}':
            type = TokenType::RIGHT_BRACKET;
            content = "}";
            next();
            break;
        case ';':
            type = TokenType::STMT_END;
            content = ";";
            next();
            break;
        case ':':
            type = TokenType::CONSTRAINT_OP;
            content = ":";
            next();
            break;
        case ',':
            type = TokenType::COMMA;
            content = ",";
            next();
            break;
        case '.':
            type = TokenType::DOT;
            content = ".";
            next();
            break;
        case '%':
            type = TokenType::MOD;
            content = "%";
            next();
            break;
        default:
            break;
    }


    return Token(type, content, start_line, start_column, details);
}


std::vector<sakoraE::Token> sakoraE::Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (peek() != '\0') {
        skip(); 
        if (peek() == '\0') break; 
        char c = peek();
        Token token;
        if (std::isalpha(c) || c == '_') {
            token = makeIdentifierOrKeyword();
        } 
        else if (std::isdigit(c)) {
            token = makeNumberLiteral();
        } 
        else if (c == '\"') {
            token = makeStringLiteral();
        }
        else if (c == '\'') {
            token = makeCharLiteral();
        }
        else {
            token = makeSymbol();
        }
        
        tokens.push_back(token);
    }
    
    tokens.push_back(Token(TokenType::_EOF_, "", current_line, current_column, "End of File"));
    return tokens;
}