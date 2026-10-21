#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Lowering/session.hpp"
#include "Compiler/LLVMCodegen/LLVMTypeMapper.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Casting.h>

#include <cassert>
#include <stdexcept>
#include <string_view>

// 验证 storage 类型(IRType) -> LLVM 类型的映射（Codegen::map）：基本整数/浮点/void/label，
// 无符号与有符号同宽映射到同一 LLVM 整数类型，指针/引用/string/TypeInfo 统一为指针，
// 数组与函数签名的结构正确；非法输入（空指针、0 位整数、void 元素、函数返回函数、void 形参）抛 invalid_argument。
void testLLVMTypeMapping() {
    using namespace sakuraE::IR;
    using sakuraE::Codegen::map;
    IRContext storage;
    llvm::LLVMContext context;
    auto* integer = storage.getInt32Ty();
    assert(map(storage.getVoidTy(), context)->isVoidTy());
    assert(map(storage.getBoolTy(), context)->isIntegerTy(1));
    assert(map(storage.getCharTy(), context)->isIntegerTy(8));
    assert(map(integer, context)->isIntegerTy(32));
    assert(map(storage.getInt64Ty(), context)->isIntegerTy(64));
    assert(map(storage.getIntNTy(17), context)->isIntegerTy(17));
    // LLVM 整数不区分符号，符号性只在指令选择时体现。
    assert(map(storage.getUIntNTy(17), context) == map(storage.getIntNTy(17), context));
    assert(map(storage.getUInt32Ty(), context) == map(integer, context));
    assert(map(storage.getUInt64Ty(), context) == map(storage.getInt64Ty(), context));
    assert(map(storage.getFloat32Ty(), context)->isFloatTy());
    assert(map(storage.getFloat64Ty(), context)->isDoubleTy());
    assert(map(storage.getBlockTy(), context)->isLabelTy());
    assert(map(storage.getPointerTo(integer), context)->isPointerTy());
    assert(map(storage.getRefTo(integer), context)->isPointerTy());
    assert(map(storage.getTypeInfoTy(), context)->isPointerTy());
    // string 的 storage 类型就是 char*，且与上下文中缓存的同一实例相等。
    auto* string = IRType::getStringTy();
    assert(string == storage.getPointerTo(storage.getCharTy()));
    assert(map(string, context)->isPointerTy());

    auto* array = llvm::cast<llvm::ArrayType>(map(storage.getArrayTy(integer, 3), context));
    assert(array->getNumElements() == 3);
    assert(array->getElementType() == map(integer, context));
    auto* signature = storage.getFunctionTy(storage.getVoidTy(), {integer, string});
    auto* function = llvm::cast<llvm::FunctionType>(map(signature, context));
    assert(function->getReturnType()->isVoidTy());
    assert(function->getNumParams() == 2);
    assert(function->getParamType(0) == map(integer, context));
    assert(function->getParamType(1) == map(string, context));
    assert(!function->isVarArg());

    // 判断 action 是否抛出 invalid_argument。
    const auto rejects = [](auto action) {
        try {
            action();
        }
        catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    assert(rejects([&] { map(nullptr, context); }));
    assert(rejects([&] { map(storage.getIntNTy(0), context); }));
    assert(rejects([&] { map(storage.getArrayTy(storage.getVoidTy(), 1), context); }));
    assert(rejects([&] { map(storage.getFunctionTy(signature, {}), context); }));
    assert(rejects([&] { map(storage.getFunctionTy(integer, {storage.getVoidTy()}), context); }));
}

// 验证结构体 lowering 的生命周期（对应 next-plan“测试与验收”中 opaque 结构体 / 跨模块 / 多 IRContext 条目）：
// 前向声明的 opaque 结构体先被引用后补全且保持同一 IRStructType/LLVM StructType 身份；
// 通过指针/引用的自引用与互引用可完成；按值递归、void/函数字段被拒绝；
// 失败的 lowering 不会留下半完成状态，后续定义补齐后可重试；IRStructType::complete 具备原子性。
void testStructLoweringLifecycle() {
    using namespace sakuraE::IR;
    IRContext first;
    llvm::LLVMContext firstLLVM;
    NamingContext names("lifecycle-test");
    const sakuraE::PositionInfo definitionInfo {1, 1, "struct lifecycle test"};
    // 语义类型(TypeInfo)在池中按结构去重：相同函数签名返回同一实例。
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

    // 情形 1：opaque 结构体 Forward 在实现前就被函数签名、数组指针、nullable 引用。
    names.declareOpaqueStruct("Forward", definitionInfo);
    auto* forwardInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Forward"));
    auto* forwardArrayInfo = first.typeInfoPool().makeArrayTypeID(forwardInfo, 2);
    auto* forwardPointerInfo = first.typeInfoPool().makePointerTypeID(forwardArrayInfo);
    auto* nullableForwardInfo = first.typeInfoPool().wrapTypeAsNullable(forwardInfo, definitionInfo);
    auto* forwardFunctionInfo = first.typeInfoPool().makeFunctionTypeID(forwardInfo, {forwardPointerInfo});
    auto* forwardFunction = firstLowering.lowerType(forwardFunctionInfo);
    auto* forwardPointer = firstLowering.lowerType(forwardPointerInfo);
    auto* forwardType = static_cast<IRStructType*>(firstLowering.lowerType(forwardInfo));
    auto* llvmForward = llvm::cast<llvm::StructType>(sakuraE::Codegen::map(forwardType, firstLLVM));
    // 尚未实现时 lowering 得到不完整结构体，映射为 LLVM opaque 结构体。
    assert(!forwardType->isComplete());
    assert(llvmForward->isOpaque());
    // 补全后再次 lowering：所有派生类型命中缓存不变，结构体原地补全，LLVM 侧同一 StructType 被设置 body。
    names.implStruct("Forward", {{"value", firstTypeInfo, definitionInfo}}, {}, definitionInfo);
    assert(firstLowering.lowerType(forwardPointerInfo) == forwardPointer);
    assert(firstLowering.lowerType(forwardFunctionInfo) == forwardFunction);
    // nullable 不改变 storage 类型。
    assert(firstLowering.lowerType(nullableForwardInfo) == forwardType);
    assert(firstLowering.lowerType(forwardInfo) == forwardType);
    assert(forwardType->isComplete());
    assert(forwardType->findMember("value")->type == firstInt32);
    assert(sakuraE::Codegen::map(forwardType, firstLLVM) == llvmForward);
    assert(!llvmForward->isOpaque());
    assert(llvmForward->getElementType(0) == sakuraE::Codegen::map(firstInt32, firstLLVM));
    // 已完成的结构体不能重复 complete。
    assert(rejects([&] { forwardType->complete({}); }));

    // 结构体 storage 类型由 NamingContext 持有，不随某个 LoweringSession 销毁而失效。
    {
        LoweringSession temporary(first, &names);
        assert(temporary.lowerType(forwardInfo) == forwardType);
    }
    assert(names.lookupStructType("Forward") == forwardType);
    assert(names.getStructTy(first, names.lookupStructId("Forward")) == forwardType);
    assert(names.resolveNamingContext(names.lookupStructId("Forward")) == &names);
    assert(names.lookupStructType("Unknown") == nullptr);
    // 同一 NamingContext 搭配另一个 IRContext 进行 lowering 必须被拒绝（storage 类型属于唯一 IRContext）。
    IRContext otherContext;
    LoweringSession wrongContext(otherContext, &names);
    assert(rejects([&] { wrongContext.lowerType(forwardInfo); }));
    // 不同模块的同名结构体通过 StructDeclId 区分，lowering 出不同的类型。
    NamingContext otherNames("other-module");
    otherNames.defineStruct("Forward", {{"value", firstTypeInfo, definitionInfo}}, definitionInfo);
    auto* otherForwardInfo = first.typeInfoPool().makeStructTypeID(otherNames.lookupStructId("Forward"));
    LoweringSession otherLowering(first, &otherNames);
    auto* otherForwardType = otherLowering.lowerType(otherForwardInfo);
    assert(otherForwardType != forwardType);
    assert(otherNames.lookupStructType("Forward") == otherForwardType);
    assert(sakuraE::Codegen::map(otherForwardType, firstLLVM) != llvmForward);
    assert(forwardType->findMember("value").has_value());

    // 情形 2：通过指针与引用的自引用结构体可以完成，且成员指向自身类型。
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
    assert(sakuraE::Codegen::map(nodeType, firstLLVM)->isSized());

    // 情形 3：Outer 按值包含 Inner，Inner 通过指针引用 Outer —— 递归 lowering 可完成并得到 sized 类型。
    names.declareOpaqueStruct("Outer", definitionInfo);
    names.declareOpaqueStruct("Inner", definitionInfo);
    auto* outerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Outer"));
    auto* innerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Inner"));
    names.implStruct("Outer", {{"inner", innerInfo, definitionInfo}}, {}, definitionInfo);
    names.implStruct("Inner", {{"outer", first.typeInfoPool().makePointerTypeID(outerInfo), definitionInfo}}, {}, definitionInfo);
    auto* outerType = static_cast<IRStructType*>(firstLowering.lowerType(outerInfo));
    assert(outerType->isComplete());
    assert(sakuraE::Codegen::map(outerType, firstLLVM)->isSized());

    // void 字段与函数类型字段不是合法的 storage 字段类型。
    names.defineStruct("VoidField", {{"value", first.typeInfoPool().makeBasicTypeID(TypeID::Void), definitionInfo}}, definitionInfo);
    auto* voidFieldInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("VoidField"));
    assert(rejects([&] { firstLowering.lowerType(voidFieldInfo); }));
    names.defineStruct("FunctionField", {{"value", forwardFunctionInfo, definitionInfo}}, definitionInfo);
    auto* functionFieldInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("FunctionField"));
    assert(rejects([&] { firstLowering.lowerType(functionFieldInfo); }));

    // 按值包含自身会形成无限大小，必须报错，且失败后结构体仍保持未完成。
    names.declareOpaqueStruct("Self", definitionInfo);
    auto* selfInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Self"));
    names.implStruct("Self", {{"self", selfInfo, definitionInfo}}, {}, definitionInfo);
    assert(rejects([&] { firstLowering.lowerType(selfInfo); }));
    assert(!names.lookupStructType("Self")->isComplete());

    // Left 包含 Right 数组时 Right 仍是 opaque，Left 可先完成；之后 Right 按值包含 Left 形成按值环，应被拒绝。
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

    // 情形 4：Container 按值包含仍为 opaque 的 Child；Child 实现后再次 lowering，Container 不变，
    // Child 原地补全，LLVM 侧元素类型身份保持不变。
    names.declareOpaqueStruct("Container", definitionInfo);
    names.declareOpaqueStruct("Child", definitionInfo);
    auto* childInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Child"));
    auto* containerInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Container"));
    names.implStruct("Container", {{"child", childInfo, definitionInfo}}, {}, definitionInfo);
    auto* containerType = static_cast<IRStructType*>(firstLowering.lowerType(containerInfo));
    auto* childType = static_cast<IRStructType*>(containerType->findMember("child")->type);
    assert(!childType->isComplete());
    auto* llvmContainer = llvm::cast<llvm::StructType>(sakuraE::Codegen::map(containerType, firstLLVM));
    auto* llvmChild = llvm::cast<llvm::StructType>(llvmContainer->getElementType(0));
    assert(llvmChild->isOpaque());
    names.implStruct("Child", {{"value", firstTypeInfo, definitionInfo}}, {}, definitionInfo);
    assert(firstLowering.lowerType(containerInfo) == containerType);
    assert(childType->isComplete());
    assert(sakuraE::Codegen::map(containerType, firstLLVM) == llvmContainer);
    assert(llvmContainer->getElementType(0) == llvmChild);
    assert(!llvmChild->isOpaque());
    assert(sakuraE::Codegen::map(containerType, firstLLVM)->isSized());

    // 情形 5：字段引用了尚未分配的 StructDeclId（预测下一个 localId），lowering 失败；
    // 该 id 随后被 defineStruct 占用，重试 lowering 成功。
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
    assert(rejects([&] { firstLowering.lowerType(nullptr); }));

    // 情形 6：complete 的原子性——非法字段（空类型、重复字段名）抛错时不留下任何部分字段。
    names.declareOpaqueStruct("Atomic", definitionInfo);
    // 仅声明未 lowering 时 NamingContext 中还没有 storage 类型。
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
    // 未知模块的 StructDeclId 无法解析。
    assert(rejects([&] { names.getStructTy(first, {"missing-module", 0}); }));

}

// 入口：`--struct-lowering` 只运行结构体生命周期测试；默认依次验证类型映射、
// IRContext 隔离与缓存、nullable 语义类型、跨上下文结构体 lowering，最后再跑生命周期测试。
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--struct-lowering") {
        testStructLoweringLifecycle();
        return 0;
    }
    using namespace sakuraE::IR;
    testLLVMTypeMapping();

    // 每个 IRContext 内 storage 类型唯一化，不同 IRContext 之间互不共享。
    IRContext first;
    IRContext second;

    auto* firstInt32 = first.getInt32Ty();
    assert(firstInt32 == first.getInt32Ty());
    assert(first.getPointerTo(firstInt32) == first.getPointerTo(firstInt32));

    auto* secondInt32 = second.getInt32Ty();
    assert(firstInt32 != secondInt32);
    llvm::LLVMContext firstLLVM;
    llvm::LLVMContext secondLLVM;
    assert(sakuraE::Codegen::map(firstInt32, firstLLVM) != sakuraE::Codegen::map(secondInt32, secondLLVM));

    // TypeInfo 池同样按上下文唯一化。
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
    // nullable 包装产生新的 TypeInfo，可解包回原类型，且重复包装同一类型得到同一实例。
    auto* pointTypeInfo = first.typeInfoPool().makeStructTypeID(names.lookupStructId("Point"));
    auto* nullablePointTypeInfo = first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo);
    assert(nullablePointTypeInfo != pointTypeInfo);
    assert(!pointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->isNullable());
    assert(nullablePointTypeInfo->getBase() == pointTypeInfo);
    assert(nullablePointTypeInfo == first.typeInfoPool().wrapTypeAsNullable(pointTypeInfo, definitionInfo));
    LoweringSession firstLowering(first, &names);
    // 两个独立的 IRContext/NamingContext（同模块名）各自 lowering 同名结构体，结果互不相同，
    // 且映射后的 LLVM 类型属于各自的 LLVMContext。
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
    auto* firstLLVMPoint = sakuraE::Codegen::map(firstPoint, firstLLVM);
    auto* secondLLVMPoint = sakuraE::Codegen::map(secondPoint, secondLLVM);
    assert(firstLLVMPoint != secondLLVMPoint);
    assert(&firstLLVMPoint->getContext() == &firstLLVM);
    assert(&secondLLVMPoint->getContext() == &secondLLVM);
    // 赋值兼容：非空可赋给 nullable，反之不行。
    assert(isAssignableTo(pointTypeInfo, pointTypeInfo));
    assert(isAssignableTo(nullablePointTypeInfo, nullablePointTypeInfo));
    assert(isAssignableTo(pointTypeInfo, nullablePointTypeInfo));
    assert(!isAssignableTo(nullablePointTypeInfo, pointTypeInfo));

    // nullable 数组仍是数组，lowering 后与非 nullable 数组的 storage 类型相同。
    auto* pointArrayTypeInfo = first.typeInfoPool().makeArrayTypeID(pointTypeInfo, 4);
    auto* nullablePointArrayTypeInfo = first.typeInfoPool().wrapTypeAsNullable(pointArrayTypeInfo, definitionInfo);
    assert(nullablePointArrayTypeInfo->isArray());
    assert(nullablePointArrayTypeInfo->isNullable());
    assert(nullablePointArrayTypeInfo->getBase() == pointArrayTypeInfo);
    assert(nullablePointArrayTypeInfo->getElementType() == pointTypeInfo);
    assert(firstLowering.lowerType(nullablePointArrayTypeInfo) == firstLowering.lowerType(pointArrayTypeInfo));

    // 标量类型不允许包装为 nullable。
    bool rejectedScalarNullable = false;
    try {
        first.typeInfoPool().wrapTypeAsNullable(firstTypeInfo, definitionInfo);
    }
    catch (const sakuraE::SakuraError&) {
        rejectedScalarNullable = true;
    }
    assert(rejectedScalarNullable);

    // 不允许对 nullable 再次包装。
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

    // 同一 NamingContext 中重复定义结构体应报错。
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
