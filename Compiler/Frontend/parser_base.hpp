#ifndef SAKORAE_PARSER_BASE_HPP
#define SAKORAE_PARSER_BASE_HPP

#include <iostream>
#include <type_traits>
#include <memory>
#include <variant>
#include <tuple>

#include "lexer.h"

namespace sakoraE {
    using TokenIter = std::vector<Token>::const_iterator;

    enum class ParseStatus {
        SUCCESS, FAILED, UNPARSED
    };

    // Parser Result: using to identify the result
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

    // Concept: Check if the parser type T has a static epsilonable() method.
    template<typename T>
    concept HasHasEpsilonable = requires {
        { T::epsilonable() } -> std::convertible_to<bool>;
    };

    // Only to parse a single token
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

    // Parse single token, but ignore it (not include it in the value)
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
        std::vector<std::shared_ptr<T>> children;
    public:
        ClosureParser(std::vector<std::shared_ptr<T>> _children): children(std::move(_children)) {}

        const std::vector<std::shared_ptr<T>> getClosure() {
            return children;
        }

        bool isEmpty() {
            return children.empty();
        }

        static constexpr bool epsilonable() { return true; }

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

                if (result.status != ParseStatus::SUCCESS) {
                    break;
                }

                ch.push_back(result.val);
                current = result.end;
            }

            return Result<ClosureParser<T>>(ParseStatus::SUCCESS, std::make_shared<ClosureParser<T>>(std::move(ch)), current);
        }
    };

    template<typename T, typename = void>
    struct has_epsilonable : std::false_type {};

    template<typename T>
    struct has_epsilonable<T, std::void_t<decltype(T::epsilonable())>> : std::true_type {};

    template<typename... Nodes>
    class ConnectionParser {
        std::tuple<std::shared_ptr<Nodes>...> children;

        template<size_t Index, typename... ParsedNodes>
        static Result<ConnectionParser> parseImpl(
            TokenIter original_begin,
            TokenIter current_begin,
            TokenIter end,
            std::tuple<ParsedNodes...>&& current_tuple) 
        {
            if constexpr (Index == sizeof...(Nodes)) {
                auto instance = std::make_shared<ConnectionParser>(ConnectionParser(std::move(current_tuple)));
                return Result<ConnectionParser>(ParseStatus::SUCCESS, instance, current_begin);
            } 
            else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                
                auto result = CurrentType::parse(current_begin, end);
                
                if (result.status == ParseStatus::SUCCESS) {
                    return parseImpl<Index + 1>(
                        original_begin,
                        result.end,
                        end,
                        std::tuple_cat(std::move(current_tuple), std::make_tuple(result.val))
                    );
                } 
                else {
                    return Result<ConnectionParser>::failed(original_begin);
                }
            }
        }

        template<std::size_t Index>
        static bool checkImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                
                if (CurrentType::check(begin, end)) {
                    return true;
                }
                
                if constexpr (HasEpsilonable<CurrentType>){
                    if (CurrentType::epsilonable()) {
                        return checkImpl<Index + 1>(begin, end);
                    }
                }
                
                return false;
            }
            return true; 
        }

    public:
        ConnectionParser(std::tuple<std::shared_ptr<Nodes>...> ch) : children(std::move(ch)) {}
        ConnectionParser() = default;

        const std::tuple<std::shared_ptr<Nodes>...>& getTuple() const {
            return children;
        }

        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<ConnectionParser> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, begin, end, std::make_tuple());
        }
    };

    template<typename... Nodes>
    class OptionsParser {
        std::variant<std::shared_ptr<Nodes>...> _child;
        size_t _index;

        template<size_t Index>
        static constexpr bool epsilonableImpl() {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                
                if constexpr (HasEpsilonable<CurrentType>) {
                    if (CurrentType::epsilonable()) return true;
                }
                return epsilonableImpl<Index + 1>();
            }
            return false;
        }

        template<size_t Index>
        static bool checkImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                if (CurrentType::check(begin, end))
                    return true;
                return checkImpl<Index + 1>(begin, end);
            }
            return epsilonable();
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

                if (result.status != ParseStatus::FAILED) {
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
        OptionsParser(OptionsParser&& other) noexcept : _child(std::move(other._child)), _index(other._index) {}

        const std::variant<std::shared_ptr<Nodes>...>& option() const { return _child; }
        size_t index() const { return _index; }

        static constexpr bool epsilonable() {
            return epsilonableImpl<0>();
        }

        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<OptionsParser> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, end);
        }
    };
}

#endif