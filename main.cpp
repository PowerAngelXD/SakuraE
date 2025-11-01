#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include "Compiler/Frontend/ast.h"
#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser.h"

// 示例程序源代码
const std::string SOURCE_CODE = R"(
let max_count: int = 100;

data.list[1 + 2] = max_count * 2 + 5;

log_value(data.list[3], true); 

if (data.list[3] > max_count || max_count != 100) 
    let temp: float = data.list[3] / 2.0; 
else {
    data.list[0] = [1, 2 * 2, func_call(max_count, 0)];
}
while (max_count > 0) {
    max_count = max_count - 1;
}
for (let i: int = 0; i < 10; i = i + 1) 
    log_index(i); 
func my_func(arg1: int, arg2: [5]raw_string) -> int {
    let result = arg1 + 1;
    return result;
    let final = result;
}
{
    let a = 1;
    {
        let b = 2;
    }
}
)";

int main() {
    std::cout << "--- 源代码 ---\n" << SOURCE_CODE << "\n";
    
    try {
        Parser parser(SOURCE_CODE);

        std::vector<std::shared_ptr<StmtNode>> program_ast = parser.parseProgram();

        std::cout << "\n--- AST 结构 (toString 结果) ---\n";
        if (program_ast.empty()) {
            std::cout << "AST 是空的，可能源代码为空或解析失败。\n";
        } else {
            for (auto stmt : program_ast) {
                if (stmt) {
                    std::cout << stmt->toString() << "\n";
                }
            }
            std::cout << "\n--- AST 解析成功！---\n";
        }

    } catch (const std::runtime_error& e) {
        std::cerr << "\n--- 解析失败错误 ---\n";
        std::cerr << "错误信息: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "\n--- 未知错误 ---\n";
        std::cerr << "错误信息: " << e.what() << "\n";
        return 1;
    }

    return 0;
}