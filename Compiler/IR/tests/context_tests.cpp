#include "Compiler/IR/context.hpp"

#include <cassert>

int main() {
    using namespace sakuraE::IR;

    IRContext first;
    IRContext second;

    auto* firstInt32 = first.getInt32Ty();
    assert(firstInt32 == first.getInt32Ty());
    assert(first.getPointerTo(firstInt32) == first.getPointerTo(firstInt32));

    auto* secondInt32 = second.getInt32Ty();
    assert(firstInt32 != secondInt32);
    assert(&first.llvmContext() != &second.llvmContext());

    auto* firstTypeInfo = first.typeInfoPool().makeBasicTypeID(TypeID::Int32);
    assert(firstTypeInfo == first.typeInfoPool().makeBasicTypeID(TypeID::Int32));
    assert(firstTypeInfo != second.typeInfoPool().makeBasicTypeID(TypeID::Int32));

    NamingContext names("test-module");
    const sakuraE::PositionInfo definitionInfo {1, 1, "context test"};
    names.defineStruct(
        "Point",
        {{"x", first.getInt32Ty(), firstTypeInfo, definitionInfo},
         {"y", first.getInt32Ty(), firstTypeInfo, definitionInfo}},
        definitionInfo);

    assert(names.lookupStructDecl("Point") != nullptr);
    auto* pointType = names.lookupStructType("Point");
    assert(pointType != nullptr);

    auto* pointTypeInfo = first.typeInfoPool().makeStructTypeID(pointType);
    assert(pointTypeInfo->toIRType() == pointType);
    auto* nullablePointTypeInfo = first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo);
    assert(nullablePointTypeInfo != pointTypeInfo);
    assert(!pointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->getBase() == pointTypeInfo);
    assert(nullablePointTypeInfo == first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo));
    assert(nullablePointTypeInfo->toIRType() == pointTypeInfo->toIRType());
    assert(isAssignableTo(pointTypeInfo, pointTypeInfo));
    assert(isAssignableTo(nullablePointTypeInfo, nullablePointTypeInfo));
    assert(isAssignableTo(pointTypeInfo, nullablePointTypeInfo));
    assert(!isAssignableTo(nullablePointTypeInfo, pointTypeInfo));

    auto* pointArrayTypeInfo = first.typeInfoPool().makeArrayTypeID(pointTypeInfo, 4);
    auto* nullablePointArrayTypeInfo = first.typeInfoPool().wrapTypeAsNullable(pointArrayTypeInfo, definitionInfo);
    assert(nullablePointArrayTypeInfo->isArray());
    assert(nullablePointArrayTypeInfo->isNullable());
    assert(nullablePointArrayTypeInfo->getBase() == pointArrayTypeInfo);
    assert(nullablePointArrayTypeInfo->getElementType() == pointTypeInfo);
    assert(nullablePointArrayTypeInfo->toIRType() == pointArrayTypeInfo->toIRType());

    bool rejectedScalarNullable = false;
    try {
        first.typeInfoPool().wrapTypeAsNullable(firstTypeInfo, definitionInfo);
    }
    catch (const sakuraE::SakuraError&) {
        rejectedScalarNullable = true;
    }
    assert(rejectedScalarNullable);

    bool rejectedRepeatedNullable = false;
    try {
        first.typeInfoPool().wrapTypeAsNullable(nullablePointTypeInfo, definitionInfo);
    }
    catch (const sakuraE::SakuraError&) {
        rejectedRepeatedNullable = true;
    }
    assert(rejectedRepeatedNullable);

    assert(first.typeInfoPool().makePointerTypeID(pointTypeInfo)->toIRType()
           == first.getPointerTo(pointType));
    assert(first.typeInfoPool().makeArrayTypeID(pointTypeInfo, 4)->toIRType()
           == first.getArrayTy(pointType, 4));

    bool rejectedDuplicate = false;
    try {
        names.defineStruct("Point", {}, definitionInfo);
    }
    catch (const sakuraE::SakuraError&) {
        rejectedDuplicate = true;
    }
    assert(rejectedDuplicate);

    return 0;
}
