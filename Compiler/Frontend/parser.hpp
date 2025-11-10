#ifndef SAKORAE_PARSER_HPP
#define SAKORAE_PARSER_HPP
#include <memory>

#include "AST.hpp"

namespace sakoraE {
    // Forward declare
    class AddExprParser;
    class WholeExprParser;
    //

    class LiteralParser:
    public OptionsParser<
        TokenParser<TokenType::INT_N>,
        TokenParser<TokenType::FLOAT_N>,
        TokenParser<TokenType::STRING>,
        TokenParser<TokenType::BOOL_CONST>,
        TokenParser<TokenType::CHAR>
    > 
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsParser::check(begin, end);
        }

        static Result<LiteralParser> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsParser::parse(begin, end);
            return Result<LiteralParser>(result.status, 
                                        std::static_pointer_cast<LiteralParser>(result.val), 
                                        result.end);
        }
    };

    class IndexOpParser:
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        AddExprParser,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>
    > 
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<IndexOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<IndexOpParser>(result.status, 
                                        std::static_pointer_cast<IndexOpParser>(result.val), 
                                        result.end);
        }
    };

    class CallingOpParser:
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_PAREN>,
        ClosureParser<WholeExprParser>,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::COMMA>,
                WholeExprParser
            >
        >,
        DiscardParser<TokenType::RIGHT_PAREN>
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<CallingOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<CallingOpParser>(result.status,
                                        std::static_pointer_cast<CallingOpParser>(result.val),
                                        result.end);
        }
    };

    class AtomIdentifierExprParser:
    public ConnectionParser<
        TokenParser<TokenType::IDENTIFIER>,
        ClosureParser<
            OptionsParser<
                CallingOpParser,
                IndexOpParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<AtomIdentifierExprParser> parse(TokenIter begin,  TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<AtomIdentifierExprParser>(result.status,
                                                    std::static_pointer_cast<AtomIdentifierExprParser>(result.val),
                                                    result.end);
        }
    };

    class IdentifierExprParser: 
    public ConnectionParser<
        ClosureParser<TokenParser<TokenType::LGC_NOT>>,
        AtomIdentifierExprParser,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::DOT>,
                AtomIdentifierExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<IdentifierExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<IdentifierExprParser>(result.status,
                                                std::static_pointer_cast<IdentifierExprParser>(result.val),
                                                result.end);
        }
    };

    class PrimExprParser: 
    public OptionsParser<
        LiteralParser,
        IdentifierExprParser,
        ConnectionParser<
            DiscardParser<TokenType::LEFT_PAREN>,
            WholeExprParser,
            DiscardParser<TokenType::RIGHT_PAREN>
        >
    > 
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsParser::check(begin, end);
        }

        static Result<PrimExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsParser::parse(begin, end);
            return Result<PrimExprParser>(result.status,
                                            std::static_pointer_cast<PrimExprParser>(result.val),
                                            result.end);
        }
    };

    class MulExprParser: 
    public ConnectionParser<
        PrimExprParser,
        ClosureParser<
            ConnectionParser<
                OptionsParser<
                    TokenParser<TokenType::MUL>,
                    TokenParser<TokenType::DIV>,
                    TokenParser<TokenType::MOD>
                >,
                PrimExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<MulExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<MulExprParser>(result.status,
                                        std::static_pointer_cast<MulExprParser>(result.val),
                                        result.end);
        }
    };

    class AddExprParser: 
    public ConnectionParser<
        MulExprParser,
        ClosureParser<
            ConnectionParser<
                OptionsParser<
                    TokenParser<TokenType::ADD>,
                    TokenParser<TokenType::SUB>
                >,
                MulExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<AddExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<AddExprParser>(result.status,
                                        std::static_pointer_cast<AddExprParser>(result.val),
                                        result.end);
        }
    };
    
    class LogicExprParser:
    public ConnectionParser<
        AddExprParser,
        ClosureParser<
            ConnectionParser<
                OptionsParser<
                    TokenParser<TokenType::LGC_LS_THAN>,
                    TokenParser<TokenType::LGC_LSEQU_THAN>,
                    TokenParser<TokenType::LGC_MR_THAN>,
                    TokenParser<TokenType::LGC_MREQU_THAN>,
                    TokenParser<TokenType::LGC_NOT_EQU>,
                    TokenParser<TokenType::LGC_EQU>
                >,
                AddExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<LogicExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<LogicExprParser>(result.status,
                                            std::static_pointer_cast<LogicExprParser>(result.val),
                                            result.end);
        }
    };

    class BoolExprParser:
    public ConnectionParser<
        LogicExprParser,
        ClosureParser<
            ConnectionParser<
                OptionsParser<
                    TokenParser<TokenType::LGC_AND>,
                    TokenParser<TokenType::LGC_OR>
                >,
                LogicExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<BoolExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<BoolExprParser>(result.status,
                                            std::static_pointer_cast<BoolExprParser>(result.val),
                                            result.end);
        }
    };

    class ArrayExprParser:
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        ClosureParser<WholeExprParser>,
        ClosureParser<
            ConnectionParser<
                DiscardParser<TokenType::COMMA>,
                WholeExprParser
            >
        >,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>
    >
    {
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<ArrayExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<ArrayExprParser>(result.status,
                                            std::static_pointer_cast<ArrayExprParser>(result.val),
                                            result.end);
        }
    };

    class AssignExprParser:
    public ConnectionParser<
        IdentifierExprParser,
        TokenParser<TokenType::ASSIGN_OP>,
        WholeExprParser
    >
    {
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<AssignExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<AssignExprParser>(result.status,
                                            std::static_pointer_cast<AssignExprParser>(result.val),
                                            result.end);
        }
    };

    class WholeExprParser:
    public OptionsParser<
        AddExprParser,
        BoolExprParser,
        ArrayExprParser,
        AssignExprParser
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsParser::check(begin, end);
        }

        static Result<WholeExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsParser::parse(begin, end);
            return Result<WholeExprParser>(result.status,
                                        std::static_pointer_cast<WholeExprParser>(result.val),
                                        result.end);
        }
    };

    class BasicTypeModifierParser:
    public OptionsParser<
        TokenParser<TokenType::TYPE_INT>,
        TokenParser<TokenType::TYPE_CHAR>,
        TokenParser<TokenType::TYPE_FLOAT>,
        TokenParser<TokenType::TYPE_BOOL>
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsParser::check(begin, end);
        }

        static Result<BasicTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsParser::parse(begin, end);
            return Result<BasicTypeModifierParser>(result.status,
                                        std::static_pointer_cast<BasicTypeModifierParser>(result.val),
                                        result.end);
        }
    };

    class ArrayTypeModifierParser:
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        ClosureParser<AddExprParser>,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>,
        ClosureParser<WholeExprParser>,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::COMMA>,
                WholeExprParser
            >
        >
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return ConnectionParser::check(begin, end);
        }

        static Result<ArrayTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = ConnectionParser::parse(begin, end);
            return Result<ArrayTypeModifierParser>(result.status,
                                        std::static_pointer_cast<ArrayTypeModifierParser>(result.val),
                                        result.end);
        } 
    };

    class TypeModifierParser:
    public OptionsParser<
        BasicTypeModifierParser,
        ArrayTypeModifierParser
    >
    {
    public:
        static bool check(TokenIter begin, TokenIter end) {
            return OptionsParser::check(begin, end);
        }

        static Result<TypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = OptionsParser::parse(begin, end);
            return Result<TypeModifierParser>(result.status,
                                        std::static_pointer_cast<TypeModifierParser>(result.val),
                                        result.end);
        }   
    };
    
    // Statement

}

#endif