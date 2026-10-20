#include "Compiler/IR/Semantic/generator.hpp"
#include "Compiler/LLVMCodegen/LLVMCodegenerator.hpp"
#include "Runtime/gc.h"
#include "Runtime/rttype.h"
#include "Runtime/errors.h"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <cassert>
#include <cstring>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>

using namespace sakuraE;

class JITSession {
    std::unique_ptr<llvm::orc::LLJIT> jit;
public:
    explicit JITSession(IR::Program& program) {
        auto* backend = program.lowering().lowerModule(*program.curMod());
        auto context = std::make_unique<llvm::LLVMContext>();
        Codegen::LLVMCodeGenerator emitter(*backend, *context);
        emitter.start();
        emitter.optimize();
        jit = llvm::cantFail(llvm::orc::LLJITBuilder().create());
        llvm::orc::SymbolMap symbols;
        const auto bind = [&](const char* name, auto* function) {
            symbols[jit->mangleAndIntern(name)] = {llvm::orc::ExecutorAddr::fromPtr(function), llvm::JITSymbolFlags::Exported};
        };
        bind("__runtime_alloc_value", &runtime::__runtime_alloc_value);
        bind("__gc_enter_scope", &runtime::__gc_enter_scope);
        bind("__gc_leave_scope", &runtime::__gc_leave_scope);
        bind("__runtime_type_info_basic", &runtime::__runtime_type_info_basic);
        bind("__runtime_type_info_struct", &runtime::__runtime_type_info_struct);
        bind("__runtime_check_array_bounds", &runtime::__runtime_check_array_bounds);
        bind("strlen", &std::strlen);
        llvm::cantFail(jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(symbols)));
        auto sharedContext = llvm::orc::ThreadSafeContext(std::move(context));
        for (auto* module : emitter.getModules()) {
            std::unique_ptr<llvm::Module> owned(module->content);
            module->content = nullptr;
            llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(owned), sharedContext)));
        }
    }
    template<typename T> T lookup(const char* name) {
        return llvm::cantFail(jit->lookup(name)).toPtr<T>();
    }
};

void testLogicalNot() {
    IR::IRGenerator generator("not-jit");
    generator.startGenerate(
        "func invert(value: bool) -> bool { return !value; } "
        "func main() -> i32 { return 0; }", "__main");
    JITSession session(generator.getProgram());
    auto invert = session.lookup<runtime::RuntimeValue*(*)(runtime::RuntimeValue*)>("invert_bool");
    runtime::RuntimeValue input{};
    input.type = runtime::RuntimeType::Bool;
    input.data.boolean = true;
    auto* result = invert(&input);
    assert(result->type == runtime::RuntimeType::Bool && !result->data.boolean);
    input.data.boolean = false;
    result = invert(&input);
    assert(result->type == runtime::RuntimeType::Bool && result->data.boolean);
    bool rejected = false;
    try {
        IR::IRGenerator invalid("not-invalid");
        invalid.startGenerate("func main() -> i32 { let value = 7; let wrong = !value; return 0; }", "__main");
    } catch (const SakuraError&) { rejected = true; }
    assert(rejected);
    runtime::__gc_reset();
}

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    testLogicalNot();
    IR::Constant::clearAll();
    IR::IRArray::clearArrayPool();
    std::cout << "logical-not: passed\n";
}
