#include "LanguageServer/analyzer.hpp"

#include <algorithm>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser.hpp"

namespace sakurae::lsp {

    const std::vector<std::string> &runtimeFunctionNames() {
        static const std::vector<std::string> names = {
            "__alloc", "__free", "create_string", "free_string", "concat_string",
            "print", "println", "__println", "input", "inputc",
            "__runtime_alloc_value", "__runtime_check_array_bounds", "__runtime_array_bounds_error",
            "__runtime_type_info_basic", "__runtime_type_info_pointer", "__runtime_type_info_reference",
            "__runtime_type_info_array", "__gc_alloc", "__gc_register", "__gc_register_value",
            "__gc_register_value_slot", "__gc_enter_scope", "__gc_leave_scope", "__gc_pop",
            "__gc_collect", "__gc_get_atomic_type", "__gc_get_array_type",
            "__gc_get_array_type_with_length", "__gc_get_runtime_value_array_type", "__gc_get_struct_type",
        };
        return names;
    }

    namespace {

        using sakuraE::Token;
        using sakuraE::TokenType;

        std::string text(const Token &token) {
            return token.content.c_str();
        }

        std::string typeName(const Token &token) {
            return text(token);
        }

        std::string typeModifierName(const std::vector<Token> &tokens, std::size_t start, std::size_t &end) {
            if (start >= tokens.size()) {
                end = start;
                return {};
            }

            std::string name = typeName(tokens[start]);
            std::size_t cursor = start + 1;
            while (cursor < tokens.size()) {
                if (tokens[cursor].type == TokenType::MUL) {
                    name += '*';
                    ++cursor;
                    continue;
                }
                if (tokens[cursor].type == TokenType::AND) {
                    name += '&';
                    ++cursor;
                    continue;
                }
                if (tokens[cursor].type == TokenType::LEFT_SQUARE_BRACKET && cursor + 2 < tokens.size() &&
                    tokens[cursor + 2].type == TokenType::RIGHT_SQUARE_BRACKET) {
                    name += '[' + text(tokens[cursor + 1]) + ']';
                    cursor += 3;
                    continue;
                }
                break;
            }
            end = cursor;
            return name;
        }

        void markTypeContext(std::vector<bool> &typeContext, std::size_t begin, std::size_t end) {
            end = std::min(end, typeContext.size());
            for (std::size_t index = begin; index < end; ++index)
                typeContext[index] = true;
        }

        TypeInfo literalType(const Token &token) {
            switch (token.type) {
            case TokenType::INT_N:
                return {"i32", true};
            case TokenType::FLOAT_N:
                return {"f64", true};
            case TokenType::STRING:
                return {"string", true};
            case TokenType::CHAR:
                return {"char", true};
            case TokenType::BOOL_CONST:
                return {"bool", true};
            default:
                return {};
            }
        }

        Range tokenRange(const Token &token) {
            const auto length = std::max<std::size_t>(text(token).size(), 1);
            Position start{std::max(token.info.line - 1, 0), std::max(token.info.column - 1, 0)};
            return {start, {start.line, start.character + static_cast<int>(length)}};
        }

        void addDiagnostic(AnalysisResult &result, Range range, std::string message) {
            result.diagnostics.push_back({range, std::move(message), DiagnosticSeverity::Error, "sakurae"});
        }

        bool isBuiltin(const std::string &name) {
            if (name == "true" || name == "false")
                return true;
            const auto &runtimeNames = runtimeFunctionNames();
            return std::find(runtimeNames.begin(), runtimeNames.end(), name) != runtimeNames.end();
        }

        bool isInside(const Range &range, Position position) {
            if (position.line < range.start.line || position.line > range.end.line)
                return false;
            if (position.line == range.start.line && position.character < range.start.character)
                return false;
            if (position.line == range.end.line && position.character > range.end.character)
                return false;
            return true;
        }

    } // namespace

    AnalysisResult Analyzer::analyze(const Document &document) const {
        AnalysisResult result;
        std::vector<Token> tokens;
        try {
            sakuraE::Lexer lexer(fzlib::String(document.text));
            tokens = lexer.tokenize();
        } catch (const sakuraE::SakuraError &error) {
            addDiagnostic(result,
                          {{std::max(error.position().line - 1, 0), std::max(error.position().column - 1, 0)},
                           {std::max(error.position().line - 1, 0), std::max(error.position().column, 0)}},
                          error.message().c_str());
            return result;
        }

        result.index.scopes.push_back({0, -1, {{0, 0}, {1000000, 0}}, {}});
        std::vector<int> tokenScopes(tokens.size(), 0);
        std::vector<bool> declaration(tokens.size(), false);
        std::vector<bool> typeContext(tokens.size(), false);
        std::vector<std::vector<std::pair<std::size_t, Symbol>>> pendingParameters;
        int currentScope = 0;

        auto addSymbol = [&](Symbol symbol) {
            const int index = static_cast<int>(result.index.symbols.size());
            result.index.symbols.push_back(std::move(symbol));
            result.index.scopes[currentScope].symbols.push_back(index);
            return index;
        };

        for (std::size_t i = 0; i < tokens.size(); ++i) {
            tokenScopes[i] = currentScope;
            const Token &token = tokens[i];

            if (token.type == TokenType::LEFT_BRACKET) {
                const int newScope = static_cast<int>(result.index.scopes.size());
                result.index.scopes.push_back({newScope, currentScope, tokenRange(token), {}});
                currentScope = newScope;
                if (!pendingParameters.empty()) {
                    for (const auto &[parameterIndex, parameter] : pendingParameters.back()) {
                        declaration[parameterIndex] = true;
                        const int symbolIndex = static_cast<int>(result.index.symbols.size());
                        result.index.symbols.push_back(parameter);
                        result.index.scopes[currentScope].symbols.push_back(symbolIndex);
                    }
                    pendingParameters.pop_back();
                }
                tokenScopes[i] = currentScope;
                continue;
            }
            if (token.type == TokenType::RIGHT_BRACKET) {
                if (currentScope != 0) {
                    result.index.scopes[currentScope].range.end = tokenRange(token).end;
                    currentScope = result.index.scopes[currentScope].parent;
                }
                tokenScopes[i] = currentScope;
                continue;
            }

            if (token.type == TokenType::KEYWORD_FUNC && i + 1 < tokens.size() &&
                tokens[i + 1].type == TokenType::IDENTIFIER) {
                const std::string name = text(tokens[i + 1]);
                const auto nameRange = tokenRange(tokens[i + 1]);
                const int functionIndex =
                    addSymbol({name, SymbolKind::Function, {}, nameRange, nameRange, "func " + name, currentScope, -1});
                declaration[i + 1] = true;

                std::vector<std::pair<std::size_t, Symbol>> parameters;
                std::size_t cursor = i + 2;
                while (cursor < tokens.size() && tokens[cursor].type != TokenType::LEFT_PAREN)
                    ++cursor;
                if (cursor < tokens.size())
                    ++cursor;
                while (cursor < tokens.size() && tokens[cursor].type != TokenType::RIGHT_PAREN) {
                    if (tokens[cursor].type == TokenType::IDENTIFIER) {
                        const std::size_t parameterIndex = cursor;
                        std::string parameterName = text(tokens[cursor]);
                        TypeInfo parameterType;
                        if (cursor + 2 < tokens.size() && tokens[cursor + 1].type == TokenType::CONSTRAINT_OP) {
                            std::size_t typeEnd = cursor + 2;
                            parameterType = {typeModifierName(tokens, cursor + 2, typeEnd), true};
                            markTypeContext(typeContext, cursor + 2, typeEnd);
                            cursor = typeEnd;
                        } else {
                            ++cursor;
                        }
                        auto parameterRange = tokenRange(tokens[parameterIndex]);
                        parameters.emplace_back(parameterIndex, Symbol{parameterName, SymbolKind::Parameter,
                                                                       parameterType, parameterRange, parameterRange,
                                                                       parameterType.name, 0, functionIndex});
                        declaration[parameterIndex] = true;
                        if (cursor < tokens.size() && tokens[cursor].type == TokenType::COMMA)
                            ++cursor;
                        continue;
                    }
                    ++cursor;
                }
                std::string returnType = "unknown";
                for (; cursor < tokens.size() && tokens[cursor].type != TokenType::LEFT_BRACKET; ++cursor) {
                    if (tokens[cursor].type == TokenType::ARROW && cursor + 1 < tokens.size()) {
                        std::size_t typeEnd = cursor + 1;
                        returnType = typeModifierName(tokens, cursor + 1, typeEnd);
                        markTypeContext(typeContext, cursor + 1, typeEnd);
                        cursor = typeEnd;
                    }
                }
                result.index.symbols[functionIndex].type = {returnType, returnType != "unknown"};
                result.index.symbols[functionIndex].detail = "func " + name + "() -> " + returnType;
                pendingParameters.push_back(std::move(parameters));
                continue;
            }

            if (token.type == TokenType::KEYWORD_LET && i + 1 < tokens.size() &&
                tokens[i + 1].type == TokenType::IDENTIFIER) {
                const std::size_t nameIndex = i + 1;
                const std::string name = text(tokens[nameIndex]);
                TypeInfo variableType;
                std::size_t cursor = nameIndex + 1;
                if (cursor + 1 < tokens.size() && tokens[cursor].type == TokenType::CONSTRAINT_OP) {
                    std::size_t typeEnd = cursor + 1;
                    variableType = {typeModifierName(tokens, cursor + 1, typeEnd), true};
                    markTypeContext(typeContext, cursor + 1, typeEnd);
                    cursor = typeEnd;
                }
                while (cursor < tokens.size() && tokens[cursor].type != TokenType::ASSIGN_OP &&
                       tokens[cursor].type != TokenType::STMT_END)
                    ++cursor;
                if (!variableType.known && cursor + 1 < tokens.size()) {
                    const std::size_t expression = cursor + 1;
                    variableType = literalType(tokens[expression]);
                    if (!variableType.known && expression + 1 < tokens.size() &&
                        (tokens[expression].type == TokenType::AND || tokens[expression].type == TokenType::MUL) &&
                        tokens[expression + 1].type == TokenType::IDENTIFIER) {
                        const int sourceIndex = result.index.lookup(text(tokens[expression + 1]), currentScope);
                        if (sourceIndex >= 0) {
                            const std::string &sourceType = result.index.symbols[sourceIndex].type.name;
                            if (result.index.symbols[sourceIndex].type.known && tokens[expression].type == TokenType::AND)
                                variableType = {sourceType + '*', true};
                            else if (result.index.symbols[sourceIndex].type.known &&
                                     sourceType.size() > 1 && sourceType.back() == '*' &&
                                     tokens[expression].type == TokenType::MUL)
                                variableType = {sourceType.substr(0, sourceType.size() - 1), true};
                        }
                    }
                }
                auto range = tokenRange(tokens[nameIndex]);
                addSymbol(
                    {name, SymbolKind::Variable, variableType, range, range, variableType.name, currentScope, -1});
                declaration[nameIndex] = true;
            }
        }

        currentScope = 0;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const Token &token = tokens[i];
            if (token.type == TokenType::LEFT_BRACKET) {
                currentScope = tokenScopes[i];
            } else if (token.type == TokenType::RIGHT_BRACKET && currentScope != 0) {
                currentScope = result.index.scopes[currentScope].parent;
            }
            if (token.type == TokenType::IDENTIFIER && !declaration[i] && !typeContext[i]) {
                const bool afterDot = i > 0 && tokens[i - 1].type == TokenType::DOT;
                const bool declarationLike = i > 0 && (tokens[i - 1].type == TokenType::KEYWORD_FUNC ||
                                                       tokens[i - 1].type == TokenType::CONSTRAINT_OP);
                if (afterDot || declarationLike)
                    continue;
                const std::string name = text(token);
                const int symbolIndex = isBuiltin(name) ? -1 : result.index.lookup(name, tokenScopes[i]);
                auto range = tokenRange(token);
                result.index.references.push_back({name, range, tokenScopes[i], symbolIndex});
                if (symbolIndex < 0 && !isBuiltin(name))
                    addDiagnostic(result, range, "Undefined symbol: " + name);
            }
        }

        auto current = tokens.cbegin();
        while (current != tokens.cend() && current->type != TokenType::_EOF_) {
            auto parsed = sakuraE::StatementParser::parse(current, tokens.cend());
            if (parsed.status == sakuraE::ParseStatus::FAILED) {
                if (parsed.err) {
                    const auto &position = parsed.err->position();
                    addDiagnostic(result,
                                  {{std::max(position.line - 1, 0), std::max(position.column - 1, 0)},
                                   {std::max(position.line - 1, 0), std::max(position.column, 0)}},
                                  parsed.err->message().c_str());
                }
                break;
            }
            if (parsed.end <= current)
                break;
            current = parsed.end;
        }

        return result;
    }

    bool Analyzer::contains(const Range &range, Position position) {
        return isInside(range, position);
    }

    int Analyzer::scopeAt(const AnalysisResult &result, Position position) const {
        int best = 0;
        int bestDepth = 0;
        for (const auto &scope : result.index.scopes) {
            if (!contains(scope.range, position))
                continue;
            int depth = 0;
            for (int parent = scope.parent; parent >= 0 && parent < static_cast<int>(result.index.scopes.size());
                 parent = result.index.scopes[parent].parent)
                ++depth;
            if (depth >= bestDepth) {
                best = scope.id;
                bestDepth = depth;
            }
        }
        return best;
    }

    const Symbol *Analyzer::symbolAt(const AnalysisResult &result, Position position) const {
        for (const auto &symbol : result.index.symbols)
            if (contains(symbol.selection_range, position))
                return &symbol;
        return nullptr;
    }

    const Reference *Analyzer::referenceAt(const AnalysisResult &result, Position position) const {
        for (const auto &reference : result.index.references)
            if (contains(reference.range, position))
                return &reference;
        return nullptr;
    }

} // namespace sakurae::lsp
