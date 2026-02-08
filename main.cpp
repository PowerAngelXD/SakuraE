#include <iostream>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>

#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser_base.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/generator.hpp"
#include "Compiler/LLVMCodegen/LLVMCodegenerator.hpp"

const fzlib::String SOURCE_CODE = R"(
func main(a: int, b: int) -> int {
    let g = 10;
    for (let i=0; i<a+b; i++) {
        g += i*2;
    }
    return g;
}
)";
typedef int (*SakuraFuncPtr)(int, int);
int main() {
    std::cout << "--- Source ---\n" << SOURCE_CODE << "\n";
    try {
        sakuraE::Lexer lexer(SOURCE_CODE);
        auto r = lexer.tokenize();

        for(auto t: r) {
            std::cout << t.toString() << "; size:" << t.toString().len() << std::endl;
        }
        sakuraE::TokenIter current = r.begin();
        while ((*current).type != sakuraE::TokenType::_EOF_) {
            auto result = sakuraE::StatementParser::parse(current, r.end());
            if (result.status == sakuraE::ParseStatus::FAILED) {
                if (result.err == nullptr) {
                    std::cerr << "Error: Parse failed with NULL error object at token: " << current->toString() << std::endl;
                    return 1;
                }    
                throw *(result.err);
            }

            auto res = result.val->genResource();

            std::cout << res->toFormatString(true) << std::endl;

            std::cout << "=================SakuraE IR Printer==================" << std::endl;

            sakuraE::IR::IRGenerator generator("TestProject");

            generator.visitStmt(res);

            std::cout << generator.toFormatString() << std::endl;

            std::cout << "===================LLVM IR Printer===================" << std::endl;

            current = result.end;
            auto program = generator.getProgram();

            sakuraE::Codegen::LLVMCodeGenerator llvmCodegen(&program);

            llvmCodegen.start();

            std::cout << "===================Before Optimize===================" << std::endl;

            llvmCodegen.print();

            llvmCodegen.optimize();

            std::cout << "===================After Optimize===================" << std::endl;

            llvmCodegen.print();

            auto module = llvmCodegen.getModules()[0];
            llvm::Module* rawModule = module->content; 
            auto modulePtr = std::unique_ptr<llvm::Module>(rawModule);

            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();
            llvm::InitializeNativeTargetAsmParser();

            std::string errStr;
            llvm::ExecutionEngine* EE = llvm::EngineBuilder(std::move(modulePtr))
                                            .setErrorStr(&errStr)
                                            .create();

            if (!EE) {
                std::cerr << "Failed to construct ExecutionEngine: " << errStr << std::endl;
                return 1;
            }

            llvm::Function* mainFn = EE->FindFunctionNamed("main");
            if (!mainFn) {
                std::cerr << "Could not find main function in module" << std::endl;
                return 1;
            }

            EE->finalizeObject();

            auto addr = EE->getFunctionAddress("main");

            SakuraFuncPtr func = reinterpret_cast<SakuraFuncPtr>(addr);

            std::cout << "JIT Result: " << func(10,10) << std::endl;


            int a=10,b=10;
            int g=10;

            for (int i=0;i<a+b;i++) {
                g += i*2;
            }

            std::cout << "Real Result: " << g << std::endl;
        }
    } 
    catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } 
    catch (sakuraE::SakuraError& e) {
        std::cerr << e.toString() << "\n";
        return 1;
    } 
    catch (const std::exception& e) {
        std::cerr << "OtherError: " << e.what() << "\n";
        return 1;
    }

    return 0;
}