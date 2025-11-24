#ifndef SAKORAE_PARSER_HPP
#define SAKORAE_PARSER_HPP
#include <memory>

#include "AST.hpp"
#include "parser_base.hpp"

namespace sakoraE {
    // Forward declare
    class AddExprParser;
    class WholeExprParser;
    class BlockStmtParser;
    class ElseStmtParser;
    //

    using LiteralParserRule = OptionsParser<
        TokenParser<TokenType::INT_N>,
        TokenParser<TokenType::FLOAT_N>,
        TokenParser<TokenType::STRING>,
        TokenParser<TokenType::BOOL_CONST>,
        TokenParser<TokenType::CHAR>
    >;
    class LiteralParser: public ResourceFetcher, public LiteralParserRule {
    public:
        // 从基类移动构造
        LiteralParser(LiteralParserRule&& base) : LiteralParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return LiteralParserRule::check(begin, end);
        }

        static Result<LiteralParser> parse(TokenIter begin, TokenIter end) {
            auto result = LiteralParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status, 
                    std::make_shared<LiteralParser>(std::move(*result.val)), 
                    result.end};
        }

        NodePtr genResource() override;
    };

    using IndexOpParserRule = ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        AddExprParser,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>
    >;
    class IndexOpParser: public ResourceFetcher, public IndexOpParserRule {
    public:
        IndexOpParser(IndexOpParserRule&& base) : IndexOpParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return IndexOpParserRule::check(begin, end);
        }

        static Result<IndexOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = IndexOpParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status, 
                    std::make_shared<IndexOpParser>(std::move(*result.val)), 
                    result.end};
        }

        NodePtr genResource() override;
    };

    using CallingOpParserRule = ConnectionParser<
        DiscardParser<TokenType::LEFT_PAREN>,
        ClosureParser<WholeExprParser>,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::COMMA>,
                WholeExprParser
            >
        >,
        DiscardParser<TokenType::RIGHT_PAREN>
    >;
    class CallingOpParser: public ResourceFetcher, public CallingOpParserRule {
    public:
        CallingOpParser(CallingOpParserRule&& base) : CallingOpParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return CallingOpParserRule::check(begin, end);
        }

        static Result<CallingOpParser> parse(TokenIter begin, TokenIter end) {
            auto result = CallingOpParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<CallingOpParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using AtomIdentifierExprParserRule = ConnectionParser<
        TokenParser<TokenType::IDENTIFIER>,
        ClosureParser<
            OptionsParser<
                CallingOpParser,
                IndexOpParser
            >
        >
    >;
    class AtomIdentifierExprParser: public ResourceFetcher, public AtomIdentifierExprParserRule {
    public:
        AtomIdentifierExprParser(AtomIdentifierExprParserRule&& base) : AtomIdentifierExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return AtomIdentifierExprParserRule::check(begin, end);
        }

        static Result<AtomIdentifierExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = AtomIdentifierExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AtomIdentifierExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using IdentifierExprParserRule = ConnectionParser<
        ClosureParser<TokenParser<TokenType::LGC_NOT>>,
        AtomIdentifierExprParser,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::DOT>,
                AtomIdentifierExprParser
            >
        >
    >;
    class IdentifierExprParser: public ResourceFetcher, public IdentifierExprParserRule {
    public:
        IdentifierExprParser(IdentifierExprParserRule&& base) : IdentifierExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return IdentifierExprParserRule::check(begin, end);
        }

        static Result<IdentifierExprParser> parse(TokenIter begin, TokenIter end) {
            std::cout << "meet identifier!" << std::endl;
            auto result = IdentifierExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<IdentifierExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using PrimExprParserRule = OptionsParser<
        LiteralParser,
        IdentifierExprParser,
        ConnectionParser<
            DiscardParser<TokenType::LEFT_PAREN>,
            WholeExprParser,
            DiscardParser<TokenType::RIGHT_PAREN>
        >
    >;
    class PrimExprParser: public ResourceFetcher, public PrimExprParserRule {
    public:
        PrimExprParser(PrimExprParserRule&& base) : PrimExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return PrimExprParserRule::check(begin, end);
        }

        static Result<PrimExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = PrimExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<PrimExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using MulExprParserRule = ConnectionParser<
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
    >;
    class MulExprParser: public ResourceFetcher, public MulExprParserRule {
    public:
        MulExprParser(MulExprParserRule&& base) : MulExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return MulExprParserRule::check(begin, end);
        }

        static Result<MulExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = MulExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<MulExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using AddExprParserRule = ConnectionParser<
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
    >;
    class AddExprParser: public ResourceFetcher, public AddExprParserRule {
    public:
        AddExprParser(AddExprParserRule&& base) : AddExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return AddExprParserRule::check(begin, end);
        }

        static Result<AddExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = AddExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AddExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };
    
    using LogicExprParserRule = ConnectionParser<
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
    >;
    class LogicExprParser: public ResourceFetcher, public LogicExprParserRule {
    public:
        LogicExprParser(LogicExprParserRule&& base) : LogicExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return LogicExprParserRule::check(begin, end);
        }

        static Result<LogicExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = LogicExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<LogicExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using BinaryExprParserRule = ConnectionParser<
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
    >;
    class BinaryExprParser: public ResourceFetcher, public BinaryExprParserRule {
    public:
        BinaryExprParser(BinaryExprParserRule&& base) : BinaryExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BinaryExprParserRule::check(begin, end);
        }

        static Result<BinaryExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = BinaryExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<BinaryExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using ArrayExprParserRule = ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        ClosureParser<WholeExprParser>,
        ClosureParser<
            ConnectionParser<
                DiscardParser<TokenType::COMMA>,
                WholeExprParser
            >
        >,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>
    >;
    class ArrayExprParser: public ResourceFetcher, public ArrayExprParserRule {
    public:
        ArrayExprParser(ArrayExprParserRule&& base) : ArrayExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ArrayExprParserRule::check(begin, end);
        }

        static Result<ArrayExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = ArrayExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<ArrayExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using AssignExprParserRule = ConnectionParser<
        IdentifierExprParser,
        TokenParser<TokenType::ASSIGN_OP>,
        WholeExprParser
    >;
    class AssignExprParser: public ResourceFetcher, public AssignExprParserRule {
    public:
        AssignExprParser(AssignExprParserRule&& base) : AssignExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return AssignExprParserRule::check(begin, end);
        }

        static Result<AssignExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = AssignExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<AssignExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using WholeExprParserRule = OptionsParser<
        AssignExprParser,
        BinaryExprParser,
        ArrayExprParser
    >;
    class WholeExprParser: public ResourceFetcher, public WholeExprParserRule {
    public:
        WholeExprParser(WholeExprParserRule&& base) : WholeExprParserRule(std::move(base)) {}
        
        static bool check(TokenIter begin, TokenIter end) {
            return WholeExprParserRule::check(begin, end);
        }

        static Result<WholeExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = WholeExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<WholeExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    using BasicTypeModifierParserRule = 
    OptionsParser<
        TokenParser<TokenType::TYPE_INT>,
        TokenParser<TokenType::TYPE_CHAR>, 
        TokenParser<TokenType::TYPE_FLOAT>, 
        TokenParser<TokenType::TYPE_BOOL>
    >;
    class BasicTypeModifierParser: public ResourceFetcher, public BasicTypeModifierParserRule {
    public:
        BasicTypeModifierParser(BasicTypeModifierParserRule&& base) : BasicTypeModifierParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BasicTypeModifierParserRule::check(begin, end);
        }

        static Result<BasicTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = BasicTypeModifierParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<BasicTypeModifierParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };
    
    using ArrayTypeModifierParserRule =
    ConnectionParser<
        DiscardParser<TokenType::LEFT_SQUARE_BRACKET>,
        ClosureParser<AddExprParser>,
        DiscardParser<TokenType::RIGHT_SQUARE_BRACKET>,
        BasicTypeModifierParser
    >;
    class ArrayTypeModifierParser: public ResourceFetcher, public ArrayTypeModifierParserRule {
    public:
        ArrayTypeModifierParser(ArrayTypeModifierParserRule&& base) : ArrayTypeModifierParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ArrayTypeModifierParserRule::check(begin, end);
        }

        static Result<ArrayTypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = ArrayTypeModifierParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<ArrayTypeModifierParser>(std::move(*result.val)),
                    result.end};
        } 
        
        NodePtr genResource() override;
    };

    using RangeExprParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_RANGE>,
        OptionsParser<
            ArrayExprParser,
            IdentifierExprParser
        >
    >;
    class RangeExprParser: public ResourceFetcher, public RangeExprParserRule {
    public:
        RangeExprParser(RangeExprParserRule&& base) : RangeExprParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return RangeExprParserRule::check(begin, end);
        }

        static Result<RangeExprParser> parse(TokenIter begin, TokenIter end) {
            auto result = RangeExprParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<RangeExprParser>(std::move(*result.val)),
                    result.end};
        }

        NodePtr genResource() override;
    };

    
    using TypeModifierParserRule = OptionsParser<BasicTypeModifierParser, ArrayTypeModifierParser>;
    class TypeModifierParser: public ResourceFetcher, public TypeModifierParserRule {
    public:
        TypeModifierParser(TypeModifierParserRule&& base) : TypeModifierParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return TypeModifierParserRule::check(begin, end);
        }

        static Result<TypeModifierParser> parse(TokenIter begin, TokenIter end) {
            auto result = TypeModifierParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<TypeModifierParser>(std::move(*result.val)),
                    result.end};
        }   
        
        NodePtr genResource() override;
    };
    
    // Statement parsers 
    using DeclareStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_LET>,
        TokenParser<TokenType::IDENTIFIER>,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::CONSTRAINT_OP>,
                TypeModifierParser
            >
        >,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::ASSIGN_OP>,
                WholeExprParser
            >
        >,
        TokenParser<TokenType::STMT_END>
    >;
    class DeclareStmtParser: public ResourceFetcher, public DeclareStmtParserRule {
    public:
        DeclareStmtParser(DeclareStmtParserRule&& base) : DeclareStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return DeclareStmtParser::check(begin, end);
        }

        static Result<DeclareStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = DeclareStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<DeclareStmtParser>(std::move(*result.val)),
                    result.end};
        }   
        
        NodePtr genResource() override;
    };

    using ExprStmtParserRule = 
    ConnectionParser<
        OptionsParser<
            IdentifierExprParser,
            AssignExprParser
        >,
        TokenParser<TokenType::STMT_END>
    >;
    class ExprStmtParser: public ResourceFetcher, public ExprStmtParserRule {
        public:
        ExprStmtParser(ExprStmtParserRule&& base) : ExprStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ExprStmtParser::check(begin, end);
        }

        static Result<ExprStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = ExprStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            
            return {result.status,
                    std::make_shared<ExprStmtParser>(std::move(*result.val)),
                    result.end};
        }   
        
        NodePtr genResource() override;
    };

    using IfStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_IF>,
        TokenParser<TokenType::LEFT_PAREN>,
        BinaryExprParser,
        TokenParser<TokenType::LEFT_PAREN>,
        OptionsParser<
            BlockStmtParser,
            ElseStmtParser
        >
    >;
    class IfStmtParser: public ResourceFetcher, public IfStmtParserRule {
    public:
        IfStmtParser(IfStmtParserRule&& base) : IfStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return IfStmtParserRule::check(begin, end);
        }

        static Result<IfStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = IfStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<IfStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };
    

    using ElseStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_ELSE>,
        BlockStmtParser
    >;
    class ElseStmtParser: public ResourceFetcher, public ElseStmtParserRule {
    public:
        ElseStmtParser(ElseStmtParserRule&& base) : ElseStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ElseStmtParserRule::check(begin, end);
        }

        static Result<ElseStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = ElseStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<ElseStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };
    

    using WhileStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_WHILE>,
        TokenParser<TokenType::LEFT_PAREN>,
        BinaryExprParser,
        TokenParser<TokenType::LEFT_PAREN>,
        BlockStmtParser
    >;
    class WhileStmtParser: public ResourceFetcher, public WhileStmtParserRule {
    public:
        WhileStmtParser(WhileStmtParserRule&& base) : WhileStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return WhileStmtParserRule::check(begin, end);
        }

        static Result<WhileStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = WhileStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<WhileStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };

    using TraditionalConditionChain = 
    ConnectionParser<
        DeclareStmtParser,
        WholeExprParser,
        TokenParser<TokenType::STMT_END>,
        WholeExprParser
    >;
    using RangeConditionChain = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_LET>,
        TokenParser<TokenType::IDENTIFIER>,
        ClosureParser<
            ConnectionParser<
                TokenParser<TokenType::CONSTRAINT_OP>,
                TypeModifierParser
            >
        >,
        TokenParser<TokenType::ASSIGN_OP>,
        RangeExprParser
    >;
    using ForStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_FOR>,
        TokenParser<TokenType::LEFT_PAREN>,
        OptionsParser<
            TraditionalConditionChain,
            RangeConditionChain
        >,
        TokenParser<TokenType::LEFT_PAREN>,
        BlockStmtParser
    >;
    class ForStmtParser: public ResourceFetcher, public ForStmtParserRule {
    public:
        ForStmtParser(ForStmtParserRule&& base) : ForStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ForStmtParserRule::check(begin, end);
        }

        static Result<ForStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = ForStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<ForStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };
    
    using ContainableStmt = 
    OptionsParser<
        DeclareStmtParser,
        ExprStmtParser,
        IfStmtParser,
        WhileStmtParser,
        ForStmtParser
    >;
    using BlockStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::LEFT_BRACKET>,
        ClosureParser<ContainableStmt>,
        TokenParser<TokenType::RIGHT_BRACKET>
    >;
    class BlockStmtParser: public ResourceFetcher, public BlockStmtParserRule {
    public:
        BlockStmtParser(BlockStmtParserRule&& base) : BlockStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return BlockStmtParserRule::check(begin, end);
        }

        static Result<BlockStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = BlockStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<BlockStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };

    using MemberUnit = 
    ConnectionParser<
        TokenParser<TokenType::IDENTIFIER>,
        TokenParser<TokenType::CONSTRAINT_OP>,
        TypeModifierParser
    >;
    using FuncDefineStmtParserRule =
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_FUNC>,
        TokenParser<TokenType::IDENTIFIER>,
        TokenParser<TokenType::LEFT_PAREN>,
        ClosureParser<MemberUnit>,
        TokenParser<TokenType::RIGHT_PAREN>,
        TokenParser<TokenType::ARROW>,
        TypeModifierParser,
        BlockStmtParser
    >;
    class FuncDefineStmtParser: public ResourceFetcher, public FuncDefineStmtParserRule {
    public:
        FuncDefineStmtParser(FuncDefineStmtParserRule&& base) : FuncDefineStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return FuncDefineStmtParserRule::check(begin, end);
        }

        static Result<FuncDefineStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = FuncDefineStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<FuncDefineStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };

    using ReturnStmtParserRule = 
    ConnectionParser<
        TokenParser<TokenType::KEYWORD_RETURN>,
        WholeExprParser,
        TokenParser<TokenType::STMT_END>
    >;
    class ReturnStmtParser: public ResourceFetcher, public ReturnStmtParserRule {
    public:
        ReturnStmtParser(ReturnStmtParserRule&& base) : ReturnStmtParserRule(std::move(base)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return ReturnStmtParserRule::check(begin, end);
        }

        static Result<ReturnStmtParser> parse(TokenIter begin, TokenIter end) {
            auto result = ReturnStmtParserRule::parse(begin, end);
            if (result.status != ParseStatus::SUCCESS) {
                return {result.status, nullptr, result.end};
            }
            return {result.status, std::make_shared<ReturnStmtParser>(std::move(*result.val)), result.end};
        }

        NodePtr genResource() override;
    };
}

#endif