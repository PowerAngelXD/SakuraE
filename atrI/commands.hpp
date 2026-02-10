#ifndef SAKURAE_ATRI_COMMANDS_HPP
#define SAKURAE_ATRI_COMMANDS_HPP

#include <iostream>
#include <llvm/ExecutionEngine/Orc/CoreContainers.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include "Runtime/alloc.h"
#include "Runtime/raw_string.h"
#include "Runtime/print.h"


#include "Compiler/Frontend/lexer.h"
#include "Compiler/Frontend/parser_base.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/generator.hpp"
#include "Compiler/LLVMCodegen/LLVMCodegenerator.hpp"
#include "utils.hpp"
#include "config/config.hpp"

namespace atri::cmds {
    inline void cmdHelp(std::vector<fzlib::String> args) {
        auto content = readSourceFile("./help.txt");
        std::cout << content << std::endl;
    }

    inline void cmdExit(std::vector<fzlib::String> args) {
        exit(0);
    }

    inline void cmdRun(std::vector<fzlib::String> args) {
        if (args.size() < 1) {
            fzlib::String content = "Invalid argument for command: 'run': ";
            for (auto arg: args) {
                content += arg + " ";
            }
            throw std::runtime_error(content.c_str());
        }

        auto content = readSourceFile(args[0]);
        DebugConfig config;

        if (args.size() >= 2 && args[1] == "-ast") config.displayAST = true; 
        if (args.size() >= 3 && args[2] == "-sakir") config.displaySakIR = true; 
        if (args.size() >= 4 && args[3] == "-rawllvm") config.displayRawLLVMIR = true; 
        if (args.size() >= 5 && args[4] == "-llvmir") config.displayOptimizedLLVMIR = true; 
        
        sakuraE::Lexer lexer(content);
        auto r = lexer.tokenize();

        sakuraE::TokenIter current = r.begin();
        sakuraE::IR::IRGenerator generator("__main");

        while ((*current).type != sakuraE::TokenType::_EOF_) {
            auto result = sakuraE::StatementParser::parse(current, r.end());
            if (result.status == sakuraE::ParseStatus::FAILED) {
                if (result.err == nullptr) {
                    throw std::runtime_error("Error: Parse failed with NULL error object at token: ");
                }    
                throw *(result.err);
            }

            auto res = result.val->genResource();

            if (config.displayAST) {
                std::cout << "--------------================:DEBUG: AST DISPLAY:================--------------" << std::endl;
                std::cout << res->toFormatString() << std::endl;
            }

            generator.visitStmt(res);
            current = result.end;
        }

        if (config.displaySakIR) {
            std::cout << "--------------================:DEBUG: SAKIR DISPLAY:================--------------" << std::endl;
            std::cout << generator.toFormatString() << std::endl;
        }

        auto program = generator.getProgram();

        sakuraE::Codegen::LLVMCodeGenerator llvmCodegen(&program);
        llvmCodegen.start();

        if (config.displayRawLLVMIR) {
            std::cout << "--------------================:DEBUG: RAW LLVM IR DISPLAY:================--------------" << std::endl;
            llvmCodegen.print();
        }

        llvmCodegen.optimize();

        if (config.displayOptimizedLLVMIR) {
            std::cout << "--------------================:DEBUG: Optimized LLVM IR DISPLAY:================--------------" << std::endl;
            llvmCodegen.print();
        }

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        auto JIT = llvm::cantFail(llvm::orc::LLJITBuilder().create());

        auto& JD = JIT->getMainJITDylib();
        llvm::orc::SymbolMap runtimeSymbols;

        runtimeSymbols[JIT->mangleAndIntern("__alloc")] = {llvm::orc::ExecutorAddr::fromPtr(&__alloc), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__free")] = { llvm::orc::ExecutorAddr::fromPtr(&__free), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("create_string")] = { llvm::orc::ExecutorAddr::fromPtr(&create_string), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("free_string")] = { llvm::orc::ExecutorAddr::fromPtr(&free_string), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("concat_string")] = { llvm::orc::ExecutorAddr::fromPtr(&concat_string), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__print")] = { llvm::orc::ExecutorAddr::fromPtr(&__print), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__println")] = { llvm::orc::ExecutorAddr::fromPtr(&__println), llvm::JITSymbolFlags::Exported };

        llvm::cantFail(JD.define(llvm::orc::absoluteSymbols(runtimeSymbols)));

        auto TSCtx = llvm::orc::ThreadSafeContext(llvmCodegen.releaseContext());

        for (auto mod: llvmCodegen.getModules()) {
            if (mod->ID == "__main") {
                auto module = mod;
                llvm::Module* rawModule = module->content; 
                auto modulePtr = std::unique_ptr<llvm::Module>(rawModule);
                auto TSM = llvm::orc::ThreadSafeModule(
                    std::move(modulePtr),
                    TSCtx
                );
                llvm::cantFail(JIT->addIRModule(std::move(TSM)));
                break;
            }
        }

        auto mainSymbol = llvm::cantFail(JIT->lookup("main"));
        auto sakuraMain = mainSymbol.toPtr<int(*)()>();
        auto resultVal = sakuraMain();
        std::cout << "Result: " << resultVal << std::endl;
    }
}

#endif // !SAKURAE_ATRI_COMMANDS_HPP