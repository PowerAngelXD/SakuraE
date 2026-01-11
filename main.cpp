#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser_base.hpp"
#include "Compiler/Frontend/parser.hpp"

const fzlib::String SOURCE_CODE = R"(
func foo(a: int, b: int, s: string) -> int {
    let VAR = "hello";
    let VAR1 = "hello11";

    if (VAR != VAR1 && a == 2) {
        return 0;
    }
    
    VAR1 = s;

    return a + b + foo(1, 2);
}
)";

int main() {
    std::cout << "--- Source ---\n" << SOURCE_CODE << "\n";
    try {
        sakoraE::Lexer lexer(SOURCE_CODE);
        auto r = lexer.tokenize();

        for(auto t: r) {
            std::cout << t.toString() << "; size:" << t.toString().len() << std::endl;
        }
        sakoraE::TokenIter current = r.begin();
        while ((*current).type != sakoraE::TokenType::_EOF_) {
            auto result = sakoraE::StatementParser::parse(current, r.end());
            if (result.status == sakoraE::ParseStatus::FAILED) {
                if (result.err == nullptr) {
                    std::cerr << "Error: Parse failed with NULL error object at token: " << current->toString() << std::endl;
                    return 1;
                }    
                throw *(result.err);
            }

            auto res = result.val->genResource();

            std::cout << res->toFormatString(true) << std::endl;

            current = result.end;
        }
    } 
    catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } 
    catch (sakoraE::SakoraError& e) {
        std::cerr << e.toString() << "\n";
        return 1;
    } 
    catch (const std::exception& e) {
        std::cerr << "OtherError: " << e.what() << "\n";
        return 1;
    }

    return 0;
}