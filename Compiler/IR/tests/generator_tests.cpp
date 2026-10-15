#include "Compiler/IR/Semantic/generator.hpp"
#include "Compiler/IR/Semantic/model/struct.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

void testSemanticGenerationDoesNotLowerStructs() {
    using namespace sakuraE::IR;
    IRGenerator generator("semantic-only-construction");
    generator.startGenerate(
        "struct Node { next: Node* } "
        "func keep(node: Node?) -> Node? { let local: Node? = null; return node; } "
        "func main() -> i32 { let array = [1, 2]; let size = sizeof(1); return 0; }",
        "semantic-only-construction");
    assert(generator.getProgram().curMod()->lookupStructType("Node") == nullptr);
    generator.getProgram().toString();
    assert(generator.getProgram().curMod()->lookupStructType("Node") == nullptr);
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

void testFunctionReturnFollowsSemanticSignature() {
    using namespace sakuraE::IR;
    IRContext context;
    auto& types = context.typeInfoPool();
    auto* integer = types.makeBasicTypeID(Int32);
    Function function("semantic-return", {1, 1, "function-signature"});
    function.setFunctionType(types.makeFunctionTypeID(integer, {}));
    assert(function.getReturnType() == integer);
}

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
    assert(program.curMod()->lookupStructType("Node") == nullptr);
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

    IRGenerator defaultGenerator("default-generator-test");
    defaultGenerator.startGenerate(
        "struct DefaultNode { next: DefaultNode? = null }",
        "default-generator-test");
    auto* defaultDecl = defaultGenerator.getProgram().curMod()->lookupStruct("DefaultNode");
    assert(defaultDecl != nullptr);
    assert(defaultDecl->find("next") != nullptr);
    assert(defaultDecl->hasDefaultValue("next"));
    assert(defaultDecl->getDefaultValue("next")->isNullHandle());

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

    IRGenerator argumentNullGenerator("argument-null-test");
    argumentNullGenerator.startGenerate(
        "struct ArgumentNode { next: ArgumentNode* } "
        "func consume(node: ArgumentNode?) -> i32 { return 0; } "
        "func main() -> i32 { consume(null); return 0; }",
        "argument-null-test");

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

    class TestCallableValue: public CallableValue {
    public:
        explicit TestCallableValue(TypeInfo* type)
            : CallableValue(type, "function-value") {}
    } functionValue(function.getSemanticType());
    functionValue.setFunctionType(function.getSemanticType());
    assert(functionValue.getFunctionTypeInfo() == function.getFunctionTypeInfo());

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
