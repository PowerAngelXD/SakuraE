#ifndef SAKORAE_PARSER_BASE_HPP
#define SAKORAE_PARSER_BASE_HPP

#include <iostream>
#include <type_traits>
#include <memory>
#include <variant>

#include "lexer.h"

namespace sakoraE {
    using TokenIter = std::vector<Token>::const_iterator;

    enum class ParseStatus {
        SUCCESS, FAILED, UNPARSED
    };

    template<typename T>
    struct Result{
        std::shared_ptr<T> val = nullptr;
        ParseStatus status = ParseStatus::UNPARSED;
        TokenIter end;

        Result(ParseStatus sts, std::shared_ptr<T> value, TokenIter e):
            val(std::move(value)), status(sts), end(e) {}

        static Result<T> failed(TokenIter end) {
            return Result<T>(ParseStatus::FAILED, nullptr, end);
        }
    };

    template<sakoraE::TokenType T>
    struct TokenParser {
        const std::shared_ptr<Token> token;

        TokenParser(std::shared_ptr<Token> tok): token(std::move(tok)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type == T;
        }

        static Result<TokenParser> parse(TokenIter begin, TokenIter end) {
            if (check(begin, end))
                return Result<TokenParser>(ParseStatus::SUCCESS, std::make_shared<TokenParser>(std::make_shared<Token>(*begin)), begin + 1);
            else
                return Result<TokenParser>::failed(end);
        }
    };

    template<sakoraE::TokenType T>
    struct DiscardParser {
        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type  == T;
        }

        static Result<DiscardParser> parse(TokenIter begin, TokenIter end) {
            if (check(begin, end))
                return Result<DiscardParser>(ParseStatus::SUCCESS, nullptr, begin + 1);
            else
                return Result<DiscardParser>::failed(end);
        }
    };

    template<typename T>
    class ClosureParser {
    protected:
        std::vector<std::shared_ptr<T>> children;
    public:
        ClosureParser(std::vector<std::shared_ptr<T>> _children): children(std::move(_children)) {}
        const std::vector<std::shared_ptr<T>> getChildren() {
            return children;
        }

        static bool check(TokenIter begin, TokenIter end) {
            return T::check(begin, end);
        }

        static Result<ClosureParser<T>> parse(TokenIter begin, TokenIter end) {
            std::vector<std::shared_ptr<T>> ch;
            TokenIter current = begin;

            while (current != end) {
                if (!T::check(current, end))
                    break;
                auto result = T::parse(current, end);

                if (result.status != ParseStatus::SUCCESS)
                    break;

                ch.push_back(result.val);
                current = result.end;
            }

            return Result<ClosureParser<T>>(ParseStatus::SUCCESS, std::make_shared<ClosureParser>(ClosureParser<T>(std::move(ch))), current);
        }
    };

    template<typename... Nodes>
    class ConnectionParser {
    protected:
        std::tuple<std::shared_ptr<Nodes>...> children;

        template<std::size_t Index>
        static bool checkImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                if (!CurrentType::check(begin, end))
                    return false;


                return true;
            }
            return true;
        }

        template<size_t Index, typename... ParsedNodes>
        static Result<ConnectionParser> parseImpl(TokenIter begin, TokenIter end, std::tuple<ParsedNodes...>&& current_tuple) {
            if constexpr (Index == sizeof...(Nodes)) {
                return Result<ConnectionParser>(
                    ParseStatus::SUCCESS, 
                    std::make_shared<ConnectionParser>(ConnectionParser(std::move(current_tuple))),
                    begin
                );
            } else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                auto result = CurrentType::parse(begin, end);
                
                if (result.status != ParseStatus::SUCCESS) {
                    cleanup_tuple(current_tuple);
                    return Result<ConnectionParser>::failed(begin);
                }
                
                auto new_tuple = std::tuple_cat(
                    std::move(current_tuple),
                    std::make_tuple(result.val)
                );
            
                return parseImpl<Index + 1, ParsedNodes..., std::shared_ptr<CurrentType>>(result.end, end, std::move(new_tuple));
            }
        }
        
        template<typename... Ts>
        static void cleanup_tuple(const std::tuple<std::shared_ptr<Ts>...>& tuple) {
            std::apply([](std::shared_ptr<auto>... ptrs) {
                (ptrs.reset(), ...);
            }, tuple);
        }
    public:
        ConnectionParser(std::tuple<std::shared_ptr<Nodes>...>&& _children): children(std::move(_children)) {}
        
        const std::tuple<std::shared_ptr<Nodes>...>& getChildren() const { 
            return children; 
        }

        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<ConnectionParser> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, end, std::make_tuple());
        }
    };

    template<typename... Nodes>
    class OptionsParser {
    protected:
        std::variant<std::shared_ptr<Nodes>...> _child;
        size_t _index;

        template<size_t Index>
        static bool checkImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                if (CurrentType::check(begin, end))
                    return true;
                return checkImpl<Index + 1>(begin, end);
            }
            return false;
        }

        template<size_t Index>
        static Result<OptionsParser> parseImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index == sizeof...(Nodes)) {
                return Result<OptionsParser>::failed(end);
            } else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;

                if (!CurrentType::check(begin, end)) {
                    return parseImpl<Index + 1>(begin, end);
                }

                auto result = CurrentType::parse(begin, end);
                if (result.status != ParseStatus::SUCCESS) {
                    std::variant<std::shared_ptr<Nodes>...> v;
                    v.template emplace<Index>(result.val);
                    return Result<OptionsParser>(ParseStatus::SUCCESS, std::make_shared<OptionsParser>(OptionsParser(std::move(v), Index)), result.end);
                }

                return parseImpl<Index + 1>(begin, end);
            }
        }
    public:
        OptionsParser(std::variant<std::shared_ptr<Nodes>...>&& child, size_t index)
            : _child(std::move(child)), _index(index) {}

        // Get the variant: 'child' (if you want to use it, try 'std::get<Index>()')
        const std::variant<std::shared_ptr<Nodes>...>& child() const { return _child; }
        // Get the index of variant
        size_t index() const { return _index; }

        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<OptionsParser> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, end);
        }
    };
}

#endif