#include "Compiler/IR/generator.hpp"

#include <cassert>

int main() {
    using namespace sakuraE::IR;

    IRGenerator generator("generator-test");
    generator.startGenerate(
        "struct A { b: B } struct B { value: i32 }",
        "generator-test");

    const auto& statements = generator.getParsedStatements();
    assert(statements.size() == 2);

    auto* aType = generator.getProgram().curMod()->lookupStructType("A");
    auto* bType = generator.getProgram().curMod()->lookupStructType("B");
    assert(aType != nullptr);
    assert(bType != nullptr);
    assert(aType->isComplete());
    assert(bType->isComplete());

    const auto bMember = aType->findMember("b");
    assert(bMember.has_value());
    assert(bMember->type == bType);
    assert(bMember->semanticType != nullptr);
    assert(bMember->semanticType->isStruct());
    assert(!bMember->semanticType->isNullable());
    assert(bMember->info.line == 1);
    assert(bMember->info.column == 12);

    IRGenerator nullableGenerator("nullable-generator-test");
    nullableGenerator.startGenerate(
        "struct Node { next: Node? }",
        "nullable-generator-test");

    auto* nodeType = nullableGenerator.getProgram().curMod()->lookupStructType("Node");
    assert(nodeType != nullptr);
    const auto nextMember = nodeType->findMember("next");
    assert(nextMember.has_value());
    assert(nextMember->type == nodeType);
    assert(nextMember->semanticType != nullptr);
    assert(nextMember->semanticType->isStruct());
    assert(nextMember->semanticType->isNullable());
    assert(nextMember->semanticType->getBase()->toIRType() == nodeType);
    assert(nextMember->defaultValue == nullptr);

    IRGenerator defaultGenerator("default-generator-test");
    defaultGenerator.startGenerate(
        "struct DefaultNode { next: DefaultNode? = null }",
        "default-generator-test");
    auto* defaultNode = defaultGenerator.getProgram().curMod()->lookupStructType("DefaultNode");
    auto defaultMember = defaultNode->findMember("next");
    assert(defaultMember.has_value());
    std::cerr << defaultGenerator.getParsedStatements()[0]->toString().c_str() << "\n";
    assert(defaultMember->defaultValue != nullptr);
    assert(defaultMember->defaultValue->isNullHandle());

    IRGenerator nullableArrayGenerator("nullable-array-test");
    nullableArrayGenerator.startGenerate(
        "struct Container { values: i32[2]? = null } "
        "func consume(values: i32[2]?) -> i32 { return 0; } "
        "func getValues() -> i32[2]? { return null; } "
        "func main() -> i32 { let values: i32[2]? = null; consume(null); return 0; }",
        "nullable-array-test");
    auto* container = nullableArrayGenerator.getProgram().curMod()->lookupStructType("Container");
    auto valuesMember = container->findMember("values");
    assert(valuesMember.has_value());
    assert(valuesMember->semanticType != nullptr);
    assert(valuesMember->semanticType->isNullable());
    assert(valuesMember->semanticType->getBase()->isArray());
    assert(valuesMember->defaultValue != nullptr);
    assert(valuesMember->defaultValue->isNullHandle());

    IRGenerator semanticGenerator("semantic-type-test");
    semanticGenerator.startGenerate(
        "func main() -> i32 { let value: i32 = 1; return value; }",
        "semantic-type-test");
    bool sawTypeInfoConstant = false;
    for (auto* function : semanticGenerator.getProgram().curMod()->getFunctions()) {
        for (auto* block : function->getBlocks()) {
            for (auto* instruction : block->getInstructions()) {
                if (instruction->getKind() == OpKind::constant &&
                    instruction->getType()->getIRTypeID() == TypeInfoTyID) {
                    sawTypeInfoConstant = true;
                }
                assert(instruction->getSemanticType() != nullptr);
            }
        }
    }
    assert(sawTypeInfoConstant);

    IRGenerator argumentNullGenerator("argument-null-test");
    argumentNullGenerator.startGenerate(
        "struct ArgumentNode { next: ArgumentNode? } "
        "func consume(node: ArgumentNode?) -> i32 { return 0; } "
        "func main() -> i32 { consume(null); return 0; }",
        "argument-null-test");

    IRContext functionContext;
    NamingContext functionNames("function-semantic-test");
    const sakuraE::PositionInfo functionInfo {1, 1, "function semantic test"};
    functionNames.defineStruct("Node", {}, functionInfo);
    auto* functionNodeType = functionNames.lookupStructType("Node");
    auto* functionNodeTypeInfo = functionContext.typeInfoPool().makeStructTypeID(functionNodeType);
    auto* nullableFunctionNodeTypeInfo = functionContext.typeInfoPool().wrapTypeAsNullable(
        functionNodeTypeInfo, functionInfo);
    FormalParamsDefine functionParams {{"node", functionNodeType}};
    Function function(
        "keep", functionNodeType, functionParams, functionInfo,
        {nullableFunctionNodeTypeInfo}, nullableFunctionNodeTypeInfo);
    assert(function.getSemanticReturnType() == nullableFunctionNodeTypeInfo);
    assert(function.getSemanticParamTypes().size() == 1);
    assert(function.getSemanticParamTypes()[0] == nullableFunctionNodeTypeInfo);
    assert(function.getFuncSemanticSignature() != nullptr);
    assert(function.getFuncSemanticSignature()->returnType == nullableFunctionNodeTypeInfo);
    assert(function.getFuncSemanticSignature()->paramTypes[0] == nullableFunctionNodeTypeInfo);

    class TestCallableValue: public CallableValue {
    public:
        explicit TestCallableValue(IRType* type)
            : CallableValue(type, "function-value") {}
    } functionValue(function.getType());
    functionValue.setFuncSemanticSignature(function.getFuncSemanticSignature());
    assert(functionValue.getFuncSemanticSignature() == function.getFuncSemanticSignature());

    IRGenerator nullGenerator("null-generator-test");
    try {
        nullGenerator.startGenerate(
            "struct Node { next: Node? } "
            "func getNode() -> Node? { return null; }",
            "null-generator-test");
    }
    catch (const sakuraE::SakuraError& error) {
        std::cerr << error.toString().c_str();
        throw;
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

    return 0;
}
