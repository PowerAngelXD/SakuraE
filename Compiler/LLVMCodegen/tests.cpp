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

void testRawStructFields() {
    IR::IRGenerator generator("fields-jit");
    generator.startGenerate(
        "struct Inner { amount: i32 } "
        "struct Item { value: i32, flag: bool, inner: Inner, values: i32[2], text: string, pointer: char* } "
        "func read(item: Item) -> i32 { return item.value; } "
        "func update(item: Item) -> i32 { item.value = 19; return item.value; } "
        "func nested(item: Item) -> i32 { item.inner.amount = 23; return item.inner.amount; } "
        "func indexed(item: Item) -> i32 { item.values[1] = 31; return item.values[1]; } "
        "func flipped(item: Item) -> bool { return !item.flag; } "
        "func letter(item: Item) -> char { return item.text[1]; } "
        "func native(item: Item) -> char { return item.pointer[0]; } "
        "func text(item: Item) -> string { return item.text; } "
        "func conditional(item: Item) -> i32 { if (item.flag) { item.value = 29; } return item.value; } "
        "func main() -> i32 { return 0; }", "__main");
    JITSession session(generator.getProgram());
    const auto& functions = generator.getProgram().curMod()->getFunctions();
    const auto symbol = [&](const char* prefix) {
        for (const auto* function : functions) {
            const std::string name = function->getName().c_str();
            if (name.starts_with(std::string(prefix) + "_")) return name;
        }
        throw std::runtime_error("Missing field test function");
    };
    struct Inner { std::int32_t amount; };
    struct Item { std::int32_t value; bool flag; Inner inner; std::int32_t values[2]; const char* text; char* pointer; };
    char character = 'q';
    Item item{7, true, {11}, {13, 17}, "abc", &character};
    auto read = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("read").c_str());
    auto* result = read(&item);
    assert(result->type == runtime::RuntimeType::I32 && result->data.i32 == 7);
    auto update = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("update").c_str());
    result = update(&item);
    assert(result->data.i32 == 19 && item.value == 19);
    auto nested = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("nested").c_str());
    result = nested(&item);
    assert(result->data.i32 == 23 && item.inner.amount == 23);
    auto indexed = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("indexed").c_str());
    result = indexed(&item);
    assert(result->data.i32 == 31 && item.values[0] == 13 && item.values[1] == 31);
    auto flipped = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("flipped").c_str());
    result = flipped(&item);
    assert(result->type == runtime::RuntimeType::Bool && !result->data.boolean);
    auto letter = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("letter").c_str());
    result = letter(&item);
    assert(result->type == runtime::RuntimeType::I8 && result->data.i8 == 'b');
    auto native = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("native").c_str());
    result = native(&item);
    assert(result->type == runtime::RuntimeType::I8 && result->data.i8 == 'q');
    auto text = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("text").c_str());
    result = text(&item);
    assert(result->type == runtime::RuntimeType::String && std::string(result->data.string.data) == "abc");
    assert(result->data.string.length == 3);
    auto conditional = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("conditional").c_str());
    result = conditional(&item);
    assert(result->data.i32 == 29 && item.value == 29);
    runtime::__gc_reset();
}

void testUnsupportedInstruction() {
    IR::Program program("unsupported");
    const PositionInfo info{1, 1, "unsupported"};
    program.buildModule("__main", info);
    auto* integer = program.getContext().typeInfoPool().makeBasicTypeID(IR::Int32);
    auto* function = static_cast<IR::Function*>(program.curMod()->buildFunction("main", integer, {}, info));
    function->curBlock()->createInstruction(IR::OpKind::create_struct, integer, {}, "unsupported.create_struct");
    function->curBlock()->createReturn(IR::Constant::get(std::int32_t{0}, program.getContext().typeInfoPool(), info));
    auto* backend = program.lowering().lowerModule(*program.curMod());
    llvm::LLVMContext context;
    bool rejected = false;
    try {
        Codegen::LLVMCodeGenerator emitter(*backend, context);
        emitter.start();
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("create_struct") != std::string::npos;
    }
    assert(rejected);
}

void testFieldValidationAndDeferredTypeof() {
    const auto invalidField = [](IR::TypeID resultType, const char* selector) {
        IR::Program program("invalid-field");
        const PositionInfo info{1, 1, "invalid-field"};
        program.buildModule("__main", info);
        auto& types = program.getContext().typeInfoPool();
        auto* integer = types.makeBasicTypeID(IR::Int32);
        program.curMod()->declareAndImplStruct("Item", {{"value", integer, info}}, info);
        auto* item = types.makeStructTypeID(program.curMod()->lookupStruct("Item")->getDeclId());
        auto* function = static_cast<IR::Function*>(program.curMod()->buildFunction("read", integer, {{"item", item}}, info));
        auto* base = function->curBlock()->createInstruction(IR::OpKind::param, item, {}, "param.item");
        function->curBlock()->createInstruction(IR::OpKind::gmem, types.makeBasicTypeID(resultType),
            {base, IR::Constant::get(fzlib::String(selector), types, info)}, "field");
        bool rejected = false;
        try { program.lowering().lowerModule(*program.curMod()); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    };
    invalidField(IR::Int32, "missing");
    invalidField(IR::Bool, "value");
    IR::IRGenerator generator("deferred-typeof");
    generator.startGenerate("func main() -> i32 { let value = 1; let type = typeof(value); return 0; }", "__main");
    auto* backend = generator.getProgram().lowering().lowerModule(*generator.getProgram().curMod());
    llvm::LLVMContext context;
    bool rejected = false;
    try {
        Codegen::LLVMCodeGenerator emitter(*backend, context);
        emitter.start();
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("typeof") != std::string::npos;
    }
    assert(rejected);
}

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    testLogicalNot();
    testRawStructFields();
    testUnsupportedInstruction();
    testFieldValidationAndDeferredTypeof();
    IR::Constant::clearAll();
    IR::IRArray::clearArrayPool();
    std::cout << "logical-not, raw struct fields, unsupported instructions: passed\n";
}
