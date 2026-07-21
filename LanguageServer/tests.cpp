#include "LanguageServer/analyzer.hpp"
#include "LanguageServer/document_store.hpp"
#include "LanguageServer/json_rpc.hpp"

#include <cassert>
#include <sstream>

int main() {
    using namespace sakurae::lsp;

    DocumentStore store;
    store.open("file:///test.sak", 1, "a\nxyz\n");
    const Document *document = store.find("file:///test.sak");
    assert(document != nullptr);
    assert(store.positionAt(*document, 2).line == 1);
    assert(store.positionAt(*document, 2).character == 0);
    assert(store.offsetAt(*document, {1, 2}) == 4);

    Analyzer analyzer;
    const std::string valid = "func add(a: i32) -> i32 {\n  let x = 1;\n  return x;\n}\n";
    store.open("file:///valid.sak", 1, valid);
    AnalysisResult result = analyzer.analyze(*store.find("file:///valid.sak"));
    assert(result.diagnostics.empty());
    assert(result.index.lookup("add", 0) >= 0);
    assert(result.index.lookup("x", 1) >= 0);

    store.open("file:///invalid.sak", 1, "func bad() -> i32 { return missing; }\n");
    AnalysisResult invalid = analyzer.analyze(*store.find("file:///invalid.sak"));
    assert(!invalid.diagnostics.empty());

    std::stringstream input;
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})";
    input << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    nlohmann::json message;
    assert(readMessage(input, message));
    assert(message.at("method") == "shutdown");

    return 0;
}
