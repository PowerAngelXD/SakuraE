#include "LanguageServer/server.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "LanguageServer/json_rpc.hpp"

namespace sakurae::lsp {
    namespace {

        std::string uriFrom(const nlohmann::json &params) {
            return params.at("textDocument").at("uri").get<std::string>();
        }

        nlohmann::json location(const std::string &uri, Range range) {
            nlohmann::json result;
            result["uri"] = uri;
            result["range"] = {{"start", {{"line", range.start.line}, {"character", range.start.character}}},
                               {"end", {{"line", range.end.line}, {"character", range.end.character}}}};
            return result;
        }

    } // namespace

    Position Server::readPosition(const nlohmann::json &value) {
        return {value.value("line", 0), value.value("character", 0)};
    }

    Range Server::readRange(const nlohmann::json &value) {
        return {readPosition(value.value("start", nlohmann::json::object())),
                readPosition(value.value("end", nlohmann::json::object()))};
    }

    nlohmann::json Server::toJson(Position position) {
        return {{"line", position.line}, {"character", position.character}};
    }

    nlohmann::json Server::toJson(Range range) {
        return {{"start", toJson(range.start)}, {"end", toJson(range.end)}};
    }

    nlohmann::json Server::toJson(const Diagnostic &diagnostic) {
        return {{"range", toJson(diagnostic.range)},
                {"severity", static_cast<int>(diagnostic.severity)},
                {"message", diagnostic.message},
                {"source", diagnostic.source}};
    }

    void Server::analyzeAndPublish(const std::string &uri, std::ostream &output) {
        const Document *document = documents.find(uri);
        if (!document)
            return;
        analyses[uri] = analyzer.analyze(*document);
        nlohmann::json diagnostics = nlohmann::json::array();
        for (const auto &diagnostic : analyses[uri].diagnostics)
            diagnostics.push_back(toJson(diagnostic));
        writeMessage(output, {{"jsonrpc", "2.0"},
                              {"method", "textDocument/publishDiagnostics"},
                              {"params", {{"uri", uri}, {"diagnostics", diagnostics}}}});
    }

    nlohmann::json Server::handle(const nlohmann::json &request, std::ostream &output) {
        const std::string method = request.value("method", "");
        const nlohmann::json params = request.value("params", nlohmann::json::object());

        if (method == "initialize") {
            return {{"jsonrpc", "2.0"},
                    {"id", request.at("id")},
                    {"result",
                     {{"capabilities",
                       {{"textDocumentSync", {{"openClose", true}, {"change", 1}}},
                        {"completionProvider", {{"triggerCharacters", {"."}}}},
                        {"definitionProvider", true},
                        {"hoverProvider", true},
                        {"documentSymbolProvider", true}}}}}};
        }
        if (method == "initialized")
            return {};
        if (method == "shutdown") {
            shutdownRequested = true;
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", nullptr}};
        }
        if (method == "exit") {
            shutdownRequested = true;
            return {};
        }
        if (method == "textDocument/didOpen") {
            const auto &document = params.at("textDocument");
            documents.open(document.at("uri").get<std::string>(), document.value("version", 0),
                           document.value("text", ""));
            analyzeAndPublish(document.at("uri").get<std::string>(), output);
            return {};
        }
        if (method == "textDocument/didChange") {
            const std::string uri = uriFrom(params);
            const int version = params.at("textDocument").value("version", 0);
            const auto &changes = params.at("contentChanges");
            if (!changes.empty() && changes.back().contains("text"))
                documents.change(uri, version, changes.back().at("text").get<std::string>());
            analyzeAndPublish(uri, output);
            return {};
        }
        if (method == "textDocument/didClose") {
            const std::string uri = uriFrom(params);
            documents.close(uri);
            analyses.erase(uri);
            writeMessage(output, {{"jsonrpc", "2.0"},
                                  {"method", "textDocument/publishDiagnostics"},
                                  {"params", {{"uri", uri}, {"diagnostics", nlohmann::json::array()}}}});
            return {};
        }

        if (!request.contains("id"))
            return {};
        if (!params.contains("textDocument"))
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", nullptr}};
        const std::string uri = uriFrom(params);
        const Document *document = documents.find(uri);
        if (!document)
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", nullptr}};
        if (!analyses.contains(uri))
            analyses[uri] = analyzer.analyze(*document);
        const AnalysisResult &analysis = analyses.at(uri);
        const Position position = readPosition(params.value("position", nlohmann::json::object()));

        if (method == "textDocument/completion") {
            nlohmann::json items = nlohmann::json::array();
            std::unordered_set<std::string> names;
            for (const char *keyword : {"if", "else", "while", "for", "func", "return", "let", "const",
                                        "range", "break", "continue", "match", "repeat", "struct", "impl",
                                        "is", "typeof", "ref", "default", "sizeof"}) {
                items.push_back({{"label", keyword}, {"kind", 14}});
            }
            for (const char *type : {"i32", "i64", "ui32", "ui64", "f32", "f64", "bool", "char", "string", "void"}) {
                items.push_back({{"label", type}, {"kind", 25}});
            }
            for (const auto &runtimeName : runtimeFunctionNames())
                items.push_back({{"label", runtimeName}, {"kind", 3}, {"detail", "SakuraE runtime function"}});
            const int scope = analyzer.scopeAt(analysis, position);
            for (const int index : analysis.index.visibleSymbols(scope)) {
                if (index < 0 || index >= static_cast<int>(analysis.index.symbols.size()))
                    continue;
                const Symbol &symbol = analysis.index.symbols[index];
                if (!names.insert(symbol.name).second)
                    continue;
                const int kind = symbol.kind == SymbolKind::Function ? 3 : 6;
                items.push_back({{"label", symbol.name}, {"kind", kind}, {"detail", symbol.detail}});
            }
            return {
                {"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", {{"isIncomplete", false}, {"items", items}}}};
        }

        const Symbol *symbol = analyzer.symbolAt(analysis, position);
        const Reference *reference = analyzer.referenceAt(analysis, position);
        const Symbol *target = symbol;
        if (!target && reference && reference->symbol_index >= 0 &&
            reference->symbol_index < static_cast<int>(analysis.index.symbols.size()))
            target = &analysis.index.symbols[reference->symbol_index];

        if (method == "textDocument/definition") {
            nlohmann::json result = nullptr;
            if (target)
                result = location(uri, target->selection_range);
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", result}};
        }
        if (method == "textDocument/hover") {
            nlohmann::json result = nullptr;
            if (target) {
                std::ostringstream content;
                content << "```sakurae\n" << target->detail;
                if (target->type.known)
                    content << "\n// type: " << target->type.name;
                content << "\n```";
                result = {{"contents", {{"kind", "markdown"}, {"value", content.str()}}},
                          {"range", toJson(target->selection_range)}};
            }
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", result}};
        }
        if (method == "textDocument/documentSymbol") {
            nlohmann::json symbols = nlohmann::json::array();
            for (const Symbol &entry : analysis.index.symbols) {
                symbols.push_back({{"name", entry.name},
                                   {"kind", static_cast<int>(entry.kind)},
                                   {"detail", entry.detail},
                                   {"range", toJson(entry.range)},
                                   {"selectionRange", toJson(entry.selection_range)}});
            }
            return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", symbols}};
        }

        return {{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", nullptr}};
    }

    void Server::run(std::istream &input, std::ostream &output) {
        nlohmann::json request;
        while (!shutdownRequested && readMessage(input, request)) {
            try {
                nlohmann::json response = handle(request, output);
                if (!response.is_null() && !response.empty())
                    writeMessage(output, response);
            } catch (const std::exception &error) {
                if (request.contains("id"))
                    writeMessage(output, {{"jsonrpc", "2.0"},
                                          {"id", request.at("id")},
                                          {"error", {{"code", -32603}, {"message", error.what()}}}});
            }
        }
    }

} // namespace sakurae::lsp
