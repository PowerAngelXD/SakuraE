#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Lowering/session.hpp"

#include <cassert>
#include <string_view>

void testStructLoweringLifecycle() {
    using namespace sakuraE::IR;
    IRContext first;
    NamingContext names("lifecycle-test");
    const sakuraE::PositionInfo definitionInfo {1, 1, "struct lifecycle test"};
    auto* firstTypeInfo = first.typeInfoPool().makeBasicTypeID(TypeID::Int32);
    assert(firstTypeInfo->isBasic());
    auto* signatureInfo = first.typeInfoPool().makeFunctionTypeID(firstTypeInfo, {firstTypeInfo});
    assert(signatureInfo->isFunction());
    assert(!signatureInfo->isBasic());
    assert(signatureInfo == first.typeInfoPool().makeFunctionTypeID(firstTypeInfo, {firstTypeInfo}));
    assert(std::get<FunctionTypeInfo>(signatureInfo->getComplexTypeInfo()).getArgTypes()[0] == firstTypeInfo);
    auto* firstInt32 = first.getInt32Ty();
    LoweringSession firstLowering(first, &names);

    const auto rejects = [](auto action) {
        try {
            action();
        }
        catch (const std::exception&) {
            return true;
        }
        return false;
    };

    names.declareOpaqueStruct("Forward", definitionInfo);
    auto* forwardInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Forward"));
    auto* forwardArrayInfo = first.typeInfoPool().makeArrayTypeID(forwardInfo, 2);
    auto* forwardPointerInfo = first.typeInfoPool().makePointerTypeID(forwardArrayInfo);
    auto* nullableForwardInfo = first.typeInfoPool().wrapTypeAsNullable(forwardInfo, definitionInfo);
    auto* forwardFunctionInfo = first.typeInfoPool().makeFunctionTypeID(forwardInfo, {forwardPointerInfo});
    auto* forwardFunction = firstLowering.lowerType(forwardFunctionInfo);
    auto* forwardPointer = firstLowering.lowerType(forwardPointerInfo);
    auto* forwardType = static_cast<IRStructType*>(firstLowering.lowerType(forwardInfo));
    auto* llvmForward = llvm::cast<llvm::StructType>(forwardType->toLLVMType(first.llvmContext()));
    assert(!forwardType->isComplete());
    assert(llvmForward->isOpaque());
    names.implStruct("Forward", {{"value", firstTypeInfo, definitionInfo}}, {}, definitionInfo);
    assert(firstLowering.lowerType(forwardPointerInfo) == forwardPointer);
    assert(firstLowering.lowerType(forwardFunctionInfo) == forwardFunction);
    assert(firstLowering.lowerType(nullableForwardInfo) == forwardType);
    assert(firstLowering.lowerType(forwardInfo) == forwardType);
    assert(forwardType->isComplete());
    assert(forwardType->findMember("value")->type == firstInt32);
    assert(forwardType->toLLVMType(first.llvmContext()) == llvmForward);
    assert(!llvmForward->isOpaque());
    assert(llvmForward->getElementType(0) == firstInt32->toLLVMType(first.llvmContext()));
    assert(rejects([&] { forwardType->complete({}); }));

    {
        LoweringSession temporary(first, &names);
        assert(temporary.lowerType(forwardInfo) == forwardType);
    }
    assert(names.lookupStructType("Forward") == forwardType);
    assert(names.getStructTy(first, names.lookupStructId("Forward")) == forwardType);
    assert(names.resolveNamingContext(names.lookupStructId("Forward")) == &names);
    assert(names.lookupStructType("Unknown") == nullptr);
    IRContext otherContext;
    LoweringSession wrongContext(otherContext, &names);
    assert(rejects([&] { wrongContext.lowerType(forwardInfo); }));
    NamingContext otherNames("other-module");
    otherNames.defineStruct("Forward", {{"value", firstTypeInfo, definitionInfo}}, definitionInfo);
    auto* otherForwardInfo = first.typeInfoPool().makeStructTypeID(otherNames.lookupStructId("Forward"));
    LoweringSession otherLowering(first, &otherNames);
    auto* otherForwardType = otherLowering.lowerType(otherForwardInfo);
    assert(otherForwardType != forwardType);
    assert(otherNames.lookupStructType("Forward") == otherForwardType);
    assert(otherForwardType->toLLVMType(first.llvmContext()) != llvmForward);
    assert(forwardType->findMember("value").has_value());

    names.declareOpaqueStruct("Node", definitionInfo);
    auto* nodeInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Node"));
    auto* nodePointerInfo = first.typeInfoPool().makePointerTypeID(nodeInfo);
    auto* nodeRefInfo = first.typeInfoPool().makeRefTypeID(nodeInfo);
    names.implStruct("Node", {{"next", nodePointerInfo, definitionInfo},
                               {"parent", nodeRefInfo, definitionInfo}}, {}, definitionInfo);
    auto* nodeType = static_cast<IRStructType*>(firstLowering.lowerType(nodeInfo));
    assert(nodeType->isComplete());
    assert(static_cast<IRPointerType*>(nodeType->findMember("next")->type)->getElementType() == nodeType);
    assert(static_cast<IRRefType*>(nodeType->findMember("parent")->type)->getElementType() == nodeType);
    assert(nodeType->toLLVMType(first.llvmContext())->isSized());

    names.declareOpaqueStruct("Outer", definitionInfo);
    names.declareOpaqueStruct("Inner", definitionInfo);
    auto* outerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Outer"));
    auto* innerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Inner"));
    names.implStruct("Outer", {{"inner", innerInfo, definitionInfo}}, {}, definitionInfo);
    names.implStruct("Inner", {{"outer", first.typeInfoPool().makePointerTypeID(outerInfo), definitionInfo}}, {}, definitionInfo);
    auto* outerType = static_cast<IRStructType*>(firstLowering.lowerType(outerInfo));
    assert(outerType->isComplete());
    assert(outerType->toLLVMType(first.llvmContext())->isSized());

    names.defineStruct("VoidField", {{"value", first.typeInfoPool().makeBasicTypeID(TypeID::Void), definitionInfo}}, definitionInfo);
    auto* voidFieldInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("VoidField"));
    assert(rejects([&] { firstLowering.lowerType(voidFieldInfo); }));
    names.defineStruct("FunctionField", {{"value", forwardFunctionInfo, definitionInfo}}, definitionInfo);
    auto* functionFieldInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("FunctionField"));
    assert(rejects([&] { firstLowering.lowerType(functionFieldInfo); }));

    names.declareOpaqueStruct("Self", definitionInfo);
    auto* selfInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Self"));
    names.implStruct("Self", {{"self", selfInfo, definitionInfo}}, {}, definitionInfo);
    assert(rejects([&] { firstLowering.lowerType(selfInfo); }));
    assert(!names.lookupStructType("Self")->isComplete());

    names.declareOpaqueStruct("Left", definitionInfo);
    names.declareOpaqueStruct("Right", definitionInfo);
    auto* leftInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Left"));
    auto* rightInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Right"));
    names.implStruct("Left", {{"right", first.typeInfoPool().makeArrayTypeID(rightInfo, 2), definitionInfo}}, {}, definitionInfo);
    auto* leftType = static_cast<IRStructType*>(firstLowering.lowerType(leftInfo));
    assert(leftType->isComplete());
    assert(!names.lookupStructType("Right")->isComplete());
    names.implStruct("Right", {{"left", leftInfo, definitionInfo}}, {}, definitionInfo);
    assert(rejects([&] { firstLowering.lowerType(leftInfo); }));
    assert(rejects([&] { firstLowering.lowerType(rightInfo); }));

    names.declareOpaqueStruct("Container", definitionInfo);
    names.declareOpaqueStruct("Child", definitionInfo);
    auto* childInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Child"));
    auto* containerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Container"));
    names.implStruct("Container", {{"child", childInfo, definitionInfo}}, {}, definitionInfo);
    auto* containerType = static_cast<IRStructType*>(firstLowering.lowerType(containerInfo));
    auto* childType = static_cast<IRStructType*>(containerType->findMember("child")->type);
    assert(!childType->isComplete());
    names.implStruct("Child", {{"value", firstTypeInfo, definitionInfo}}, {}, definitionInfo);
    assert(firstLowering.lowerType(containerInfo) == containerType);
    assert(childType->isComplete());
    assert(containerType->toLLVMType(first.llvmContext())->isSized());

    names.declareOpaqueStruct("Retry", definitionInfo);
    auto* retryInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Retry"));
    auto futureId = names.lookupStructId("Retry");
    ++futureId.localId;
    auto* unknownInfo = first.typeInfoPool().makeStructTypeID(futureId);
    names.implStruct("Retry", {{"missing", first.typeInfoPool().makePointerTypeID(unknownInfo), definitionInfo}}, {}, definitionInfo);
    assert(rejects([&] { firstLowering.lowerType(retryInfo); }));
    auto* retryType = names.lookupStructType("Retry");
    assert(!retryType->isComplete());
    names.defineStruct("Missing", {{"value", firstTypeInfo, definitionInfo}}, definitionInfo);
    assert(names.lookupStructId("Missing") == futureId);
    assert(firstLowering.lowerType(retryInfo) == retryType);
    assert(retryType->isComplete());
    assert(retryType->findMember("missing").has_value());

    names.declareOpaqueStruct("Atomic", definitionInfo);
    assert(names.lookupStructType("Atomic") == nullptr);
    auto* atomicType = names.getStructTy(first, names.lookupStructId("Atomic"));
    assert(rejects([&] { atomicType->complete({{"bad", nullptr, definitionInfo}}); }));
    assert(rejects([&] { atomicType->complete({{"same", firstInt32, definitionInfo},
                                             {"same", firstInt32, definitionInfo}}); }));
    assert(!atomicType->isComplete());
    assert(atomicType->getFields().empty());
    assert(!atomicType->findMember("same").has_value());
    atomicType->complete({{"good", firstInt32, definitionInfo}});
    assert(atomicType->isComplete());
    assert(rejects([&] { names.getStructTy(first, {"missing-module", 0}); }));

}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--struct-lowering") {
        testStructLoweringLifecycle();
        return 0;
    }
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
        {{"x", firstTypeInfo, definitionInfo},
         {"y", firstTypeInfo, definitionInfo}},
        definitionInfo);

    assert(names.lookupStructDecl("Point") != nullptr);
    auto* pointTypeInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Point"));
    auto* nullablePointTypeInfo = first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo);
    assert(nullablePointTypeInfo != pointTypeInfo);
    assert(!pointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->getBase() == pointTypeInfo);
    assert(nullablePointTypeInfo == first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo));
    LoweringSession firstLowering(first, &names);
    NamingContext secondNames("test-module");
    auto* secondTypeInfo = second.typeInfoPool().makeBasicTypeID(TypeID::Int32);
    secondNames.defineStruct("Point", {{"x", secondTypeInfo, definitionInfo},
                                       {"y", secondTypeInfo, definitionInfo}}, definitionInfo);
    auto* secondPointTypeInfo = second.typeInfoPool().makeStructTypeID(secondNames.lookupStructId("Point"));
    LoweringSession secondLowering(second, &secondNames);
    auto* firstPoint = firstLowering.lowerType(pointTypeInfo);
    auto* secondPoint = secondLowering.lowerType(secondPointTypeInfo);
    assert(firstPoint != secondPoint);
    assert(firstPoint->isStruct());
    assert(secondPoint->isStruct());
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
    assert(firstLowering.lowerType(nullablePointArrayTypeInfo) == firstLowering.lowerType(pointArrayTypeInfo));

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

    assert(firstLowering.lowerType(first.typeInfoPool().makePointerTypeID(pointTypeInfo))->isPointer());
    assert(firstLowering.lowerType(first.typeInfoPool().makeArrayTypeID(pointTypeInfo, 4))->isArray());

    bool rejectedDuplicate = false;
    try {
        names.defineStruct("Point", {}, definitionInfo);
    }
    catch (const sakuraE::SakuraError&) {
        rejectedDuplicate = true;
    }
    assert(rejectedDuplicate);

    testStructLoweringLifecycle();

    return 0;
}
