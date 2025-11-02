#ifndef SAKORAE_ASTBASE_HPP
#define SAKORAE_ASTBASE_HPP

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
            status(sts), val(std::move(value)), end(e) {}

        static Result<T> failed(TokenIter end) {
            return ParseResult<T>(ParseStatus::FAILED, nullptr, end);
        }
    };

    template<sakoraE::TokenType T>
    struct BasicNode {
        const std::shared_ptr<Token> token;

        BasicNode(std::shared_ptr<Token> tok): token(std::move(tok)) {}

        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type  == T;
        }

        static Result<BasicNode> parse(TokenIter begin, TokenIter end) {
            std::shared_ptr<sakoraE::Token> token = begin;

            if (check(begin, end))
                return Result<BasicNode>(ParseStatus::SUCCESS, std::make_shared<BasicNode>(begin), begin + 1);
            else
                return Result<BasicNode>::failed(end);
        }
    };

    template<sakoraE::TokenType T>
    struct DiscardNode {
        static bool check(TokenIter begin, TokenIter end) {
            return begin != end && begin->type  == T;
        }

        static Result<DiscardNode> parse(TokenIter begin, TokenIter end) {
            std::shared_ptr<sakoraE::Token> token = begin;

            if (check(begin, end))
                return Result<DiscardNode>(ParseStatus::SUCCESS, nullptr, begin + 1);
            else
                return Result<DiscardNode>::failed(end);
        }
    };

    template<typename T>
    class ClosureNode {
        std::vector<std::shared_ptr<T>> children;
    public:
        ClosureNode(std::vector<std::shared_ptr<T>> _children): children(std::move(_children)) {}
        const std::vector<std::shared_ptr<T>> getChildren() {
            return children;
        }

        static bool check(TokenIter begin, TokenIter end) {
            return T::check(begin, end);
        }

        static Result<ClosureNode<T>> parse(TokenIter begin, TokenIter end) {
            std::vector<std::shared_ptr<T>> ch;
            TokenIter current = begin;

            while (current != end) {
                if (!T::check(current, end))
                    break;
                auto result = T::parse(current, end);

                if (!result.status != ParseStatus::SUCCESS)
                    break;

                ch.push_back(result.val);
                current = result.end;
            }

            return Result<ClosureNode<T>>(ParseStatus::SUCCESS, new ClosureNode<T>(std::move(ch)), current);
        }
    };

    template<typename... Nodes>
    class ConnectionNode {
        std::tuple<std::shared_ptr<Nodes>...> children;

        template<std::size_t Index>
        static bool checkImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index < sizeof...(Nodes)) {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                if (!CurrentType::lookahead(begin, end))
                    return false;


                return true;
            }
            return true;
        }

        template<size_t Index, typename... ParsedNodes>
        static Result<ConnectionNode> parseImpl(TokenIter begin, TokenIter end, std::tuple<ParsedNodes...>&& current_tuple) {
            if constexpr (Index == sizeof...(Nodes)) {
                return ParseResult<ConnectionNode>(
                    true, 
                    new ConnectionNode(std::move(current_tuple)),
                    begin
                );
            } else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;
                auto result = CurrentType::parse(begin, end);
                
                if (!result.success) {
                    cleanup_tuple(current_tuple);
                    return Result<ConnectionNode>::failed(begin);
                }
                
                auto new_tuple = std::tuple_cat(
                    std::move(current_tuple),
                    std::make_tuple(result.val)
                );
            
                return parseImpl<Index + 1, ParsedNodes..., CurrentType*>(result.end, end, std::move(new_tuple));
            }
        }
        
        template<typename... Ts>
        static void cleanup_tuple(const std::tuple<std::shared_ptr<Ts>...>& tuple) {
            std::apply([](std::shared_ptr<auto>... ptrs) {
                (ptrs.reset(), ...);
            }, tuple);
        }
    public:
        ConnectionNode(std::tuple<std::shared_ptr<Nodes>...>&& _children): children(std::move(_children)) {}
        
        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<ConnectionNode> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, end, std::make_tuple());
        }
    };

    template<typename... Nodes>
    class OptionsNode {
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
        static Result<OptionsNode> parseImpl(TokenIter begin, TokenIter end) {
            if constexpr (Index == sizeof...(Nodes)) {
                return Result<OptionsNode>::failed(end);
            } else {
                using CurrentType = std::tuple_element_t<Index, std::tuple<Nodes...>>;

                if (!CurrentType::check(begin, end)) {
                    return parseImpl<Index + 1>(begin, end);
                }

                auto result = CurrentType::parse(begin, end);
                if (result.success) {
                    std::variant<std::shared_ptr<Nodes>...> v;
                    v.template emplace<Index>(result.val);
                    return ParseResult<OptionsNode>(true, new OptionsNode(std::move(v), Index), result.end);
                }

                return parseImpl<Index + 1>(begin, end);
            }
        }
    public:
        OptionsNode(std::variant<std::shared_ptr<Nodes>...>&& child, size_t index)
            : _child(std::move(child)), _index(index) {}

        const std::variant<std::shared_ptr<Nodes>...>& child() const { return _child; }
        size_t index() const { return _index; }

        static bool check(TokenIter begin, TokenIter end) {
            return checkImpl<0>(begin, end);
        }

        static Result<OptionsNode> parse(TokenIter begin, TokenIter end) {
            return parseImpl<0>(begin, end);
        }
    };
}

#endif