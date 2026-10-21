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

// 测试用 JIT 会话：把 Program 当前模块完整走一遍 lowering（IRModule -> BackendModule）
// 与 LLVM 代码生成/优化，再交给 ORC LLJIT 执行，便于在进程内直接调用生成的函数。
class JITSession {
    std::unique_ptr<llvm::orc::LLJIT> jit;
public:
    explicit JITSession(IR::Program& program) {
        // 先把 IRModule lower 为 Backend IR，再由 LLVMCodeGenerator 生成 LLVM IR 并优化。
        auto* backend = program.lowering().lowerModule(*program.curMod());
        auto context = std::make_unique<llvm::LLVMContext>();
        Codegen::LLVMCodeGenerator emitter(*backend, *context);
        emitter.start();
        emitter.optimize();
        jit = llvm::cantFail(llvm::orc::LLJITBuilder().create());
        // 生成代码引用的运行时函数（分配、GC 作用域、运行时类型元数据、边界检查等）
        // 不经动态链接，直接以本进程中的地址注册为绝对符号。
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
        // 所有模块共享同一个 LLVMContext；把 llvm::Module 的所有权从 emitter 的
        // 模块包装中取出（置空 content 避免二次释放），转交给 JIT。
        auto sharedContext = llvm::orc::ThreadSafeContext(std::move(context));
        for (auto* module : emitter.getModules()) {
            std::unique_ptr<llvm::Module> owned(module->content);
            module->content = nullptr;
            llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(owned), sharedContext)));
        }
    }
    // 按符号名查找已 JIT 的函数，并转换为调用方指定的函数指针类型。
    template<typename T> T lookup(const char* name) {
        return llvm::cantFail(jit->lookup(name)).toPtr<T>();
    }
};

// 验证逻辑非 `!`：bool 参数在 JIT 中正确取反，且返回值是装箱(Boxed)的 Bool RuntimeValue；
// 同时验证对非 bool（i32）使用 `!` 会在语义阶段抛出 SakuraError。
void testLogicalNot() {
    IR::IRGenerator generator("not-jit");
    generator.startGenerate(
        "func invert(value: bool) -> bool { return !value; } "
        "func main() -> i32 { return 0; }", "__main");
    JITSession session(generator.getProgram());
    // 函数名会按参数类型做名字修饰，invert(bool) 对应符号 invert_bool；参数与返回值均为装箱值指针。
    auto invert = session.lookup<runtime::RuntimeValue*(*)(runtime::RuntimeValue*)>("invert_bool");
    runtime::RuntimeValue input{};
    input.type = runtime::RuntimeType::Bool;
    input.data.boolean = true;
    auto* result = invert(&input);
    assert(result->type == runtime::RuntimeType::Bool && !result->data.boolean);
    input.data.boolean = false;
    result = invert(&input);
    assert(result->type == runtime::RuntimeType::Bool && result->data.boolean);
    // 对 i32 取逻辑非应被语义检查拒绝。
    bool rejected = false;
    try {
        IR::IRGenerator invalid("not-invalid");
        invalid.startGenerate("func main() -> i32 { let value = 7; let wrong = !value; return 0; }", "__main");
    } catch (const SakuraError&) { rejected = true; }
    assert(rejected);
    // 清理测试期间分配的运行时值，避免影响后续测试。
    runtime::__gc_reset();
}

// 验证原始(Raw)结构体字段访问：结构体参数以原生内存布局传入（C++ 侧用同布局的 struct 模拟），
// 覆盖字段读、字段写回原内存、嵌套结构体字段、定长数组字段索引、bool 字段取反、
// string 字段索引/整体读取、原生指针字段解引用，以及条件分支内的字段写入。
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
    // 函数名经过修饰（前缀 + "_" + 参数类型信息），按前缀在模块函数列表中找到实际符号名。
    const auto symbol = [&](const char* prefix) {
        for (const auto* function : functions) {
            const std::string name = function->getName().c_str();
            if (name.starts_with(std::string(prefix) + "_")) return name;
        }
        throw std::runtime_error("Missing field test function");
    };
    // 与 SakuraE 中 Inner/Item 的 storage 布局一一对应的 C++ 结构体，用于检验字段偏移是否正确。
    struct Inner { std::int32_t amount; };
    struct Item { std::int32_t value; bool flag; Inner inner; std::int32_t values[2]; const char* text; char* pointer; };
    char character = 'q';
    Item item{7, true, {11}, {13, 17}, "abc", &character};
    auto read = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("read").c_str());
    auto* result = read(&item);
    assert(result->type == runtime::RuntimeType::I32 && result->data.i32 == 7);
    auto update = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("update").c_str());
    // 写字段必须写回调用方传入的原始内存，而不是副本。
    result = update(&item);
    assert(result->data.i32 == 19 && item.value == 19);
    auto nested = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("nested").c_str());
    result = nested(&item);
    assert(result->data.i32 == 23 && item.inner.amount == 23);
    auto indexed = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("indexed").c_str());
    result = indexed(&item);
    // 只修改 values[1]，相邻元素 values[0] 必须保持不变。
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
    // string 字段读取后装箱为 String RuntimeValue，内容与长度需与原 C 字符串一致。
    assert(result->type == runtime::RuntimeType::String && std::string(result->data.string.data) == "abc");
    assert(result->data.string.length == 3);
    auto conditional = session.lookup<runtime::RuntimeValue*(*)(Item*)>(symbol("conditional").c_str());
    result = conditional(&item);
    assert(result->data.i32 == 29 && item.value == 29);
    runtime::__gc_reset();
}

// 验证代码生成器遇到尚不支持的指令（此处手工构造 create_struct）时抛出 runtime_error，
// 且错误信息中包含指令名，而不是静默生成错误代码。
void testUnsupportedInstruction() {
    IR::Program program("unsupported");
    const PositionInfo info{1, 1, "unsupported"};
    program.buildModule("__main", info);
    auto* integer = program.getContext().typeInfoPool().makeBasicTypeID(IR::Int32);
    auto* function = static_cast<IR::Function*>(program.curMod()->buildFunction("main", integer, {}, info));
    // 直接在 IR 中插入一条后端不支持的 create_struct 指令。
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

// 验证 lowering 对结构体字段访问（gmem）的校验：字段不存在、或声明的结果类型与字段类型不符时
// 抛出 runtime_error；并验证 typeof 被延迟到代码生成阶段，后端尚不支持时报错且信息包含 "typeof"。
void testFieldValidationAndDeferredTypeof() {
    // 手工构造 `read(item: Item)` 中的 gmem 字段访问，resultType/selector 用于制造非法组合。
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
    // 字段不存在。
    invalidField(IR::Int32, "missing");
    // 字段存在但结果类型(bool)与字段类型(i32)不符。
    invalidField(IR::Bool, "value");
    IR::IRGenerator generator("deferred-typeof");
    // typeof 可以通过语义分析与 lowering，但 LLVM 代码生成时应被拒绝。
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
    // ORC JIT 需要本机目标与汇编打印器初始化。
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    testLogicalNot();
    testRawStructFields();
    testUnsupportedInstruction();
    testFieldValidationAndDeferredTypeof();
    // 释放全局常量池与数组池，避免进程退出时残留。
    IR::Constant::clearAll();
    IR::IRArray::clearArrayPool();
    std::cout << "logical-not, raw struct fields, unsupported instructions: passed\n";
}
