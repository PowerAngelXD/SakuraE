#include "LanguageServer/analyzer.hpp"
#include "LanguageServer/document_store.hpp"
#include "LanguageServer/json_rpc.hpp"
#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser.hpp"

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

    const std::string runtimeCalls =
        "func runtime_calls() -> i32 {\n"
        "  print(\"a\");\n"
        "  println(\"b\");\n"
        "  __println(\"c\");\n"
        "  input();\n"
        "  inputc();\n"
        "  create_string(\"d\");\n"
        "  free_string(create_string(\"e\"));\n"
        "  concat_string(\"f\", \"g\");\n"
        "  __alloc(1);\n"
        "  __free(__alloc(1));\n"
        "  return 0;\n"
        "}\n";
    store.open("file:///runtime.sak", 1, runtimeCalls);
    AnalysisResult runtime = analyzer.analyze(*store.find("file:///runtime.sak"));
    assert(runtime.diagnostics.empty());

    const std::string pointerTypes =
        "func pointer_types(value: i32, pointer_value: i32*) -> void {\n"
        "  let pointer: i32* = &value;\n"
        "  let inferred = &value;\n"
        "}\n";
    store.open("file:///pointer-types.sak", 1, pointerTypes);
    AnalysisResult pointers = analyzer.analyze(*store.find("file:///pointer-types.sak"));
    const int pointerScope = 1;
    const int parameterIndex = pointers.index.lookup("pointer_value", pointerScope);
    const int variableIndex = pointers.index.lookup("pointer", pointerScope);
    const int inferredIndex = pointers.index.lookup("inferred", pointerScope);
    assert(parameterIndex >= 0);
    assert(variableIndex >= 0);
    assert(inferredIndex >= 0);
    assert(pointers.index.symbols[parameterIndex].type.known);
    assert(pointers.index.symbols[parameterIndex].type.name == "i32*");
    assert(pointers.index.symbols[variableIndex].type.known);
    assert(pointers.index.symbols[variableIndex].type.name == "i32*");
    assert(pointers.index.symbols[inferredIndex].type.known);
    assert(pointers.index.symbols[inferredIndex].type.name == "i32*");

    sakuraE::Lexer literalLexer(fzlib::String("1U 2UL 3L 4f"));
    const auto literalTokens = literalLexer.tokenize();
    assert(literalTokens.size() >= 5);
    assert(literalTokens[0].type == sakuraE::TokenType::INT_N);
    assert(literalTokens[1].type == sakuraE::TokenType::INT_N);
    assert(literalTokens[2].type == sakuraE::TokenType::INT_N);
    assert(literalTokens[3].type == sakuraE::TokenType::FLOAT_N);

    assert(sakuraE::tokenTypeToString(sakuraE::TokenType::STMT_END) == "';'");
    assert(sakuraE::tokenTypeToString(sakuraE::TokenType::KEYWORD_RETURN) == "'return'");
    assert(sakuraE::tokenTypeToString(sakuraE::TokenType::TYPE_I32) == "'i32'");
    assert(sakuraE::tokenTypeToString(sakuraE::TokenType::IDENTIFIER) == "identifier");

    sakuraE::Lexer missingTerminatorLexer(fzlib::String("let value = 1"));
    const auto missingTerminatorTokens = missingTerminatorLexer.tokenize();
    auto missingTerminator = sakuraE::StatementParser::parse(missingTerminatorTokens.cbegin(),
                                                              missingTerminatorTokens.cend());
    assert(missingTerminator.status == sakuraE::ParseStatus::FAILED);
    assert(missingTerminator.err != nullptr);
    assert(std::string(missingTerminator.err->message().c_str()).find("Expected ';'") != std::string::npos);

    sakuraE::Lexer addExpressionLexer(fzlib::String("1 + 2"));
    const auto addExpressionTokens = addExpressionLexer.tokenize();
    auto addExpression = sakuraE::WholeExprParser::parse(addExpressionTokens.cbegin(),
                                                          addExpressionTokens.cend());
    assert(addExpression.status == sakuraE::ParseStatus::SUCCESS);
    assert(addExpression.val->genResource()->hasNode(sakuraE::ASTTag::AddExprNode));

    std::stringstream input;
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})";
    input << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    nlohmann::json message;
    assert(readMessage(input, message));
    assert(message.at("method") == "shutdown");

    return 0;
}
