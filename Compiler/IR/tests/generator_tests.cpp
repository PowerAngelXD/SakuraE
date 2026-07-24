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
    assert(bMember->info.line == 1);
    assert(bMember->info.column == 12);

    return 0;
}
