#include "lexer.h"
#include "Compiler/Error/error.hpp"
#include <cctype>

sakuraE::Token::Token(TokenType t, const fzlib::String& c, int l, int col, const fzlib::String& det)
    : content(c), type(t) {
    info.line = l;
    info.column = col;
    info.details = det;
}

fzlib::String sakuraE::tokenTypeToString(TokenType type) {
    switch (type) {
    case TokenType::IDENTIFIER: return "identifier";
    case TokenType::KEYWORD: return "keyword";
    case TokenType::INT_N: return "integer literal";
    case TokenType::FLOAT_N: return "floating-point literal";
    case TokenType::STRING: return "string literal";
    case TokenType::CHAR: return "character literal";
    case TokenType::BOOL_CONST: return "boolean literal";
    case TokenType::ADD: return "'+'";
    case TokenType::SUB: return "'-'";
    case TokenType::MUL: return "'*'";
    case TokenType::DIV: return "'/'";
    case TokenType::MOD: return "'%'";
    case TokenType::OR: return "'|'";
    case TokenType::AND: return "'&'";
    case TokenType::LGC_NOT: return "'!'";
    case TokenType::LGC_NOT_EQU: return "'!='";
    case TokenType::LGC_AND: return "'&&'";
    case TokenType::LGC_OR: return "'||'";
    case TokenType::LGC_EQU: return "'=='";
    case TokenType::LGC_MR_THAN: return "'>'";
    case TokenType::LGC_LS_THAN: return "'<'";
    case TokenType::LGC_MREQU_THAN: return "'>='";
    case TokenType::LGC_LSEQU_THAN: return "'<='";
    case TokenType::LEFT_PAREN: return "'('";
    case TokenType::RIGHT_PAREN: return "')'";
    case TokenType::LEFT_SQUARE_BRACKET: return "'['";
    case TokenType::RIGHT_SQUARE_BRACKET: return "']'";
    case TokenType::LEFT_BRACKET: return "'{'";
    case TokenType::RIGHT_BRACKET: return "'}'";
    case TokenType::ASSIGN_OP: return "'='";
    case TokenType::ADD_ASSIGN: return "'+='";
    case TokenType::SUB_ASSIGN: return "'-='";
    case TokenType::MUL_ASSIGN: return "'*='";
    case TokenType::DIV_ASSIGN: return "'/='";
    case TokenType::AUTO_INC:
    case TokenType::AINC: return "'++'";
    case TokenType::AUTO_DEC:
    case TokenType::SDEC: return "'--'";
    case TokenType::CONSTRAINT_OP: return "':'";
    case TokenType::ARROW: return "'->'";
    case TokenType::BIG_ARROW: return "'=>'";
    case TokenType::SPACE_SHIP: return "'<=>'";
    case TokenType::DOT: return "'.'";
    case TokenType::COMMA: return "','";
    case TokenType::STMT_END: return "';'";
    case TokenType::FN_OP: return "'|>'";
    case TokenType::KEYWORD_LET: return "'let'";
    case TokenType::KEYWORD_IF: return "'if'";
    case TokenType::KEYWORD_ELSE: return "'else'";
    case TokenType::KEYWORD_WHILE: return "'while'";
    case TokenType::KEYWORD_FOR: return "'for'";
    case TokenType::KEYWORD_FUNC: return "'func'";
    case TokenType::KEYWORD_RETURN: return "'return'";
    case TokenType::KEYWORD_CONST: return "'const'";
    case TokenType::KEYWORD_RANGE: return "'range'";
    case TokenType::KEYWORD_BREAK: return "'break'";
    case TokenType::KEYWORD_CONTINUE: return "'continue'";
    case TokenType::KEYWORD_REF: return "'ref'";
    case TokenType::KEYWORD_STRUCT: return "'struct'";
    case TokenType::KEYWORD_IMPL: return "'impl'";
    case TokenType::KEYWORD_REPEAT: return "'repeat'";
    case TokenType::KEYWORD_MATCH: return "'match'";
    case TokenType::KEYWORD_DEFAULT: return "'default'";
    case TokenType::KEYWORD_TYPEOF: return "'typeof'";
    case TokenType::KEYWORD_SIZEOF: return "'sizeof'";
    case TokenType::TYPE_I32: return "'i32'";
    case TokenType::TYPE_I64: return "'i64'";
    case TokenType::TYPE_F32: return "'f32'";
    case TokenType::TYPE_F64: return "'f64'";
    case TokenType::TYPE_CHAR: return "'char'";
    case TokenType::TYPE_BOOL: return "'bool'";
    case TokenType::TYPE_STRING: return "'string'";
    case TokenType::TYPE_UI32: return "'ui32'";
    case TokenType::TYPE_UI64: return "'ui64'";
    case TokenType::TYPE_VOID: return "'void'";
    case TokenType::_EOF_: return "EOF";
    case TokenType::UNKNOWN: return "unknown token";
    }
    return "unknown token";
}

fzlib::String sakuraE::Token::typeToString() const {
    return tokenTypeToString(type);
}

fzlib::String sakuraE::Token::toString() const {
    return "<" + content + ", " + typeToString() + ">";
}

sakuraE::Lexer::Lexer(const fzlib::String& source)
    : source_code(source), current_pos(0), current_line(1), current_column(1) {}


char sakuraE::Lexer::peek(int offset) const {
    if (current_pos + offset >= source_code.len()) {
        return '\0';
    }
    return source_code[current_pos + offset];
}

char sakuraE::Lexer::next() {
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

void sakuraE::Lexer::skip() {
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

bool sakuraE::Lexer::isKeyword(const fzlib::String& content) const {
    return std::find(keywords.begin(), keywords.end(), content) != keywords.end();
}

bool sakuraE::Lexer::isTypeField(const fzlib::String &content) const {
    return std::find(typeFields.begin(), typeFields.end(), content) != typeFields.end();
}

sakuraE::TokenType sakuraE::Lexer::str2KeywordType(fzlib::String content) const {
    std::transform(content.begin(), content.end(), content.begin(), ::toupper);
    return magic_enum::enum_cast<TokenType>(("KEYWORD_" + content).c_str()).value();
}

sakuraE::TokenType sakuraE::Lexer::str2TypeField(fzlib::String content) const {
    std::transform(content.begin(), content.end(), content.begin(), ::toupper);
    return magic_enum::enum_cast<TokenType>(("TYPE_" + content).c_str()).value();
}

sakuraE::Token sakuraE::Lexer::makeIdentifierOrKeyword() {
    int start_line = current_line;
    int start_column = current_column;
    fzlib::String content;

    if (!std::isalpha(peek()) && peek() != '_') {
        // 如果调用方式正确，此处不应发生
        return Token(TokenType::UNKNOWN, "", start_line, start_column, "Expected identifier or keyword start.");
    }

    while (std::isalnum(peek()) || peek() == '_') {
        content += next();
    }

    TokenType type;
    fzlib::String details;

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

sakuraE::Token sakuraE::Lexer::makeNumberLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    fzlib::String content;
    TokenType type = TokenType::INT_N;
    fzlib::String details = "integer";
    bool has_decimal = false;

    content += next();

    while (std::isdigit(peek()) || (!has_decimal && peek() == '.' && std::isdigit(peek(1)))) {
        if (peek() == '.') {
            has_decimal = true;
            type = TokenType::FLOAT_N;
            details = "float";
        }
        content += next();
    }

    fzlib::String suffix;

    while (std::isalpha(peek())) {
        suffix += next();
    }

    if (suffix == "U" || suffix == "UL" || suffix == "L" || suffix == "f") {
        if (suffix == "f") type = TokenType::FLOAT_N;
        content += suffix;
    }
    else if (!suffix.isEmpty()) {
        throw SakuraError(OccurredTerm::LEXER,
                        "Unknown suffix of number literal: " + suffix,
                        {current_line, current_column, "lexer error"});
    }


    return Token(type, content, start_line, start_column, details);
}

sakuraE::Token sakuraE::Lexer::makeNonDecimalLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    TokenType type = TokenType::INT_N;
    fzlib::String content;

    content += next(); content += next();
    char prefix = content[1];
    while (std::isdigit(peek()) || std::isalpha(peek())) {
        if (prefix == 'b' && (peek() != '0' && peek() != '1'))
            throw SakuraError(OccurredTerm::LEXER,
                            "Binary literals can only contain '0' and '1'.",
                            {current_line, current_column, "Lexer error"});
        else if (prefix == 'o' && (peek() < '0' || peek() > '7'))
            throw SakuraError(OccurredTerm::LEXER,
                            "Octal literals must consist of digits from 0 to 7.",
                            {current_line, current_column, "Lexer error"});
        else if (prefix == 'x' && !std::isxdigit(peek()))
            throw SakuraError(OccurredTerm::LEXER,
                            "Hexadecimal literals must consist of 0-9, a-f, or A-F.",
                            {current_line, current_column, "Lexer error"});

        content += next();
    }

    return Token(type, content, start_line, start_column);
}

sakuraE::Token sakuraE::Lexer::makeCharLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    fzlib::String content;
    TokenType type = TokenType::CHAR;
    next();

    if (peek(1) != '\'') {
        sutils::reportError(OccurredTerm::LEXER, "Char Literal must have only one character", PositionInfo{start_line, start_column, "Error"});
    }

    content = next();

    next();

    return Token(type, content, start_line, start_column);
}

sakuraE::Token sakuraE::Lexer::makeStringLiteral() {
    int start_line = current_line;
    int start_column = current_column;
    TokenType type = TokenType::STRING;
    fzlib::String details = "string";
    fzlib::String content;

    next();

    while (peek() != '\"' && peek() != '\0' && peek() != '\n') {
        content += next();
    }

    if (peek() == '\"') {
        next();
    }
    else {
        sutils::reportError(OccurredTerm::LEXER, "Unclosed string literal", PositionInfo{start_line, start_column, "Error"});
    }

    return Token(type, content, start_line, start_column, details);
}

sakuraE::Token sakuraE::Lexer::makeSymbol() {
    int start_line = current_line;
    int start_column = current_column;
    fzlib::String content;
    TokenType type = TokenType::UNKNOWN;
    fzlib::String details;

    switch (peek()) {
        case '+':
            if (peek(1) == '=') {
                type = TokenType::ADD_ASSIGN;
                content = "+=";
                next(); next();
            }
            else if (peek(1) == '+') {
                type = TokenType::AINC;
                content = "++";
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
            else if (peek(1) == '-') {
                type = TokenType::SDEC;
                content = "--";
                next(); next();
            }
            else if (peek(1) == '>') {
                type = TokenType::ARROW;
                content = "->";
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
            if (peek(1) == '>') {
                type = TokenType::BIG_ARROW;
                content = "=>";
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
            else if (peek(1) == '>') {
                type = TokenType::FN_OP;
                content = "|>";
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


std::vector<sakuraE::Token> sakuraE::Lexer::tokenize() {
    std::vector<Token> tokens;

    while (peek() != '\0') {
        skip();
        if (peek() == '\0') break;
        char c = peek();
        Token token;
        if (std::isalpha(c) || c == '_') {
            token = makeIdentifierOrKeyword();
        }
        else if (c == '0' && (peek(1) == 'b' || peek(1) == 'x' || peek(1) == 'o')) {
            token = makeNonDecimalLiteral();
        }
        else if (std::isdigit(c) || (c == '-' && std::isdigit(peek(1)))) {
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
