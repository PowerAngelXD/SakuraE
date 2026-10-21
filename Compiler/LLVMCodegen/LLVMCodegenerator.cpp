#include "LLVMCodegenerator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Semantic/model/instruction.hpp"
#include "Compiler/IR/Backend/type/type.hpp"
#include "Compiler/IR/Semantic/value/array.hpp"
#include "Compiler/IR/Semantic/value/constant.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "Runtime/gc.h"
#include "includes/String.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

namespace sakuraE::Codegen {
    // 指令生成
    // 将 lowering 后的数组常量（数组字面量）生成为一个 GC 托管数组：
    // 先逐个求值元素，再通过 __gc_alloc 分配 payload，最后把元素逐个写入。
    // 返回值是托管数组 payload 指针；本函数离开时不会为其注册 root，
    // 是否需要保护由调用方（create_array 分支）根据 Backend GC 信息决定。
    llvm::Value* LLVMCodeGenerator::emitArrayConstant(IR::Constant* constant, FunctionEmiiter* curFn) {
        // Backend IR 必须已把该常量标记为 Array 种类，否则说明 lowering 与 emit 不一致
        if (curFn->backend.constants.at(constant) != IR::ConstantKind::Array) {
            throw std::runtime_error("Array emitter requires a lowered array constant");
        }
        const auto* irArray = constant->getArrayPayload();
        if (!irArray) throw std::runtime_error("Array constant has no payload");
        // 运行时类型元数据描述必须是托管数组(ManagedArray)，且长度与字面量元素个数一致
        const auto& type = runtimeType(constant);
        if (type.representation != IR::ValueRepresentation::ManagedArray || type.count != irArray->getArray().size()) {
            throw std::runtime_error("Array constant payload does not match its lowered representation");
        }
        std::vector<llvm::Value*> arrayContent;
        arrayContent.reserve(irArray->getArray().size());
        // 只有在确实存在需要保护的元素时才打开一个临时 GC root 作用域
        bool openedTempScope = false;
        for (auto* element : irArray->getArray()) {
            // 元素若是变量槽位（alloca），读取的是槽位中存放的值（RuntimeValue* 或数组指针），而非槽位地址
            auto* elementValue = toLLVMValue(element, curFn);
            if (auto* slot = llvm::dyn_cast<llvm::AllocaInst>(elementValue)) {
                elementValue = builder->CreateLoad(slot->getAllocatedType(), slot, "array.elem.load");
            }
            // 后续 createHeapAlloc 会调用 __gc_alloc，可能触发回收；
            // 因此需要把托管元素 spill 到已注册 root 的 entry 槽位，再从槽位重新读取使用
            if (curFn->shouldTrackAsGCRoot(element)) {
                if (!openedTempScope) {
                    curFn->gcEnterScope();
                    openedTempScope = true;
                }
                auto* slot = curFn->createRootedTemporary(elementValue, "gc.array.elem", runtimeType(element));
                elementValue = builder->CreateLoad(slot->getAllocatedType(), slot, "array.elem.rooted");
            }
            arrayContent.push_back(elementValue);
        }
        // 托管数组 payload 中存放的是 RuntimeValue* / 数组指针，从不直接存放未装箱的标量 storage
        auto* elementType = builder->getPtrTy();
        // payload 布局为 [count x ptr]，GC 类型元数据描述“RuntimeValue 指针数组”，以便扫描时追踪每个元素
        auto* arrayType = llvm::ArrayType::get(elementType, type.count);
        auto* gcType = curFn->parent->getRuntimeValueArrayGCType(
            static_cast<uint32_t>(curFn->parent->content->getDataLayout().getTypeAllocSize(elementType)), type.count);
        auto* arrayPtr = curFn->createHeapAlloc(arrayType, gcType, builder->getInt64(type.count));
        // 逐个元素写入 payload：GEP 以 ptr 为步长得到第 i 个槽位
        for (std::size_t i = 0; i < arrayContent.size(); ++i) {
            auto* slot = builder->CreateGEP(elementType, arrayPtr, {builder->getInt64(i)});
            builder->CreateStore(arrayContent[i], slot);
        }
        // 数组已持有所有元素，临时 root 作用域可以撤销
        if (openedTempScope) curFn->gcLeaveScope();
        return arrayPtr;
    }

    // 单条 Semantic IR 指令的 LLVM 生成入口。
    // 指令既可能按基本块顺序被 FunctionEmiiter::codegen 驱动生成，也可能作为其他指令的操作数
    // 经 toLLVMValue 被按需提前生成；生成结果通过 bind 缓存到 instructionMap，保证每条指令只 emit 一次。
    // 约束：调用前 activeFunction 必须已指向当前 BackendFunction，storageType/runtimeType 依赖它查询。
    llvm::Value* LLVMCodeGenerator::instgen(IR::Instruction* ins, FunctionEmiiter* curFn) {
        llvm::Value* instResult = nullptr;
        // 已生成过（或已被 GC root 槽位保护）则直接复用，避免重复 emit
        if (hasLLVMValue(ins)) return toLLVMValue(ins, curFn);
        switch (ins->getKind())
        {
            // constant：常量指令，直接生成其常量操作数（标量常量会装箱，字符串走 create_string，数组走 emitArrayConstant）
            case IR::OpKind::constant: {
                auto constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                if (!constant) throw std::runtime_error("Constant instruction has no constant operand");
                instResult = toLLVMConstant(constant, curFn);
                break;
            }
            // add/sub/mul/div/mod：算术运算。
            // 操作数先拆箱(Raw)得到原始标量，经 promote 做整数/浮点提升后生成对应 LLVM 算术指令，
            // 结果再按本指令的运行时类型描述装箱为新的 RuntimeValue*。
            // 整数除法/取余使用有符号版本(sdiv/srem)。
            case IR::OpKind::add: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(add(lhs, rhs), runtimeType(ins), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::sub: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(sub(lhs, rhs), runtimeType(ins), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mul: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(mul(lhs, rhs), runtimeType(ins), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::div: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(div(lhs, rhs), runtimeType(ins), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mod: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(mod(lhs, rhs), runtimeType(ins), curFn);

                bind(ins, instResult);
                break;
            }
            // lgc_not：逻辑非。操作数拆箱为 i1 后取反（对 i1 而言 xor true 即逻辑非），再装箱
            case IR::OpKind::lgc_not: {
                auto* operand = rawValue(ins->arg(0), curFn);
                instResult = boxRaw(builder->CreateNot(operand, "logical.not"), runtimeType(ins), curFn);
                break;
            }
            // gmem：结构体成员访问，结果是结构体内部字段的原始字段地址（不装箱）
            case IR::OpKind::gmem: {
                // 字段在 storage 布局中的位置（所属结构体布局 + 字段下标）由 lowering 预先计算
                const auto& field = curFn->backend.fieldAccess(ins);
                auto* base = toLLVMValue(ins->arg(0), curFn);
                const auto* address = dynamic_cast<const IR::Instruction*>(ins->arg(0));
                // 若基址是语言层槽位（局部变量/参数等），槽位里存放的是结构体 handle，需要先 load 出来
                if (isPointerSlot(address)) base = builder->CreateLoad(builder->getPtrTy(), base, "struct.handle");
                // null handle 检查：handle 为空时跳转到 struct.null 块执行 llvm.trap 并以 unreachable 终止；
                // 非空则在 struct.present 块继续。此后的指令都插入到 present 块中
                auto* present = llvm::BasicBlock::Create(*context, "struct.present", curFn->content);
                auto* missing = llvm::BasicBlock::Create(*context, "struct.null", curFn->content);
                builder->CreateCondBr(builder->CreateIsNotNull(base), present, missing);
                builder->SetInsertPoint(missing);
                builder->CreateIntrinsic(llvm::Intrinsic::trap, llvm::ArrayRef<llvm::Type*>{}, llvm::ArrayRef<llvm::Value*>{});
                builder->CreateUnreachable();
                builder->SetInsertPoint(present);
                // 在结构体 storage 布局上做 struct GEP，得到字段的原始地址
                instResult = builder->CreateStructGEP(map(field.layout, *context), base,
                    static_cast<unsigned>(field.index), "field.address");
                break;
            }
            // 比较运算：操作数拆箱后比较，得到 i1，再装箱为 Bool。
            // compare 会根据两侧运行时类型选择 strcmp（仅字符串 ==/!=）、fcmp、icmp 或指针相等比较
            case IR::OpKind::lgc_equal:
            case IR::OpKind::lgc_not_equal:
            case IR::OpKind::lgc_mr_than:
            case IR::OpKind::lgc_ls_than:
            case IR::OpKind::lgc_eq_mr_than:
            case IR::OpKind::lgc_eq_ls_than: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(compare(lhs, rhs, runtimeType(ins->arg(0)), runtimeType(ins->arg(1)), ins->getKind(), curFn), runtimeType(ins), curFn);
                break;
            }
            // create_alloca：声明语言层局部变量。
            // 变量槽位总是 ptr 类型：其中存放 RuntimeValue*（标量/字符串）、托管数组指针或结构体 handle
            case IR::OpKind::create_alloca: {
                // 指令名形如 "create_alloca.<变量名>"，取 '.' 后的第一段作为变量名
                auto insName = ins->getName();
                auto identifierName = insName.split('.')[1];

                auto identifierType = llvm::PointerType::getUnqual(*context);

                // 槽位统一建在 entry 块（createAlloca 会临时切换插入点），便于 mem2reg 与 GC root 注册
                llvm::AllocaInst* alloca = curFn->createAlloca(identifierType, nullptr, identifierName);

                auto initVal = ins->arg(0);
                // 有初始值：直接写入初始值（已装箱的值或数组指针）
                if (initVal) {
                    builder->CreateStore(toLLVMValue(initVal, curFn), alloca);
                }
                // 无初始值的数组变量：预先分配一个对应长度的托管数组（元素为 RuntimeValue* 槽），把 payload 指针写入槽位
                else if (storageType(ins)->isArray()) {
                    auto* sourceArrayType = static_cast<IR::IRArrayType*>(storageType(ins));
                    auto arrayType = llvm::ArrayType::get(
                        llvm::PointerType::getUnqual(*context),
                        sourceArrayType->getNumElements());
                    auto elementType = arrayType->getArrayElementType();
                    auto* gcType = curFn->parent->getRuntimeValueArrayGCType(
                        static_cast<uint32_t>(curFn->parent->content->getDataLayout().getTypeAllocSize(elementType)),
                            sourceArrayType->getNumElements());
                    auto* arrayPtr = curFn->createHeapAlloc(
                        arrayType,
                        gcType,
                        builder->getInt64(sourceArrayType->getNumElements()));
                    builder->CreateStore(arrayPtr, alloca);
                }
                // 其余无初始值的情况（如字符串、结构体）：槽位初始化为 null
                else {
                    builder->CreateStore(llvm::Constant::getNullValue(identifierType), alloca);
                }

                // lowering 判定该槽位承载托管对象时，将槽位地址注册为 GC root；
                // 数组走 __gc_register，其他托管值走 __gc_register_value_slot
                if (curFn->backend.rootsSlot(ins)) {
                    curFn->gcRegisterManagedSlot(alloca, runtimeType(ins));
                }

                // 槽位地址作为该指令的值，同时登记到当前函数的作用域表
                bind(ins, alloca);
                curFn->scope.declare(identifierName, alloca, nullptr);

                break;
            }
            // store：赋值。arg(0) 为目标地址，arg(1) 为要写入的值；按目标地址的种类决定写入方式
            case IR::OpKind::store: {
                IR::IRValue* irAddr = ins->arg(0);
                llvm::Value* destAddr = toLLVMValue(irAddr, curFn);
                llvm::Value* srcVal = toLLVMValue(ins->arg(1), curFn);

                if (destAddr && srcVal) {
                    // 目标是语言层指针槽位（变量/参数/托管数组元素槽）：直接写入值本身（RuntimeValue* 或数组指针）
                    if (isPointerSlot(dynamic_cast<const IR::Instruction*>(irAddr))) {
                        builder->CreateStore(srcVal, destAddr);
                    }
                    // 目标是嵌入结构体字段（原始字段地址且字段为结构体）：源值是结构体地址，
                    // 先整体 load 出结构体再 store，实现按值拷贝
                    else if (isRawFieldAddress(irAddr) && storageType(irAddr)->isStruct()) {
                        auto* type = map(storageType(irAddr), *context);
                        builder->CreateStore(builder->CreateLoad(type, srcVal, "struct.copy"), destAddr);
                    }
                    // 嵌入数组字段不支持整体赋值，必须逐个下标赋值，因此显式报错
                    else if (isRawFieldAddress(irAddr) && storageType(irAddr)->isArray()) {
                        throw std::runtime_error("Whole embedded array assignment is not supported; assign indexed elements");
                    }
                    // 其余原始存储地址（结构体标量字段、字符串/字符指针的字符元素等）：
                    // 源值若为装箱表示则先拆箱，写入的是原始标量
                    else {
                        auto* raw = runtimeType(ins->arg(1)).representation == IR::ValueRepresentation::Boxed
                            ? unboxRaw(srcVal, storageType(ins->arg(1)), curFn) : srcVal;
                        builder->CreateStore(raw, destAddr);
                    }
                    // 赋值表达式的值即为被写入的源值
                    bind(ins, srcVal);
                }
                else {
                    throw std::runtime_error("Assign failed: Null operand.");
                }
                break;
            }
            // create_array：数组字面量，生成托管数组
            case IR::OpKind::create_array: {
                auto* constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                if (!constant) throw std::runtime_error("Array instruction has no constant operand");
                auto* arrayPtr = emitArrayConstant(constant, curFn);


                // 若结果需要作为 GC root 跟踪，则 spill 到已注册 root 的临时槽位，
                // 并登记到 protectedValueSlots：之后对该指令的引用都会从槽位重新 load（见 toLLVMValue）
                if (curFn->shouldTrackAsGCRoot(ins)) {
                    auto* protectedSlot = curFn->createRootedTemporary(
                        arrayPtr,
                        "gc.array.result",
                        runtimeType(ins));
                    protectValue(ins, protectedSlot);
                }

                bind(ins, arrayPtr);
                break;
            }
            // indexing：下标访问，结果是元素地址（左值），由后续 load/store 读写。
            // 根据被索引对象的 storage 类型与运行时类型分为：托管数组、嵌入数组字段、字符串、字符指针、引用
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = rawValue(ins->arg(1), curFn);

                auto addrIRType = storageType(ins->arg(0));
                const auto& addrRuntimeType = runtimeType(ins->arg(0));
                // 调试输出：每次 emit 下标访问都会打印基址的 IR 类型
                sutils::println("addr IR type: " + addrIRType->toString());
                llvm::Type* elementType = nullptr;
                // 基址是左值（槽位地址）时，需要先从槽位 load 出真正的对象指针
                auto* addrInst = dynamic_cast<IR::Instruction*>(ins->arg(0));
                bool baseIsLValue = addrInst && addrInst->isLValue();

                if (addrIRType->isArray()) {
                    // 嵌入在结构体中的数组字段：基址是原始字段地址，元素按原始 storage 连续存放
                    if (isRawFieldAddress(ins->arg(0))) {
                        auto* array = static_cast<IR::IRArrayType*>(addrIRType);
                        // 下标统一扩展到 i64；仅 U32/U64 按无符号扩展，其余按有符号扩展
                        auto* index64 = builder->CreateIntCast(indexVal, builder->getInt64Ty(),
                            runtimeType(ins->arg(1)).kind != IR::RuntimeDescriptionKind::U32 &&
                            runtimeType(ins->arg(1)).kind != IR::RuntimeDescriptionKind::U64, "field.index");
                        // 运行时边界检查：越界时 __runtime_check_array_bounds 记录错误并结束 JIT 工作线程
                        builder->CreateCall(curFn->parent->lookup("__runtime_check_array_bounds")->content,
                            {index64, builder->getInt64(array->getNumElements())});
                        // 在 [N x T] 上做 GEP {0, index} 得到元素原始地址；break 跳出 switch，由末尾统一 bind
                        instResult = builder->CreateGEP(map(addrIRType, *context), addr,
                            {builder->getInt64(0), index64}, "field.array.element");
                        break;
                    }
                    // 托管数组：变量槽位里存放的是 payload 指针，先 load 出来
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.array.base");
                    }
                    auto* arrayType = static_cast<IR::IRArrayType*>(addrIRType);
                    // 根据下标的 storage 类型决定扩展到 i64 时是否按无符号处理
                    bool isUnsigned = false;
                    switch (storageType(ins->arg(1))->getIRTypeID()) {
                        case IR::IRTypeID::UInteger32TyID:
                        case IR::IRTypeID::UInteger64TyID:
                        case IR::IRTypeID::UIntegerNTyID:
                            isUnsigned = true;
                            break;
                        default:
                            break;
                    }
                    auto* index64 = builder->CreateIntCast(
                        indexVal,
                        llvm::Type::getInt64Ty(*context),
                        !isUnsigned,
                        "index.i64");
                    // 运行时边界检查，长度取自 storage 数组类型的静态元素个数
                    auto* boundsCheck = curFn->parent->lookup("__runtime_check_array_bounds");
                    builder->CreateCall(boundsCheck->content, {
                        index64,
                        builder->getInt64(arrayType->getNumElements())
                    });
                    /* 完全装箱模式下，托管数组存储 RuntimeValue* 元素
                     * 对此类数组进行索引会直接返回包装对象
                     */
                    auto* boxedElement = builder->CreateGEP(
                        llvm::PointerType::getUnqual(*context), addr, {index64}, "indexing.boxed.array");
                    /* 保留元素槽位作为左值，后续 load 会取得其中存储的 RuntimeValue*
                     */
                    instResult = boxedElement;
                    bind(ins, instResult);
                    break;
                }
                // 字符串：按字符(char storage)索引其底层字符 payload
                else if (addrRuntimeType.kind == IR::RuntimeDescriptionKind::String) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.string.base");
                    }
                    // 语言层字符串是 RuntimeValue*，索引操作作用于其底层字符 payload；
                    // 嵌入结构体中的字符串字段（原始字段地址）存放的已是原始字符指针，无需拆箱
                    if (!isRawFieldAddress(ins->arg(0))) addr = unboxRaw(addr, addrIRType, curFn);
                    elementType = map(rootModule.context->getCharTy(), *context);
                }
                // 指针：目前仅支持字符指针(*i8)的下标访问，按 pointee storage 类型做指针算术
                else if (addrRuntimeType.kind == IR::RuntimeDescriptionKind::Pointer) {
                    auto* ptrTy = static_cast<IR::IRPointerType*>(addrIRType);
                    auto* pointeeTy = ptrTy->getElementType();

                    if (!pointeeTy) {
                        throw std::runtime_error("Indexing failed: pointer operand has no element type.");
                    }
                    if (!addrRuntimeType.element || addrRuntimeType.element->kind != IR::RuntimeDescriptionKind::I8) {
                        throw std::runtime_error(
                            "Indexing failed: only character pointers are currently supported for pointer indexing."
                        );
                    }

                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.ptr.base");
                    }
                    // 指针变量存储装箱后的指针值，索引操作需要取得 RuntimeValue::Pointer 中的实际地址
                    if (!isRawFieldAddress(ins->arg(0))) addr = unboxRaw(addr, addrIRType, curFn);
                    elementType = map(pointeeTy, *context);
                }
                // 引用：引用值本身是被引用槽位的地址，先取得槽位，再按被引用对象的种类分派
                else if (addrRuntimeType.kind == IR::RuntimeDescriptionKind::Reference) {
                    const auto* refElement = addrRuntimeType.element;
                    auto* refElementTy = refElement ? refElement->storage : nullptr;
                    llvm::Value* refAddr = addr;

                    // 引用变量的槽位中存放的是被引用槽位的地址，先 load 出来
                    if (baseIsLValue) {
                        refAddr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.addr");
                    }

                    if (!refElementTy) {
                        throw std::runtime_error(
                            "Indexing failed: reference operand has no element type."
                        );
                    }

                    // 引用托管数组：从被引用槽位 load 出 payload 指针，返回 RuntimeValue* 元素槽位作为左值。
                    // 注意此路径直接使用原始下标，既未扩展到 i64 也未做边界检查
                    if (refElementTy->isArray()) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.array.base");
                        auto* boxedElement = builder->CreateGEP(
                            llvm::PointerType::getUnqual(*context), addr, {indexVal}, "indexing.ref.boxed.array");
                        instResult = boxedElement;
                        bind(ins, instResult);
                        break;
                    }
                    // 引用字符串：从被引用槽位取出 RuntimeValue*，再拆箱得到字符 payload
                    else if (refElement->kind == IR::RuntimeDescriptionKind::String) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.string.base");
                        addr = unboxRaw(addr, refElementTy, curFn);
                        elementType = map(rootModule.context->getCharTy(), *context);
                    }
                    // 引用字符指针：从被引用槽位取出装箱指针并拆箱得到实际地址
                    else if (refElement->kind == IR::RuntimeDescriptionKind::Pointer && refElement->element &&
                        refElement->element->kind == IR::RuntimeDescriptionKind::I8) {
                        auto* ptrTy = static_cast<IR::IRPointerType*>(refElementTy);
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.ptr.base");
                        addr = unboxRaw(addr, refElementTy, curFn);
                        elementType = map(ptrTy->getElementType(), *context);
                    }
                    else {
                        throw std::runtime_error(
                            "Indexing failed: reference operand does not refer to an indexable value."
                        );
                    }
                }
                else {
                    throw std::runtime_error("Indexing failed: unsupported operand type.");
                }

                // 走到这里的是字符串/字符指针/引用(字符串、字符指针)这几类，统一做元素指针算术
                if (!addr) {
                    throw std::runtime_error("Indexing failed: null address operand.");
                }
                if (!elementType) {
                    throw std::runtime_error("Indexing failed: failed to resolve element type.");
                }

                // 按元素 storage 类型做 GEP，得到原始元素地址（字符串/指针路径未做边界检查）
                auto ptr = builder->CreateGEP(elementType, addr, {indexVal}, "indexing.ptr");

                instResult = ptr;
                bind(ins, instResult);
                break;
            }
            // param：函数参数。参数在函数入口已被 spill 到 entry 块的槽位（见 FunctionEmiiter::codegen），
            // 这里按 "param.<参数名>" 解析出参数名，返回该槽位地址作为左值
            case IR::OpKind::param: {
                auto insName = ins->getName();
                auto paramName = insName.split('.')[1];

                llvm::Value* realAddr = curFn->getParamAddress(paramName);

                bind(ins, realAddr);
                break;
            }
            // typeof：其 LLVM 生成尚未实现（推迟），显式报错而不是生成错误代码
            case IR::OpKind::_typeof: {
                throw std::runtime_error("LLVM emission for typeof is deferred");
            }
            // sizeof：在编译期按目标 DataLayout 计算类型操作数 storage 类型的分配大小，结果装箱为 i64 常量值
            case IR::OpKind::_sizeof: {
                auto* llvmType = map(curFn->backend.typeOperands.at(ins)->storage, *context);
                auto size = curFn->parent->content->getDataLayout().getTypeAllocSize(llvmType);
                instResult = boxRaw(builder->getInt64(static_cast<std::uint64_t>(size)), runtimeType(ins), curFn);
                bind(ins, instResult);
                break;
            }
            // gaddr：取地址（&x）或取引用（ref x）。操作数是左值，toLLVMValue 得到其槽位/元素地址
            case IR::OpKind::gaddr: {
                auto* address = toLLVMValue(ins->arg(0), curFn);

                /* 一等指针是装箱后的 RuntimeValue，payload 是取地址表达式表示的实际地址
                 * 传递的不是存储槽位本身的 RuntimeValue*
                 */
                if (storageType(ins)->isPointer()) {
                    bind(ins, boxRaw(address, runtimeType(ins), curFn));
                }
                // 引用(Ref)不装箱：引用值就是被引用槽位的原始地址
                else {
                    bind(ins, address);
                }
                break;
            }
            // deref：解引用（*p）。操作数是对指针变量 load 得到的值；结果是被指向的地址（左值）
            case IR::OpKind::deref: {
                instResult = toLLVMValue(ins->arg(0), curFn);

                if (storageType(ins->arg(0))->isPointer()) {
                    /* 指针表达式在语言值边界处进行装箱
                     * 解引用操作作用于其中存储的地址
                     */
                    instResult = unboxRaw(instResult, storageType(ins->arg(0)), curFn);
                }
                else if (storageType(ins->arg(0))->isRef()) {
                    /* 引用指向语言层的存储槽位，这些槽位中存放的可能是RuntimeValue*（标量/字符串）
                     * 也可能是托管对象的payload 指针（数组），
                     * 因此解引用时必须直接加载指针，不能将其解释为未装箱的原始值后再次装箱
                     */
                    instResult = builder->CreateLoad(
                        llvm::PointerType::getUnqual(*context),
                        instResult,
                        "deref.value");
                }

                bind(ins, instResult);
                break;
            }
            // load：从左值地址读出语言层值，按地址种类决定读取方式
            case IR::OpKind::load: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                // 语言层指针槽位：槽位中直接存放 RuntimeValue* / 数组指针 / 结构体 handle，按 ptr 读取
                if (isPointerSlot(dynamic_cast<const IR::Instruction*>(ins->arg(0)))) {
                    instResult = builder->CreateLoad(builder->getPtrTy(), addr, "load.slot");
                }
                // 嵌入结构体字段：不做整体读取，直接以字段地址代表该结构体值（按地址传递，store 时再按值拷贝）
                else if (isRawFieldAddress(ins->arg(0)) && storageType(ins)->isStruct()) {
                    instResult = addr;
                }
                // 嵌入数组字段不支持整体读取，只能按下标访问元素，因此显式报错
                else if (isRawFieldAddress(ins->arg(0)) && storageType(ins)->isArray()) {
                    throw std::runtime_error("Whole embedded array materialization is not supported; use indexed elements");
                }
                // 原始存储地址（结构体标量字段、字符元素等）：按 storage 类型读出原始值，
                // 若运行时表示为装箱(Boxed)则装箱为新的 RuntimeValue*
                else {
                    auto* type = storageType(ins);
                    auto* raw = builder->CreateLoad(map(type, *context), addr, "load.raw");
                    if (runtimeType(ins).representation == IR::ValueRepresentation::Boxed) {
                        instResult = boxRaw(raw, runtimeType(ins), curFn);
                        // 字符串字段的 storage 只是字符指针，装箱后还需补上 RuntimeString.length：
                        // 对 null 指针用空串代替调用 strlen（避免空指针解引用），
                        // 把长度写到 RuntimeValue.data 的第二个 i64（即 data.string.length）
                        if (runtimeType(ins).kind == IR::RuntimeDescriptionKind::String) {
                            auto callee = curFn->parent->content->getOrInsertFunction("strlen",
                                builder->getInt64Ty(), builder->getPtrTy());
                            auto* characters = builder->CreateSelect(builder->CreateIsNull(raw),
                                builder->CreateGlobalString("", "empty.field.string"), raw);
                            auto* length = builder->CreateCall(callee, {characters}, "field.string.length");
                            auto* payload = builder->CreateStructGEP(runtimeValueType(), instResult, 2);
                            builder->CreateStore(length, builder->CreateGEP(builder->getInt64Ty(), payload,
                                builder->getInt64(1), "field.string.length.slot"));
                        }
                    }
                    // 非装箱表示（如原始指针/handle）直接使用读出的原始值
                    else {
                        instResult = raw;
                    }
                }

                bind(ins, instResult);
                break;
            }
            // br：无条件跳转。目标基本块已由 FunctionEmiiter::codegen 预先创建并 bind，这里直接取出生成 br
            case IR::OpKind::br: {
                auto targetBlockValue = toLLVMValue(ins->arg(0), curFn);


                llvm::BasicBlock* targetBlock = llvm::cast<llvm::BasicBlock>(targetBlockValue);

                instResult = builder->CreateBr(targetBlock);

                bind(ins, instResult);
                break;
            }
            // cond_br：条件跳转。条件值拆箱为 i1，两个目标块同样来自预先创建的 BasicBlock 映射
            case IR::OpKind::cond_br: {
                auto cond = rawValue(ins->arg(0), curFn);
                auto trueBlockValue = toLLVMValue(ins->arg(1), curFn);
                auto falseBlockValue = toLLVMValue(ins->arg(2), curFn);

                llvm::BasicBlock* trueBlock = llvm::cast<llvm::BasicBlock>(trueBlockValue);
                llvm::BasicBlock* falseBlock = llvm::cast<llvm::BasicBlock>(falseBlockValue);

                instResult = builder->CreateCondBr(cond, trueBlock, falseBlock);

                bind(ins, instResult);
                break;
            }
            // ret：函数返回。返回前撤销本函数打开的所有 GC root 作用域（含函数级作用域），
            // 保证 root stack 在离开函数时与进入前一致
            case IR::OpKind::ret: {
                curFn->gcLeaveAllScopes();

                if (ins->getOperands().empty()) {
                    instResult = builder->CreateRetVoid();

                    bind(ins, instResult);
                }
                else {
                    llvm::Value* retVal = toLLVMValue(ins->arg(0), curFn);
                    instResult = builder->CreateRet(retVal);

                    bind(ins, instResult);
                }
                break;
            }
            // call：函数调用。按参数顺序求值实参，必要时对托管实参做 GC root 保护，再生成 LLVM call
            case IR::OpKind::call: {
                // 指令名形如 "call.<函数名>"，去掉前 5 个字符的 "call." 前缀得到被调函数名，再在当前模块中查找
                auto insName = ins->getName();
                auto fnName = insName.slice(5, insName.len() - 5);

                auto fn = curFn->parent->lookup(fnName)->content;

                const auto& arguments = ins->getOperands();
                std::vector<llvm::Value*> llvmArguments;
                bool openedTempScope = false;
                for (std::size_t i = 0; i < arguments.size(); i ++) {
                    // 实参若是槽位地址（变量/参数），传入的是槽位中存放的值而非槽位本身
                    auto argVal = toLLVMValue(arguments[i], curFn);
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(argVal)) {
                        llvm::Type* allocatedType = allocaInst->getAllocatedType();
                        argVal = builder->CreateLoad(allocatedType, allocaInst, "call.arg.load");
                    }

                    /* 如果某个参数是 GC 对象引用，而后面还有新的参数求值或被调用者内部发生分配
                     * 就必须先 spill 到已注册 root 的临时槽位中，避免它在调用期间被误回收
                     */
                    if (curFn->shouldTrackAsGCRoot(arguments[i])) {
                        if (!openedTempScope) {
                            curFn->gcEnterScope();
                            openedTempScope = true;
                        }

                        auto* rootedSlot = curFn->createRootedTemporary(
                            argVal,
                            "gc.call.arg",
                            runtimeType(arguments[i]));
                        argVal = builder->CreateLoad(rootedSlot->getAllocatedType(), rootedSlot, "call.arg.rooted");
                    }

                    llvmArguments.push_back(argVal);
                }

                // void 返回的调用不能带结果名，否则 LLVM 会报错
                if (fn->getReturnType()->isVoidTy())
                    instResult = builder->CreateCall(fn, llvmArguments);
                else
                    instResult = builder->CreateCall(fn, llvmArguments, ins->getName().c_str());

                // 调用结束后撤销保护实参的临时 root 作用域
                if (openedTempScope) {
                    curFn->gcLeaveScope();
                }

                // 返回值若需作为 GC root 跟踪，spill 到已注册 root 的槽位，后续引用都从该槽位重新 load
                if (instResult && curFn->shouldTrackAsGCRoot(ins)) {
                    auto* protectedSlot = curFn->createRootedTemporary(
                        instResult,
                        "gc.call.result",
                        runtimeType(ins));
                    protectValue(ins, protectedSlot);
                }

                bind(ins, instResult);
                break;
            }
            // enter_scope / leave_scope：语言层块作用域边界，对应 GC root 作用域的进入与撤销
            case IR::OpKind::enter_scope: {
                curFn->gcEnterScope();
                break;
            }
            case IR::OpKind::leave_scope: {
                curFn->gcLeaveScope();
                break;
            }
            // empty：占位指令，不生成任何代码
            case IR::OpKind::empty:
                break;
            // 其余 OpKind 尚无 LLVM 生成实现，显式报错并带上指令种类和所在函数名
            default:
                throw std::runtime_error(("Unsupported LLVM instruction: " +
                    fzlib::String(magic_enum::enum_name(ins->getKind())) + " in " + curFn->backend.sourceName).c_str());
        }
        // 统一缓存本指令结果（终结指令/void 调用的结果同样会被缓存，以防重复生成）
        if (instResult)
            bind(ins, instResult);
        return instResult;
    }

    // LLVMCodegen 开始
    // 代码生成主入口：
    // 1. 从根 BackendModule 出发按 usingModules 广度优先收集所有可达模块（visited 去重，避免重复/循环依赖），
    //    每个模块创建一个 ModuleEmmiter；
    // 2. 先对所有模块执行 impl()：创建 llvm::Module 与全部函数声明，使跨模块调用可以先解析到函数；
    // 3. 再逐模块执行 codegen() 生成函数体，并用 verifyModule 校验，失败时抛出带全部模块 IR 的错误
    void LLVMCodeGenerator::start() {
        std::vector<const IR::BackendModule*> reachable{&rootModule};
        std::unordered_set<const IR::BackendModule*> visited{&rootModule};
        for (std::size_t i = 0; i < reachable.size(); ++i) {
            const auto* source = reachable[i];
            modules.push_back(new ModuleEmmiter(*source, *this));
            for (const auto* dependency : source->usingModules) {
                if (visited.insert(dependency).second) {
                    reachable.push_back(dependency);
                }
            }
        }

        for (auto* module : modules) {
            module->impl();
        }

        for (auto mod: modules) {
            mod->codegen();
            std::string stdstr;
            llvm::raw_string_ostream rstrs(stdstr);
            if (llvm::verifyModule(*mod->content, &rstrs)) {
                rstrs.flush();
                throw std::runtime_error((fzlib::String("LLVM Verification Failed for module " +
                    mod->content->getName().str() + ":\n" + stdstr + "\n Error IR: \n") + this->toString()).c_str());
            }
        }
    }

    // 调试输出：将所有模块的 LLVM IR 打印到标准输出
    void LLVMCodeGenerator::print() {
        for (auto mod: modules) {
            mod->content->print(llvm::outs(), nullptr);
        }
    }

    // 将所有模块的 LLVM IR 拼接为字符串，用于函数/模块校验失败时附带到错误信息中
    fzlib::String LLVMCodeGenerator::toString() {
        std::string stdstr;
        llvm::raw_string_ostream rstrs(stdstr);
        for (auto mod: modules) {
            mod->content->print(rstrs, nullptr);
        }
        rstrs.flush();
        return fzlib::String(stdstr);
    }
}
