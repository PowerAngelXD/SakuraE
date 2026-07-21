#include "LanguageServer/analyzer.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser.hpp"

namespace sakurae::lsp {
    namespace {

        using sakuraE::Token;
        using sakuraE::TokenType;

        std::string text(const Token &token) {
            return token.content.c_str();
        }

        std::string typeName(const Token &token) {
            return text(token);
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
            static const std::unordered_set<std::string> values = {"print",  "println", "input",
                                                                   "inputc", "true",    "false"};
            return values.contains(name);
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
                            parameterType = {typeName(tokens[cursor + 2]), true};
                            typeContext[cursor + 2] = true;
                        }
                        auto parameterRange = tokenRange(tokens[cursor]);
                        parameters.emplace_back(parameterIndex, Symbol{parameterName, SymbolKind::Parameter,
                                                                       parameterType, parameterRange, parameterRange,
                                                                       parameterType.name, 0, functionIndex});
                        declaration[parameterIndex] = true;
                        ++cursor;
                    }
                    ++cursor;
                }
                std::string returnType = "unknown";
                for (; cursor < tokens.size() && tokens[cursor].type != TokenType::LEFT_BRACKET; ++cursor) {
                    if (tokens[cursor].type == TokenType::ARROW && cursor + 1 < tokens.size()) {
                        returnType = text(tokens[cursor + 1]);
                        typeContext[cursor + 1] = true;
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
                    variableType = {text(tokens[cursor + 1]), true};
                    typeContext[cursor + 1] = true;
                    cursor += 2;
                }
                while (cursor < tokens.size() && tokens[cursor].type != TokenType::ASSIGN_OP &&
                       tokens[cursor].type != TokenType::STMT_END)
                    ++cursor;
                if (!variableType.known && cursor + 1 < tokens.size())
                    variableType = literalType(tokens[cursor + 1]);
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
