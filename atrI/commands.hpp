#ifndef SAKURAE_ATRI_COMMANDS_HPP
#define SAKURAE_ATRI_COMMANDS_HPP

#include <ctime>
#include <iostream>
#include <llvm/ExecutionEngine/Orc/CoreContainers.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <chrono>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include "Runtime/alloc.h"
#include "Runtime/gc.h"
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
        CompilerSessionGuard compilerSessionGuard;

        if (args.size() < 1) {
            fzlib::String content = "Invalid argument for command: 'run': ";
            for (auto arg: args) {
                content += arg + " ";
            }
            throw std::runtime_error(content.c_str());
        }

        auto content = readSourceFile(args[0]);
        bool isDebug = false;
        DebugConfig config;

        if (contains(args, "-ast")) { config.displayAST = true; isDebug = true; }
        if (contains(args, "-sakir")) { config.displaySakIR = true; isDebug = true; }
        if (contains(args, "-rawllvm")) { config.displayRawLLVMIR = true; isDebug = true; }
        if (contains(args, "-llvmir")) { config.displayOptimizedLLVMIR = true; isDebug = true; }

        std::ostringstream log;

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
                log << "--------------================:DEBUG: AST DISPLAY:================--------------" << std::endl;
                log << res->toFormatString() << std::endl;
            }

            generator.visitStmt(res);
            current = result.end;
        }

        if (config.displaySakIR) {
            log << "--------------================:DEBUG: SAKIR DISPLAY:================--------------" << std::endl;
            log << generator.toFormatString() << std::endl;
        }

        auto& program = generator.getProgram();

        sakuraE::Codegen::LLVMCodeGenerator llvmCodegen(&program);
        llvmCodegen.start();

        if (config.displayRawLLVMIR) {
            log << "--------------================:DEBUG: RAW LLVM IR DISPLAY:================--------------" << std::endl;
            log << llvmCodegen.toString() << std::endl;
        }

        llvmCodegen.optimize();

        if (config.displayOptimizedLLVMIR) {
            log << "--------------================:DEBUG: Optimized LLVM IR DISPLAY:================--------------" << std::endl;
            log << llvmCodegen.toString() << std::endl;
        }

        auto currentTime = std::format("{:%Y-%m-%d_%H-%M-%S}", std::chrono::system_clock::now());
        auto logPath = fzlib::String("log-" + std::string(currentTime.c_str()) + ".txt");
        if (isDebug) writeFile(logPath, log.str());

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
        runtimeSymbols[JIT->mangleAndIntern("__gc_alloc")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_alloc), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_collect")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_collect), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_enter_scope")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_enter_scope), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_leave_scope")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_leave_scope), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_pop")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_pop), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_register")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_register), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_get_atomic_type")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_get_atomic_type), llvm::JITSymbolFlags::Exported };
        runtimeSymbols[JIT->mangleAndIntern("__gc_get_array_type")] = { llvm::orc::ExecutorAddr::fromPtr(&sakuraE::runtime::__gc_get_array_type), llvm::JITSymbolFlags::Exported };

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
