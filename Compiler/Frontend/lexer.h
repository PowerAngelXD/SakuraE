#ifndef SAKURAE_LEXER_H
#define SAKURAE_LEXER_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "includes/magic_enum.hpp"
#include "includes/String.hpp"

#include "Compiler/Error/error.hpp"
#include "Compiler/Utils/Logger.hpp"

namespace sakuraE {
    enum class TokenType {
        IDENTIFIER, KEYWORD,

        INT_N, FLOAT_N,
        STRING, CHAR,
        BOOL_CONST,

        ADD, SUB, MUL, DIV, MOD,
        OR, AND,
        LGC_NOT, LGC_NOT_EQU, LGC_AND, LGC_OR, LGC_EQU, LGC_MR_THAN, LGC_LS_THAN, LGC_MREQU_THAN, LGC_LSEQU_THAN,
        LEFT_PAREN, RIGHT_PAREN, // ()
        LEFT_SQUARE_BRACKET, RIGHT_SQUARE_BRACKET, // []
        LEFT_BRACKET, RIGHT_BRACKET, // {}
        ASSIGN_OP, ADD_ASSIGN, SUB_ASSIGN, MUL_ASSIGN, DIV_ASSIGN, // =, +=, -=, *=, /=
        AUTO_INC, AUTO_DEC, // ++, --
        CONSTRAINT_OP, // :
        ARROW,           // ->
        BIG_ARROW,       // =>
        SPACE_SHIP,      // <=>
        DOT,             // .
        COMMA,           // ,
        STMT_END,        // ;
        FN_OP,           // |>
        AINC,            // ++
        SDEC,            // --

        KEYWORD_LET, KEYWORD_IF, KEYWORD_ELSE,
        KEYWORD_WHILE, KEYWORD_FOR, KEYWORD_FUNC,
        KEYWORD_RETURN, KEYWORD_CONST, KEYWORD_RANGE,
        KEYWORD_BREAK, KEYWORD_CONTINUE, KEYWORD_REF,
        KEYWORD_STRUCT, KEYWORD_IMPL, KEYWORD_REPEAT,
        KEYWORD_MATCH, KEYWORD_DEFAULT,

        TYPE_I32, TYPE_I64, TYPE_F32,
        TYPE_F64, TYPE_CHAR, TYPE_BOOL,
        TYPE_STRING, TYPE_UI32, TYPE_UI64,

        _EOF_,
        UNKNOWN
    };

    class Token {
    public:
        PositionInfo info;
        fzlib::String content;
        TokenType type;

        Token(TokenType t = TokenType::UNKNOWN,
                const fzlib::String &c = "default",
                int l = 0,
                int col = 0,
                const fzlib::String &det = "default");

        fzlib::String typeToString() const;
        fzlib::String toString() const;
    };

    class Lexer {
    public:
        Lexer(const fzlib::String &source);
        std::vector<Token> tokenize();

    private:
        fzlib::String source_code;
        size_t current_pos;
        int current_line = 1;
        int current_column = 1;

        const std::vector<fzlib::String> keywords = {
            "if", "else", "while", "for", "func",
            "return", "let", "const", "range", "true",
            "false", "break", "continue", "match", "repeat",
            "struct", "impl", "is", "typeof", "ref", "default"
        };

        const std::vector<fzlib::String> typeFields = {
            "i32", "i64", "f32", "f64", "ui32", "ui64", "bool", "char", "string"
        };

        char peek(int offset = 0) const;
        char next();
        void skip();
        bool isKeyword(const fzlib::String &content) const;
        bool isTypeField(const fzlib::String &content) const;
        TokenType str2KeywordType(fzlib::String content) const;
        TokenType str2TypeField(fzlib::String content) const;
        Token makeIdentifierOrKeyword();
        Token makeNumberLiteral();
        Token makeCharLiteral();
        Token makeStringLiteral();
        Token makeNonDecimalLiteral();
        Token makeSymbol();
    };

}

#endif
