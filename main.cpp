#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser_base.hpp"
#include "Compiler/Frontend/parser.hpp"

// 示例程序源代码
const std::string SOURCE_CODE = R"(
WWWWi[1*3] - 9 != 99 && !MYID
)";

int main() {
    std::cout << "--- 源代码 ---\n" << SOURCE_CODE << "\n";
    
    try {
        sakoraE::Lexer lexer(SOURCE_CODE);
        auto r = lexer.tokenize();

        for(auto t: r) {
            std::cout << t.toString() << std::endl;
        }

        auto result = sakoraE::WholeExprParser::parse(r.begin(), r.end());

        if (result.status == sakoraE::ParseStatus::FAILED)
            sutils::reportError(sakoraE::OccurredTerm::PARSER, "Parsering Failed...", {result.end->info.line, result.end->info.column, "system parser error"});

        auto res = result.val->genResource();

        std::cout << res->toFormatString(true) << std::endl;
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