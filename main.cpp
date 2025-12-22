#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser_base.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/value.hpp"

const std::string SOURCE_CODE = R"(
func foo(a: int, b: int, s: string) -> int {
    let VAR = "hello";
    let VAR1 = "hello11";

    if (VAR != VAR1 && a == 2) {
        return 0;
    }
    
    return a + b;
}
)";

int main() {
    std::cout << "--- Source ---\n" << SOURCE_CODE << "\n";
    
    try {
        sakoraE::Lexer lexer(SOURCE_CODE);
        auto r = lexer.tokenize();

        for(auto t: r) {
            std::cout << t.toString() << std::endl;
        }
        sakoraE::TokenIter current = r.begin();
        while ((*current).type != sakoraE::TokenType::_EOF_) {
            auto result = sakoraE::StatementParser::parse(current, r.end());
            if (result.status == sakoraE::ParseStatus::FAILED)
                sutils::reportError(sakoraE::OccurredTerm::PARSER, "Parsering Failed...", {result.end->info.line, result.end->info.column, "system parser error"});

            auto res = result.val->genResource();

            std::cout << res->toFormatString(true) << std::endl;

            current = result.end;
        }
    } 
    catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } 
    catch (const std::exception& e) {
        std::cerr << "OtherError: " << e.what() << "\n";
        return 1;
    }

    return 0;
}