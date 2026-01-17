#ifndef SAKURAE_PARSER_BASE_HPP
#define SAKURAE_PARSER_BASE_HPP

#include <iostream>
#include <type_traits>
#include <memory>
#include <variant>
#include <tuple>

#include "lexer.h"

namespace sakuraE {
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

        std::shared_ptr<SakuraError> err = nullptr;
        TokenIter err_pos;

        Result(ParseStatus sts, std::shared_ptr<T> value, TokenIter e, 
            std::shared_ptr<SakuraError> error = nullptr, 
            TokenIter err_position = {}):
            val(std::move(value)), status(sts), end(e), err(error), err_pos(err_position) {}
        
        static Result<T> failed(TokenIter current, std::shared_ptr<SakuraError> err, TokenIter err_pos) {
            return Result<T>(ParseStatus::FAILED, nullptr, current, err, err_pos);
        }

        static Result<T> failed(TokenIter end) {
            return Result<T>(ParseStatus::FAILED, nullptr, end);
        }
    };

    // Concept: Check if the parser type T has a static epsilonable() method.
    template<typename T>
    concept HasEpsilonable = requires {
        { T::epsilonable() } -> std::convertible_to<bool>;
    };

    // Only to parse a Single token
    template<sakuraE::TokenType T>
    struct TokenParser {
        const std::shared_ptr<Token> token;

        TokenParser(std::shared_ptr<Token> tok): token(std::move(tok)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type == T;
        }

        static Result<TokenParser> parse(TokenIter begin, TokenIter end) {
            if (check(begin, end))
                return Result<TokenParser>(ParseStatus::SUCCESS, std::make_shared<TokenParser>(std::make_shared<Token>(*begin)), begin + 1);
            else {
                fzlib::String msg = "Expected " + fzlib::String(magic_enum::enum_name(T)) + ", but got ";
                if (begin == end) msg += "EOF";
                else msg += begin->typeToString();
                
                PositionInfo info;
                if (begin != end) info = begin->info;
                
                auto err = std::make_shared<SakuraError>(OccurredTerm::PARSER, msg, info);
                return Result<TokenParser>::failed(begin, err, begin);
            }
        }
    };

    // NullParser
    struct NullParser {
        static constexpr bool epsilonable() { 
            return true; 
        }

        static bool check(TokenIter begin, TokenIter end) {
            return true;
        }

        static Result<NullParser> parse(TokenIter begin, TokenIter end) {
            return Result<NullParser>(ParseStatus::SUCCESS, nullptr, begin);
        }
    };


    // Parse single token, but ignore it (not include it in the value)
    template<sakuraE::TokenType T>
    struct DiscardParser {
        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type  == T;
        }

        static Result<DiscardParser> parse(TokenIter begin, TokenIter end) {
            if (check(begin, end))
                return Result<DiscardParser>(ParseStatus::SUCCESS, nullptr, begin + 1);
            else {
                fzlib::String msg = "Expected " + fzlib::String(magic_enum::enum_name(T)) + ", but got ";
                if (begin == end) msg += "EOF";
                else msg += begin->typeToString();
                
                PositionInfo info;
                if (begin != end) info = begin->info;

                auto err = std::make_shared<SakuraError>(OccurredTerm::PARSER, msg, info);
                return Result<DiscardParser>::failed(begin, err, begin);
            }
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
                    if (result.err_pos > current) {
                        return Result<ClosureParser<T>>::failed(begin, result.err, result.err_pos);
                    }
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
                    return Result<ConnectionParser>::failed(original_begin, result.err, result.err_pos);
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
        static Result<OptionsParser> parseImpl(TokenIter begin, TokenIter end, std::shared_ptr<SakuraError> best_err, TokenIter best_pos) {
            if constexpr (Index == sizeof...(Nodes)) {
                if (!best_err) {
                    PositionInfo info;
                    if (begin != end) info = begin->info;
                    auto err = std::make_shared<SakuraError>(OccurredTerm::PARSER, "Unexpected token", info);
                    return Result<OptionsParser>::failed(begin, err, begin);
                }
                return Result<OptionsParser>::failed(begin, best_err, best_pos);
            } else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;

                if (!CurrentType::check(begin, end)) {
                    return parseImpl<Index + 1>(begin, end, best_err, best_pos);
                }

                auto result = CurrentType::parse(begin, end);

                if (result.status != ParseStatus::FAILED) {
                    std::variant<std::shared_ptr<Nodes>...> v;
                    v.template emplace<Index>(result.val);
                    return Result<OptionsParser>(ParseStatus::SUCCESS, std::make_shared<OptionsParser>(OptionsParser(std::move(v), Index)), result.end);
                }

                if (result.err_pos > best_pos || (result.err_pos == best_pos && !best_err)) {
                    return parseImpl<Index + 1>(begin, end, result.err, result.err_pos);
                } else {
                    return parseImpl<Index + 1>(begin, end, best_err, best_pos);
                }
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
            return parseImpl<0>(begin, end, nullptr, begin);
        }
    };

    template<typename T, sakuraE::TokenType Separator>
    class NullableSequenceParser {
        using SequenceRule = OptionsParser<
            ConnectionParser<
                T,
                ClosureParser<ConnectionParser<DiscardParser<Separator>, T>>
            >,
            NullParser
        >;

        std::shared_ptr<SequenceRule> _rule_result;

    public:
        NullableSequenceParser(std::shared_ptr<SequenceRule> res) : _rule_result(std::move(res)) {}

        static constexpr bool epsilonable() { return true; }
        static bool check(TokenIter begin, TokenIter end) { return true; }

        bool isMatch() const {                                
            return _rule_result->index() == 0;
        }

        std::vector<std::shared_ptr<T>> getClosure() const {
            std::vector<std::shared_ptr<T>> results;
            if (isEmpty()) return results;

            auto conn = std::get<0>(_rule_result->option());
            auto [head, closure] = conn->getTuple();

            results.push_back(head);
            for (auto& item_conn : closure->getClosure()) {
                results.push_back(std::get<1>(item_conn->getTuple()));
            }
            return results;
        }

        static Result<NullableSequenceParser<T, Separator>> parse(TokenIter begin, TokenIter end) {
            auto res = SequenceRule::parse(begin, end);
            if (res.status == ParseStatus::SUCCESS) {
                return Result<NullableSequenceParser<T, Separator>>(
                    ParseStatus::SUCCESS,
                    std::make_shared<NullableSequenceParser<T, Separator>>(res.val),
                    res.end
                );
            }
            return Result<NullableSequenceParser<T, Separator>>::failed(begin, res.err, res.err_pos);
        }
    };

}

#endif