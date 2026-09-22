#include "Compiler/IR/Semantic/generator.hpp"
#include "Compiler/IR/Semantic/model/struct.hpp"

#include <cassert>

int main() {
    using namespace sakuraE::IR;

    try {

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
    auto* functionNodeDecl = functionNames.lookupStructDecl("Node");
    auto* functionNodeTypeInfo = functionContext.typeInfoPool().makeStructTypeID(functionNodeDecl->getDeclId());
    auto* nullableFunctionNodeTypeInfo = functionContext.typeInfoPool().wrapTypeAsNullable(
        functionNodeTypeInfo, functionInfo);
    FormalParamsDefine functionParams {{"node", nullptr}};
    Function function(
        "keep", nullptr, functionParams, functionInfo,
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
