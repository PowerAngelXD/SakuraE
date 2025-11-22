#ifndef SAKORAE_PARSER_HPP
#define SAKORAE_PARSER_HPP
#include <memory>

#include "AST.hpp"
#include "parser_base.hpp"

namespace sakoraE {
    // Forward declare
    class AddExprParser;
    class WholeExprParser;
    //

    class LiteralParser: public ResourceFetcher, 
    public OptionsParser<
        TokenParser<TokenType::INT_N>,
        TokenParser<TokenType::FLOAT_N>,
        TokenParser<TokenType::STRING>,
        TokenParser<TokenType::BOOL_CONST>,
        TokenParser<TokenType::CHAR>
    > 
    {
    public:
        using BaseType = OptionsParser<TokenParser<TokenType::INT_N>, TokenParser<TokenType::FLOAT_N>, TokenParser<TokenType::STRING>, TokenParser<TokenType::BOOL_CONST>, TokenParser<TokenType::CHAR>>;
        
        // 从基类移动构造
        LiteralParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<LiteralParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status, 
                    std::make_shared<LiteralParser>(std::move(*result.val)), 
                    result.end};
        }

        NodePtr genResource() override;
    };

    class IndexOpParser: public ResourceFetcher, 
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        AddExprParser,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>
    > 
    {
    public:
        using BaseType = ConnectionParser<DiscardParser<TokenType::LEFT_SQUARE_BRACKET>, AddExprParser, DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>>;
        
        IndexOpParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<IndexOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status, 
                    std::make_shared<IndexOpParser>(std::move(*result.val)), 
                    result.end};
        }

        NodePtr genResource() override;
    };

    class CallingOpParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<DiscardParser<TokenType::LEFT_PAREN>, ClosureParser<WholeExprParser>, ClosureParser<ConnectionParser<TokenParser<TokenType::COMMA>, WholeExprParser>>, DiscardParser<TokenType::RIGHT_PAREN>>;
        
        CallingOpParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<CallingOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<CallingOpParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class AtomIdentifierExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<TokenParser<TokenType::IDENTIFIER>, ClosureParser<OptionsParser<CallingOpParser, IndexOpParser>>>;
        
        AtomIdentifierExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<AtomIdentifierExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AtomIdentifierExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class IdentifierExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<ClosureParser<TokenParser<TokenType::LGC_NOT>>, AtomIdentifierExprParser, ClosureParser<ConnectionParser<TokenParser<TokenType::DOT>, AtomIdentifierExprParser>>>;
        
        IdentifierExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<IdentifierExprParser> parse(TokenIter begin, TokenIter end) {
            std::cout << "meet identifier!" << std::endl;
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<IdentifierExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class PrimExprParser: public ResourceFetcher,
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
        using BaseType = OptionsParser<LiteralParser, IdentifierExprParser, ConnectionParser<DiscardParser<TokenType::LEFT_PAREN>, WholeExprParser, DiscardParser<TokenType::RIGHT_PAREN>>>;
        
        PrimExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<PrimExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<PrimExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class MulExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<PrimExprParser, ClosureParser<ConnectionParser<OptionsParser<TokenParser<TokenType::MUL>, TokenParser<TokenType::DIV>, TokenParser<TokenType::MOD>>, PrimExprParser>>>;
        
        MulExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<MulExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<MulExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class AddExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<MulExprParser, ClosureParser<ConnectionParser<OptionsParser<TokenParser<TokenType::ADD>, TokenParser<TokenType::SUB>>, MulExprParser>>>;
        
        AddExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<AddExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AddExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };
    
    class LogicExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<AddExprParser, ClosureParser<ConnectionParser<OptionsParser<TokenParser<TokenType::LGC_LS_THAN>, TokenParser<TokenType::LGC_LSEQU_THAN>, TokenParser<TokenType::LGC_MR_THAN>, TokenParser<TokenType::LGC_MREQU_THAN>, TokenParser<TokenType::LGC_NOT_EQU>, TokenParser<TokenType::LGC_EQU>>, AddExprParser>>>;
        
        LogicExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<LogicExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<LogicExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class BinaryExprParser: public ResourceFetcher,
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
        using BaseType = ConnectionParser<LogicExprParser, ClosureParser<ConnectionParser<OptionsParser<TokenParser<TokenType::LGC_AND>, TokenParser<TokenType::LGC_OR>>, LogicExprParser>>>;
        
        BinaryExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<BinaryExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<BinaryExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class ArrayExprParser: public ResourceFetcher,
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
    public:
        using BaseType = ConnectionParser<DiscardParser<TokenType::LEFT_SQUARE_BRACKET>, ClosureParser<WholeExprParser>, ClosureParser<ConnectionParser<DiscardParser<TokenType::COMMA>, WholeExprParser>>, DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>>;
        
        ArrayExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<ArrayExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<ArrayExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class AssignExprParser: public ResourceFetcher,
    public ConnectionParser<
        IdentifierExprParser,
        TokenParser<TokenType::ASSIGN_OP>,
        WholeExprParser
    >
    {
    public:
        using BaseType = ConnectionParser<IdentifierExprParser, TokenParser<TokenType::ASSIGN_OP>, WholeExprParser>;
        
        AssignExprParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<AssignExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AssignExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class WholeExprParser: public ResourceFetcher,
    public OptionsParser<
        BinaryExprParser,
        ArrayExprParser,
        AssignExprParser
    >
    {
    public:
        using BaseType = OptionsParser<BinaryExprParser, ArrayExprParser, AssignExprParser>;
        
        WholeExprParser(BaseType&& base) : BaseType(std::move(base)) {}
        
        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<WholeExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<WholeExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    class BasicTypeModifierParser: public ResourceFetcher,
    public OptionsParser<
        TokenParser<TokenType::TYPE_INT>,
        TokenParser<TokenType::TYPE_CHAR>,
        TokenParser<TokenType::TYPE_FLOAT>,
        TokenParser<TokenType::TYPE_BOOL>
    >
    {
    public:
        using BaseType = OptionsParser<TokenParser<TokenType::TYPE_INT>, TokenParser<TokenType::TYPE_CHAR>, TokenParser<TokenType::TYPE_FLOAT>, TokenParser<TokenType::TYPE_BOOL>>;
        
        BasicTypeModifierParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<BasicTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<BasicTypeModifierParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };
    
    class ArrayTypeModifierParser: public ResourceFetcher,
    public ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        ClosureParser<AddExprParser>,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>,
        BasicTypeModifierParser
    >
    {
    public:
        using BaseType = ConnectionParser<DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,ClosureParser<AddExprParser>,DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>,BasicTypeModifierParser>;
        
        ArrayTypeModifierParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<ArrayTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<ArrayTypeModifierParser>(std::move(*result.val)),
                    result.end};
        } 
        
        NodePtr genResource() override;
    };

    class TypeModifierParser: public ResourceFetcher,
    public OptionsParser<
        BasicTypeModifierParser,
        ArrayTypeModifierParser
    >
    {
    public:
        using BaseType = OptionsParser<BasicTypeModifierParser, ArrayTypeModifierParser>;
        
        TypeModifierParser(BaseType&& base) : BaseType(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BaseType::check(begin, end);
        }

        static Result<TypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = BaseType::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<TypeModifierParser>(std::move(*result.val)),
                    result.end};
        }   
        
        NodePtr genResource() override;
    };
    
    // Statement parsers would go here...

}

#endif