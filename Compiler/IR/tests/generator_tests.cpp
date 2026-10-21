#include "Compiler/IR/Semantic/model/module.hpp"
#include "Compiler/IR/Semantic/generator.hpp"
#include "Compiler/IR/Semantic/model/struct.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

// 验证语义 IR 生成与 lowering 分离：生成（以及 toString 打印）期间不会创建结构体的 storage 类型(IRType)；
// 每条指令及其常量操作数都带有属于所在 Program 上下文的语义类型(TypeInfo)；sizeof 的类型操作数是语义类型。
void testSemanticGenerationDoesNotLowerStructs() {
    using namespace sakuraE::IR;
    IRGenerator generator("semantic-only-construction");
    generator.startGenerate(
        "struct Node { next: Node* } "
        "func keep(node: Node?) -> Node? { let local: Node? = null; return node; } "
        "func main() -> i32 { let array = [1, 2]; let size = sizeof(1); return 0; }",
        "semantic-only-construction");
    // 生成结束后 NamingContext 中尚无 Node 的 storage 类型。
    assert(generator.getProgram().curMod()->structRegistry().lookupStructType("Node") == nullptr);
    // 打印 IR 也不能触发 lowering。
    generator.getProgram().toString();
    assert(generator.getProgram().curMod()->structRegistry().lookupStructType("Node") == nullptr);
    bool sawParam = false;
    bool sawAlloca = false;
    bool sawSizeof = false;
    for (auto* function : generator.getProgram().curMod()->getFunctions()) {
        for (auto* block : function->getBlocks()) {
            for (auto* instruction : block->getInstructions()) {
                assert(instruction->getSemanticType());
                assert(&instruction->getSemanticType()->getContext() == &generator.getProgram().getContext());
                sawParam |= instruction->getKind() == OpKind::param;
                sawAlloca |= instruction->getKind() == OpKind::create_alloca;
                if (instruction->getKind() == OpKind::_sizeof) {
                    sawSizeof = true;
                    assert(instruction->getTypeOperand()->getTypeID() == Int32);
                }
                for (auto* operand : instruction->getOperands()) {
                    if (auto* constant = dynamic_cast<Constant*>(operand)) {
                        assert(&constant->getSemanticType()->getContext() == &generator.getProgram().getContext());
                    }
                }
            }
        }
    }
    assert(sawParam && sawAlloca && sawSizeof);
}

// 验证常量与控制指令的上下文隔离：常量按 (值, IRContext) 缓存复用，null 句柄按 nullable 类型区分；
// 不同 IRContext 的同值常量是不同实例；类型描述常量保存语义类型本身；作用域/返回指令的语义类型为 void。
void testSemanticConstantAndControlContextIsolation() {
    using namespace sakuraE::IR;
    const sakuraE::PositionInfo info {1, 1, "semantic-context-isolation"};
    IRContext first;
    auto& types = first.typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    auto* firstLiteral = Constant::get(std::int32_t{37}, types, info);
    assert(firstLiteral == Constant::get(std::int32_t{37}, types, info));
    assert(firstLiteral->getSemanticType() == integer);
    auto* array = types.makeArrayTypeID(integer, 2);
    auto* nullableArray = types.wrapTypeAsNullable(array, info);
    auto* nullArray = Constant::getNullHandle(nullableArray, info);
    auto* nullableOther = types.wrapTypeAsNullable(types.makeArrayTypeID(integer, 3), info);
    assert(nullArray == Constant::getNullHandle(nullableArray, info));
    assert(nullArray != Constant::getNullHandle(nullableOther, info));
    assert(nullArray->getSemanticType() == nullableArray);
    Function function("control", integer, info);

    IRContext second;
    auto* secondLiteral = Constant::get(std::int32_t{37}, second.typeInfoPool(), info);
    assert(secondLiteral != firstLiteral);
    assert(secondLiteral->getSemanticType() == second.typeInfoPool().makeBasicTypeID(Int32));
    auto* firstDefault = Constant::getDefault(integer, info);
    assert(firstDefault->getSemanticType() == integer);
    auto* descriptor = Constant::get(nullableArray, info);
    assert(&descriptor->getSemanticType()->getContext() == &first);
    assert(descriptor->getContentValue<TypeInfo*>() == nullableArray);
    function.buildBlock("entry");
    function.curBlock()->createEnterScope();
    function.curBlock()->createReturn();
    function.curBlock()->createLeaveScope();
    for (auto* instruction : function.curBlock()->getInstructions()) {
        assert(instruction->getSemanticType() == types.makeBasicTypeID(Void));
    }
}

// 验证生成器产生的字面量（整数、自增、字符串）都使用所属 Program 的 IRContext，而非其他生成器的上下文。
void testGeneratorLiteralsUseOwningProgramContext() {
    using namespace sakuraE::IR;
    IRGenerator first("first-generator");
    IRGenerator second("second-generator");
    first.startGenerate(
        "func main() -> i32 { let value = 1; value++; let text = \"text\"; return 0; }",
        "first-generator");
    for (auto* function : first.getProgram().curMod()->getFunctions()) {
        for (auto* block : function->getBlocks()) {
            for (auto* instruction : block->getInstructions()) {
                assert(&instruction->getSemanticType()->getContext() == &first.getProgram().getContext());
            }
        }
    }
}

// 验证函数返回类型取自设置的语义函数签名。
void testFunctionReturnFollowsSemanticSignature() {
    using namespace sakuraE::IR;
    IRContext context;
    auto& types = context.typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    Function function("semantic-return", {1, 1, "function-signature"});
    function.setFunctionType(types.makeFunctionTypeID(integer, {}));
    assert(function.getReturnType() == integer);
}

// 验证函数签名更新与校验：setFunctionType 同步形参类型与打印结果；参数个数不符、空类型、
// 外部 IRContext 的类型都会抛 invalid_argument 且失败时签名保持不变（原子性）；
// Program::reset 后 LoweringSession 实例保持不变且仍可 lowering。
void testFunctionSignatureUpdatesAndValidation() {
    using namespace sakuraE::IR;
    IRContext context;
    auto& types = context.typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    auto* boolean = types.makeBasicTypeID(Bool);
    const sakuraE::PositionInfo info {1, 1, "function-signature"};
    Function function("convert", integer, {{"value", boolean}}, info);
    assert(function.toString() == "func convert(value: bool) -> i32 {}");
    function.setFunctionType(types.makeFunctionTypeID(boolean, {integer}));
    assert(function.getFormalParams()[0].second == integer);
    assert(function.getParamsOnlyType()[0] == integer);
    assert(function.toString() == "func convert(value: i32) -> bool {}");

    const auto rejects = [](auto action) {
        try { action(); }
        catch (const std::invalid_argument&) { return true; }
        return false;
    };
    // 记录失败前的签名，用于验证失败不会留下部分修改。
    auto* signature = function.getSemanticType();
    assert(rejects([&] { function.setFunctionType(types.makeFunctionTypeID(integer, {})); }));
    assert(rejects([&] { function.setFuncDefineInfo({{"invalid", nullptr}}, integer); }));
    assert(rejects([&] { function.setFuncDefineInfo({}, nullptr); }));
    IRContext foreign;
    auto* foreignInteger = foreign.typeInfoPool().makeBasicTypeID(Int32);
    assert(rejects([&] { function.setFuncDefineInfo({{"foreign", foreignInteger}}, integer); }));
    assert(function.getSemanticType() == signature);
    assert(function.getFormalParams()[0].first == "value");
    function.setFuncDefineInfo({}, integer);
    assert(function.getFormalParams().empty());
    assert(function.getReturnType() == integer);

    // Program 持有唯一的 LoweringSession；reset 不重建它。
    Program program("signature-session");
    auto* session = &program.lowering();
    program.buildModule("functions", info);
    auto* result = program.getContext().typeInfoPool().makeBasicTypeID(Int32);
    auto* defined = static_cast<Function*>(program.curMod()->buildFunction("answer", result, {}, info));
    assert(program.curMod()->lookup("answer")->getSemanticType() == defined->getSemanticType());
    program.reset();
    assert(&program.lowering() == session);
    assert(program.lowering().lowerType(defined->getReturnType()) == program.getContext().getInt32Ty());
}

// 验证语义层运算的类型推导：取地址(gaddr)得到指针或 ref、解引用得到元素类型、重载按参数类型选择；
// 并验证非法运算（解引用 string、bool 下标、缺失/多余返回值、对 string 元素取地址、实参类型不符）报 SakuraError。
void testSemanticOperations() {
    using namespace sakuraE::IR;
    IRGenerator generator("semantic-operations");
    generator.startGenerate(
        "func pick(value: i32) -> i32 { return value; } "
        "func pick(value: i32*) -> i32 { return *value; } "
        "struct Item { value: i32 } "
        "func read(item: Item) -> i32 { return item.value; } "
        "func main() -> i32 { let value = 7; let pointer = &value; "
        "let array = [1, 2]; let text = \"text\"; let character = text[0]; "
        "let element = array[0]; let size = sizeof(pointer); "
        "let description = typeof(value); "
        "let reference = ref array; let refElement = reference[0]; "
        "let result = pick(pointer); println(result); return result; }",
        "semantic-operations");
    bool sawPointer = false;
    bool sawDeref = false;
    for (auto* function : generator.getProgram().curMod()->getFunctions()) {
        for (auto* block : function->getBlocks()) {
            for (auto* instruction : block->getInstructions()) {
                assert(instruction->getSemanticType() != nullptr);
                if (instruction->getKind() == OpKind::gaddr) {
                    if (instruction->getSemanticType()->isPointer()) {
                        sawPointer = true;
                        assert(instruction->getSemanticType()->getPointeeType()->getTypeID() == TypeID::Int32);
                    }
                    else {
                        assert(instruction->getSemanticType()->isRef());
                        assert(instruction->getSemanticType()->getPointeeType()->isArray());
                    }
                }
                if (instruction->getKind() == OpKind::deref) {
                    sawDeref = true;
                    assert(instruction->getSemanticType()->getTypeID() == TypeID::Int32);
                }
            }
        }
    }
    assert(sawPointer && sawDeref);
    // 直接用一个语义可调用值调用 visitCallingOpNode，验证调用结果类型取自函数签名的返回类型。
    auto* integer = generator.getProgram().getContext().typeInfoPool().makeBasicTypeID(Int32);
    auto* signature = generator.getProgram().getContext().typeInfoPool().makeFunctionTypeID(integer, {integer});
    IRValue semanticCallable(signature, "semantic-callable");
    IRValue argument(integer, "semantic-argument");
    generator.getProgram().curMod()->curFunc()->buildBlock("semantic-call-verification");
    auto* call = generator.visitCallingOpNode(&semanticCallable, generator.getParsedStatements().front(), {&argument});
    assert(call->getSemanticType() == integer);
    const auto rejected = [](const fzlib::String& source) {
        try {
            IRGenerator invalid("invalid-semantic");
            invalid.startGenerate(source, "invalid-semantic");
        }
        catch (const sakuraE::SakuraError&) {
            return true;
        }
        return false;
    };
    assert(rejected("func main() -> i32 { let text = \"text\"; return *text; }"));
    assert(rejected("func main() -> i32 { let array = [1, 2]; return array[true]; }"));
    assert(rejected("func value() -> i32 { return; }"));
    assert(rejected("func value() -> void { return 1; }"));
    assert(rejected("func main() -> i32 { let text = \"text\"; let address = &text[0]; return 0; }"));
    assert(rejected("func take(value: i32) -> i32 { return value; } func main() -> i32 { return take(true); }"));
}

// 验证语义类型(TypeInfo)构造的接受/拒绝规则：复合 TypeID 不能当基本类型构造、空元素与外部上下文类型被拒绝、
// 模块重复构建被拒绝；结构体类型按 StructDeclId 去重；validateSemanticType 不触发 lowering，
// 但会拒绝未知或间接引用未知 StructDeclId 的类型；函数类型不可 nullable；非空可赋给 nullable 反之不行。
void testSemanticTypeAcceptance() {
    using namespace sakuraE::IR;
    Program program("type-acceptance");
    const sakuraE::PositionInfo info {1, 1, "Type acceptance"};
    program.buildModule("types", info);
    auto& types = program.getContext().typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    const auto rejects = [](auto action) {
        try { action(); }
        catch (const std::exception&) { return true; }
        return false;
    };
    for (const auto composite : {Array, Pointer, Ref, Struct, FunctionType}) {
        assert(rejects([&] { types.makeBasicTypeID(composite); }));
    }
    assert(rejects([&] { types.makeArrayTypeID(nullptr, 2); }));
    assert(rejects([&] { types.makePointerTypeID(nullptr); }));
    assert(rejects([&] { types.makeFunctionTypeID(integer, {nullptr}); }));
    IRContext foreign;
    auto* foreignInteger = foreign.typeInfoPool().makeBasicTypeID(Int32);
    assert(rejects([&] { types.makeRefTypeID(foreignInteger); }));
    assert(rejects([&] { program.validateSemanticType(foreignInteger, info); }));
    assert(rejects([&] { program.buildModule("types", info); }));
    program.curMod()->declareStruct("Node", info);
    auto* declaration = program.curMod()->lookupStruct("Node");
    auto* node = types.makeStructTypeID(declaration->getDeclId());
    assert(node == types.makeStructTypeID(declaration->getDeclId()));
    auto* pointer = types.makePointerTypeID(node);
    assert(pointer->getPointeeType() == node);
    auto* array = types.makeArrayTypeID(pointer, 3);
    assert(array->getElementType() == pointer);
    auto* signature = types.makeFunctionTypeID(node, {array});
    assert(signature->getFunctionTypeInfo()->getReturnType() == node);
    assert(signature->getFunctionTypeInfo()->getArgTypes()[0] == array);
    program.curMod()->implStruct("Node", {{"next", pointer, info}}, {}, info);
    program.validateSemanticType(signature, info);
    // 语义校验不会创建 storage 类型。
    assert(program.curMod()->structRegistry().lookupStructType("Node") == nullptr);
    assert(rejects([&] { declaration->complete({}); }));
    assert(rejects([&] { program.curMod()->declareStruct("Node", info); }));
    auto* missing = types.makeStructTypeID({"unknown-module", 0});
    assert(rejects([&] { program.validateSemanticType(missing, info); }));
    program.curMod()->declareStruct("Broken", info);
    program.curMod()->implStruct("Broken", {{"value", types.makeArrayTypeID(types.makePointerTypeID(missing), 2), info}}, {}, info);
    auto* broken = types.makeStructTypeID(program.curMod()->lookupStruct("Broken")->getDeclId());
    assert(rejects([&] { program.validateSemanticType(broken, info); }));
    assert(rejects([&] { types.wrapTypeAsNullable(signature, info); }));
    auto* nullable = types.wrapTypeAsNullable(node, info);
    assert(isAssignableTo(node, nullable));
    assert(!isAssignableTo(nullable, node));
}

// 验证仓库 test/ 下的若干 .sak 示例能完整通过语义生成。
void testSemanticFixtures() {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    for (const auto* fixture : {"sizeof_basic.sak", "test3.sak", "gc_pass_argument.sak"}) {
        std::ifstream input(root / "test" / fixture);
        assert(input.is_open());
        std::stringstream source;
        source << input.rdbuf();
        sakuraE::IR::IRGenerator generator(fixture);
        generator.startGenerate(source.str(), fixture);
    }
}

// 验证 IRModule -> BackendModule lowering 的身份与补全（对应 next-plan“测试与验收”的缓存/opaque 补全条目）：
// 重复 lowering 返回同一 BackendModule/BackendFunction；opaque 结构体在后续实现后原地补全；
// 语义声明保持语义类型；不同 Program 不能 lower 该模块。
void testBackendModuleIdentityAndCompletion() {
    using namespace sakuraE::IR;
    const sakuraE::PositionInfo info {1, 1, "backend-completion"};
    Program program("backend-completion");
    program.buildModule("definitions", info);
    auto* source = program.curMod();
    source->declareStruct("Unused", info);
    const auto id = source->lookupStruct("Unused")->getDeclId();
    auto* integer = program.getContext().typeInfoPool().makeBasicTypeID(Int32);
    auto* function = static_cast<Function*>(source->buildFunction("answer", integer, {}, info));
    function->curBlock()->createReturn(Constant::get(std::int32_t{42}, program.getContext().typeInfoPool(), info));

    auto& lowering = program.lowering();
    auto* backend = lowering.lowerModule(*source);
    auto* unused = backend->structTypes.at(id);
    auto* answer = lowering.lowerFunction(*function);
    assert(!unused->isComplete());
    assert(backend->lookup("answer") == answer);
    // 实现结构体后再次 lowering：缓存命中，原 IRStructType 被补全而非替换。
    source->implStruct("Unused", {{"value", integer, info}}, {}, info);
    assert(lowering.lowerModule(*source) == backend);
    assert(lowering.lowerFunction(*function) == answer);
    assert(backend->structTypes.at(id) == unused);
    assert(unused->isComplete());
    assert(unused->findMember("value")->type == program.getContext().getInt32Ty());
    assert(source->lookupStruct("Unused")->getFields()[0].type == integer);
    backend->verify();

    // 其他 Program（不同 IRContext）的 LoweringSession 不得 lower 本模块。
    Program foreign("backend-foreign");
    bool rejected = false;
    try { foreign.lowering().lowerModule(*source); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

// 验证模块导入图的 lowering：菱形依赖及环（leaf 反向 use root）下同一导入函数只出现一次、
// 链接名不变；导入同名函数产生歧义时 lowering 每次都失败，且失败不会污染已缓存的 BackendModule。
void testBackendImportGraphAndConflicts() {
    using namespace sakuraE::IR;
    const sakuraE::PositionInfo info {1, 1, "backend-imports"};
    Program program("backend-imports");
    auto* integer = program.getContext().typeInfoPool().makeBasicTypeID(Int32);
    program.buildModule("leaf", info);
    auto* leaf = program.curMod();
    auto* function = static_cast<Function*>(leaf->buildFunction("shared", integer, {}, info));
    function->curBlock()->createReturn(Constant::get(std::int32_t{7}, program.getContext().typeInfoPool(), info));
    program.buildModule("left", info);
    auto* left = program.curMod();
    left->use(leaf);
    program.buildModule("right", info);
    auto* right = program.curMod();
    right->use(leaf);
    program.buildModule("root", info);
    auto* root = program.curMod();
    root->use(left);
    root->use(right);
    // 制造导入环。
    leaf->use(root);
    auto& lowering = program.lowering();
    auto* backend = lowering.lowerModule(*root);
    assert(backend->lookup("shared") == lowering.lowerFunction(*function));
    assert(backend->lookup("shared")->linkageName == "shared");
    std::size_t sharedImports = 0;
    for (const auto* imported : backend->imports) sharedImports += imported->source == function;
    assert(sharedImports == 1);
    assert(lowering.lowerModule(*root) == backend);
    backend->verify();

    program.buildModule("conflicting", info);
    auto* conflicting = program.curMod();
    conflicting->buildFunction("shared", integer, {}, info);
    program.buildModule("ambiguous", info);
    auto* ambiguous = program.curMod();
    ambiguous->use(leaf);
    ambiguous->use(conflicting);
    // 重复尝试以确认失败没有留下半成品缓存。
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool rejected = false;
        try { lowering.lowerModule(*ambiguous); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    assert(lowering.lowerModule(*root) == backend);
    assert(backend->lookup("shared")->source == function);
}

// 验证 Backend IR 的 ABI 与 GC root 决策：运行时函数的参数/返回值为原始(Raw)或装箱(Boxed)，
// 用户函数（main 除外）返回装箱值且首个参数是 GC root；数组 alloca 以槽位作为 root 而非值本身；
// 取地址结果不被追踪为 root；sizeof 的类型操作数对应指针运行时类型描述。
void testBackendABIAndGCRootDecisions() {
    using namespace sakuraE::IR;
    IRGenerator generator("backend-abi");
    generator.startGenerate(
        "func keep(text: string) -> string { let saved = text; return saved; } "
        "func main() -> i32 { let array = [1, 2]; let pointer = &array; "
        "let size = sizeof(pointer); let description = typeof(size); "
        "println(keep(\"alive\")); return 0; }", "backend-abi");
    auto& program = generator.getProgram();
    auto* backend = program.lowering().lowerModule(*program.curMod());
    const auto* allocate = backend->lookup("__gc_alloc");
    const auto* print = backend->lookup("println");
    const auto* createString = backend->lookup("create_string");
    assert(allocate->abi.result == ABIValueKind::Raw);
    assert(allocate->abi.parameters[0] == ABIValueKind::Raw);
    assert(print->abi.result == ABIValueKind::Void);
    assert(print->abi.parameters[0] == ABIValueKind::Boxed);
    assert(createString->abi.result == ABIValueKind::Boxed);
    assert(createString->abi.parameters[0] == ABIValueKind::Raw);
    for (const auto& function : backend->functions) {
        if (function->sourceName != "main") {
            assert(function->abi.result == ABIValueKind::Boxed);
            assert(function->gcInfo.parameterRoots[0]);
        }
        for (const auto* block : function->source->getBlocks()) {
            for (const auto* instruction : block->getInstructions()) {
                auto* storage = function->storageType(instruction);
                if (instruction->getKind() == OpKind::create_alloca && storage->isArray()) {
                    assert(function->rootsSlot(instruction));
                    assert(!function->tracksRoot(instruction));
                }
                if (instruction->getKind() == OpKind::gaddr) {
                    assert(!function->tracksRoot(instruction));
                    assert(!function->rootsSlot(instruction));
                }
                if (instruction->getKind() == OpKind::_sizeof) {
                    assert(function->typeOperands.at(instruction)->kind == RuntimeTypeKind::Pointer);
                }
            }
        }
    }
    backend->verify();
}

// 验证运行时类型元数据(RuntimeTypeInfo)描述：string 与 char* 共享 storage 类型但运行时描述不同，
// 只有 string 参数是 GC root；数组描述带元素个数；nullable 数组的 null 常量表示为托管数组；
// 字符串字面量被追踪为 root；重复 lowering 函数时描述被缓存复用。
void testBackendRuntimeDescriptions() {
    using namespace sakuraE::IR;
    const sakuraE::PositionInfo info {1, 1, "backend-runtime-descriptions"};
    Program program("backend-runtime-descriptions");
    auto& types = program.getContext().typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    auto* text = types.makeBasicTypeID(String);
    auto* pointer = types.makePointerTypeID(types.makeBasicTypeID(Char));
    auto* array = types.makeArrayTypeID(integer, 2);
    program.buildModule("main", info);
    auto* source = static_cast<Function*>(program.curMod()->buildFunction("metadata", integer, {}, info));
    source->setFuncDefineInfo({{"text", text}, {"pointer", pointer}}, integer);
    auto* textDescriptor = Constant::get(text, info);
    auto* pointerDescriptor = Constant::get(pointer, info);
    auto* arrayDescriptor = Constant::get(array, info);
    auto* nullArray = Constant::getNullHandle(types.wrapTypeAsNullable(array, info), info);
    auto* literal = Constant::get(fzlib::String("alive"), types, info);
    for (auto* constant : {textDescriptor, pointerDescriptor, arrayDescriptor, nullArray, literal}) {
        source->curBlock()->createInstruction(OpKind::constant, constant->getSemanticType(), {constant}, "metadata.constant");
    }
    source->curBlock()->createReturn(Constant::get(std::int32_t{0}, types, info));
    auto* backend = program.lowering().lowerFunction(*source);
    assert(backend->parameters[0].second == backend->parameters[1].second);
    assert(backend->gcInfo.parameterRoots[0] && !backend->gcInfo.parameterRoots[1]);
    assert(backend->parameterRuntimeTypes[0]->tag == RuntimeValueTag::String);
    assert(backend->parameterRuntimeTypes[1]->tag == RuntimeValueTag::Pointer);
    const auto* textDescription = backend->typeOperands.at(textDescriptor);
    const auto* pointerDescription = backend->typeOperands.at(pointerDescriptor);
    assert(textDescription->storage == pointerDescription->storage);
    assert(textDescription != pointerDescription);
    assert(textDescription->kind == RuntimeTypeKind::String);
    assert(pointerDescription->kind == RuntimeTypeKind::Pointer);
    assert(pointerDescription->element->kind == RuntimeTypeKind::I8);
    assert(backend->typeOperands.at(arrayDescriptor)->count == 2);
    assert(backend->constants.at(nullArray) == ConstantKind::Null);
    assert(backend->runtimeType(nullArray).representation == ValueRepresentation::ManagedArray);
    assert(backend->constants.at(literal) == ConstantKind::String);
    assert(backend->tracksRoot(literal));
    assert(program.lowering().lowerFunction(*source) == backend);
    assert(backend->typeOperands.at(textDescriptor) == textDescription);
    backend->parent->verify();
}

// 入口：依次运行上述测试，再验证结构体声明顺序无关（A 先引用 B）、nullable 字段与默认 null 值、
// nullable 数组、类型描述常量、函数语义签名、以及非法 null 用法的报错。
int main() {
    using namespace sakuraE::IR;

    try {
    testSemanticGenerationDoesNotLowerStructs();
    testSemanticConstantAndControlContextIsolation();
    testGeneratorLiteralsUseOwningProgramContext();
    testFunctionReturnFollowsSemanticSignature();
    testFunctionSignatureUpdatesAndValidation();
    testSemanticTypeAcceptance();
    testSemanticOperations();
    testSemanticFixtures();
    testBackendModuleIdentityAndCompletion();
    testBackendImportGraphAndConflicts();
    testBackendABIAndGCRootDecisions();
    testBackendRuntimeDescriptions();

    // 结构体可前向引用后续声明的结构体，声明全部完成，lowering 结果缓存稳定。
    IRGenerator generator("generator-test");
    generator.startGenerate(
        "struct A { b: B } struct B { value: i32 }",
        "generator-test");

    const auto& statements = generator.getParsedStatements();
    assert(statements.size() == 2);

    auto* aDecl = generator.getProgram().curMod()->lookupStruct("A");
    auto* bDecl = generator.getProgram().curMod()->lookupStruct("B");
    assert(aDecl != nullptr);
    assert(bDecl != nullptr);
    assert(aDecl->isComplete());
    assert(bDecl->isComplete());

    auto& lowering = generator.getProgram().lowering();
    assert(&lowering == &generator.getProgram().lowering());
    auto* aType = generator.getProgram().getContext().typeInfoPool().makeStructTypeID(aDecl->getDeclId());
    assert(lowering.lowerType(aType) == lowering.lowerType(aType));

    const auto* bMember = aDecl->find("b");
    assert(bMember != nullptr);
    assert(bMember->type != nullptr);
    assert(bMember->type->isStruct());
    assert(!bMember->type->isNullable());
    assert(bMember->info.line == 1);
    assert(bMember->info.column == 12);

    // 自引用 nullable 字段，未写默认值时无默认值。
    IRGenerator nullableGenerator("nullable-generator-test");
    nullableGenerator.startGenerate(
        "struct Node { next: Node? }",
        "nullable-generator-test");

    auto* nodeDecl = nullableGenerator.getProgram().curMod()->lookupStruct("Node");
    assert(nodeDecl != nullptr);
    const auto* nextMember = nodeDecl->find("next");
    assert(nextMember != nullptr);
    assert(nextMember->type->isStruct());
    assert(nextMember->type->isNullable());
    assert(!nodeDecl->hasDefaultValue("next"));

    // 显式默认值 null 记录为 null 句柄。
    IRGenerator defaultGenerator("default-generator-test");
    defaultGenerator.startGenerate(
        "struct DefaultNode { next: DefaultNode? = null }",
        "default-generator-test");
    auto* defaultDecl = defaultGenerator.getProgram().curMod()->lookupStruct("DefaultNode");
    assert(defaultDecl != nullptr);
    assert(defaultDecl->find("next") != nullptr);
    assert(defaultDecl->hasDefaultValue("next"));
    assert(defaultDecl->getDefaultValue("next")->isNullHandle());

    // nullable 数组可用于字段、形参、返回值与局部变量，并接受 null。
    IRGenerator nullableArrayGenerator("nullable-array-test");
    nullableArrayGenerator.startGenerate(
        "struct Container { values: i32[2]? = null } "
        "func consume(values: i32[2]?) -> i32 { return 0; } "
        "func getValues() -> i32[2]? { return null; } "
        "func main() -> i32 { let values: i32[2]? = null; consume(null); return 0; }",
        "nullable-array-test");
    auto* containerDecl = nullableArrayGenerator.getProgram().curMod()->lookupStruct("Container");
    assert(containerDecl != nullptr);
    const auto* valuesMember = containerDecl->find("values");
    assert(valuesMember != nullptr);
    assert(valuesMember->type->isNullable());
    assert(valuesMember->type->getBase()->isArray());
    assert(containerDecl->hasDefaultValue("values"));
    assert(containerDecl->getDefaultValue("values")->isNullHandle());

    // 带显式类型注解的 let 会生成 TypeDescriptor 常量。
    IRGenerator semanticGenerator("semantic-type-test");
    semanticGenerator.startGenerate(
        "func main() -> i32 { let value: i32 = 1; return value; }",
        "semantic-type-test");
    bool sawTypeInfoConstant = false;
    for (auto* function : semanticGenerator.getProgram().curMod()->getFunctions()) {
        for (auto* block : function->getBlocks()) {
            for (auto* instruction : block->getInstructions()) {
                if (instruction->getKind() == OpKind::constant &&
                    instruction->getSemanticType()->getTypeID() == TypeDescriptor) {
                    sawTypeInfoConstant = true;
                }
                assert(instruction->getSemanticType() != nullptr);
            }
        }
    }
    assert(sawTypeInfoConstant);

    // null 可作为 nullable 结构体实参传递（生成不应抛错）。
    IRGenerator argumentNullGenerator("argument-null-test");
    argumentNullGenerator.startGenerate(
        "struct ArgumentNode { next: ArgumentNode* } "
        "func consume(node: ArgumentNode?) -> i32 { return 0; } "
        "func main() -> i32 { consume(null); return 0; }",
        "argument-null-test");

    // 直接构造 Function：语义签名保留 nullable 类型，打印时不会触发结构体 lowering。
    IRContext functionContext;
    NamingContext functionNames("function-semantic-test");
    const sakuraE::PositionInfo functionInfo {1, 1, "function semantic test"};
    functionNames.defineStruct("Node", {}, functionInfo);
    auto* functionNodeDecl = functionNames.lookupStructDecl("Node");
    auto* functionNodeTypeInfo = functionContext.typeInfoPool().makeStructTypeID(functionNodeDecl->getDeclId());
    auto* nullableFunctionNodeTypeInfo = functionContext.typeInfoPool().wrapTypeAsNullable(
        functionNodeTypeInfo, functionInfo);
    FormalParamsDefine functionParams {{"node", nullableFunctionNodeTypeInfo}};
    Function function(
        "keep", nullableFunctionNodeTypeInfo, functionParams, functionInfo);
    assert(function.getSemanticReturnType() == nullableFunctionNodeTypeInfo);
    assert(function.getSemanticParamTypes().size() == 1);
    assert(function.getSemanticParamTypes()[0] == nullableFunctionNodeTypeInfo);
    assert(function.getFunctionTypeInfo() != nullptr);
    assert(function.getFunctionTypeInfo()->getReturnType() == nullableFunctionNodeTypeInfo);
    assert(function.getFunctionTypeInfo()->getArgTypes()[0] == nullableFunctionNodeTypeInfo);
    function.toString();
    assert(functionNames.lookupStructType("Node") == nullptr);

    // CallableValue 设置函数类型后与函数共享同一 FunctionTypeInfo。
    class TestCallableValue: public CallableValue {
    public:
        explicit TestCallableValue(TypeInfo* type)
            : CallableValue(type, "function-value") {}
    } functionValue(function.getSemanticType());
    functionValue.setFunctionType(function.getSemanticType());
    assert(functionValue.getFunctionTypeInfo() == function.getFunctionTypeInfo());

    // 返回 null 给 nullable 结构体返回类型合法；若抛错则打印并失败。
    IRGenerator nullGenerator("null-generator-test");
    try {
        nullGenerator.startGenerate(
            "struct Node { next: Node* } "
            "func getNode() -> Node? { return null; }",
            "null-generator-test");
    }
    catch (const sakuraE::SakuraError& error) {
        std::cerr << error.toString().c_str();
        return 1;
    }

    // 无类型上下文的 null 无法推断类型。
    bool rejectedUntypedNull = false;
    try {
        IRGenerator invalidNull("invalid-null-test");
        invalidNull.startGenerate("func main() -> i32 { let value = null; return 0 }",
                                  "invalid-null-test");
    }
    catch (const sakuraE::SakuraError&) {
        rejectedUntypedNull = true;
    }
    assert(rejectedUntypedNull);

    // 非 nullable 类型不能接受 null。
    bool rejectedNonNullableNull = false;
    try {
        IRGenerator invalidNull("non-nullable-null-test");
        invalidNull.startGenerate(
            "struct Node {} func main() -> i32 { let value: Node = null; return 0; }",
            "non-nullable-null-test");
    }
    catch (const sakuraE::SakuraError&) {
        rejectedNonNullableNull = true;
    }
    assert(rejectedNonNullableNull);

    // null == null 的结果是 bool，不能赋给 nullable 结构体。
    bool rejectedCompositeNull = false;
    try {
        IRGenerator invalidNull("composite-null-test");
        invalidNull.startGenerate(
            "struct Node {} func main() -> i32 { let value: Node? = null == null; return 0; }",
            "composite-null-test");
    }
    catch (const sakuraE::SakuraError&) {
        rejectedCompositeNull = true;
    }
    assert(rejectedCompositeNull);

    }
    catch (const sakuraE::SakuraError& error) {
        std::cerr << error.toString().c_str() << '\n';
        return 1;
    }
    return 0;
}
