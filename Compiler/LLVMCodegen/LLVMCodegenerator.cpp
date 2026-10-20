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
    llvm::Value* LLVMCodeGenerator::emitArrayConstant(IR::Constant* constant, FunctionEmiiter* curFn) {
        if (curFn->backend.constants.at(constant) != IR::ConstantKind::Array) {
            throw std::runtime_error("Array emitter requires a lowered array constant");
        }
        const auto* irArray = constant->getArrayPayload();
        if (!irArray) throw std::runtime_error("Array constant has no payload");
        const auto& type = runtimeType(constant);
        if (type.representation != IR::ValueRepresentation::ManagedArray || type.count != irArray->getArray().size()) {
            throw std::runtime_error("Array constant payload does not match its lowered representation");
        }
        std::vector<llvm::Value*> arrayContent;
        arrayContent.reserve(irArray->getArray().size());
        bool openedTempScope = false;
        for (auto* element : irArray->getArray()) {
            auto* elementValue = toLLVMValue(element, curFn);
            if (auto* slot = llvm::dyn_cast<llvm::AllocaInst>(elementValue)) {
                elementValue = builder->CreateLoad(slot->getAllocatedType(), slot, "array.elem.load");
            }
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
        // Managed array payloads hold value/array pointers, never unboxed scalar storage.
        auto* elementType = builder->getPtrTy();
        auto* arrayType = llvm::ArrayType::get(elementType, type.count);
        auto* gcType = curFn->parent->getRuntimeValueArrayGCType(
            static_cast<uint32_t>(curFn->parent->content->getDataLayout().getTypeAllocSize(elementType)), type.count);
        auto* arrayPtr = curFn->createHeapAlloc(arrayType, gcType, builder->getInt64(type.count));
        for (std::size_t i = 0; i < arrayContent.size(); ++i) {
            auto* slot = builder->CreateGEP(elementType, arrayPtr, {builder->getInt64(i)});
            builder->CreateStore(arrayContent[i], slot);
        }
        if (openedTempScope) curFn->gcLeaveScope();
        return arrayPtr;
    }

    llvm::Value* LLVMCodeGenerator::instgen(IR::Instruction* ins, FunctionEmiiter* curFn) {
        llvm::Value* instResult = nullptr;
        if (hasLLVMValue(ins)) return toLLVMValue(ins, curFn);
        switch (ins->getKind())
        {
            case IR::OpKind::constant: {
                auto constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                if (!constant) throw std::runtime_error("Constant instruction has no constant operand");
                instResult = toLLVMConstant(constant, curFn);
                break;
            }
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
            case IR::OpKind::create_alloca: {
                auto insName = ins->getName();
                auto identifierName = insName.split('.')[1];

                auto identifierType = llvm::PointerType::getUnqual(*context);

                llvm::AllocaInst* alloca = curFn->createAlloca(identifierType, nullptr, identifierName);

                auto initVal = ins->arg(0);
                if (initVal) {
                    builder->CreateStore(toLLVMValue(initVal, curFn), alloca);
                }
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
                else {
                    builder->CreateStore(llvm::Constant::getNullValue(identifierType), alloca);
                }

                if (curFn->backend.rootsSlot(ins)) {
                    curFn->gcRegisterManagedSlot(alloca, runtimeType(ins));
                }

                bind(ins, alloca);
                curFn->scope.declare(identifierName, alloca, nullptr);

                break;
            }
            case IR::OpKind::store: {
                IR::IRValue* irAddr = ins->arg(0);
                llvm::Value* destAddr = toLLVMValue(irAddr, curFn);
                llvm::Value* srcVal = toLLVMValue(ins->arg(1), curFn);

                if (destAddr && srcVal) {
                    if (isPointerSlot(dynamic_cast<const IR::Instruction*>(irAddr))) {
                        builder->CreateStore(srcVal, destAddr);
                    }
                    else {
                        auto* raw = runtimeType(ins->arg(1)).representation == IR::ValueRepresentation::Boxed
                            ? unboxRaw(srcVal, storageType(ins->arg(1)), curFn) : srcVal;
                        builder->CreateStore(raw, destAddr);
                    }
                    bind(ins, srcVal);
                }
                else {
                    throw std::runtime_error("Assign failed: Null operand.");
                }
                break;
            }
            case IR::OpKind::create_array: {
                auto* constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                if (!constant) throw std::runtime_error("Array instruction has no constant operand");
                auto* arrayPtr = emitArrayConstant(constant, curFn);


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
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = rawValue(ins->arg(1), curFn);

                auto addrIRType = storageType(ins->arg(0));
                const auto& addrRuntimeType = runtimeType(ins->arg(0));
                sutils::println("addr IR type: " + addrIRType->toString());
                llvm::Type* elementType = nullptr;
                auto* addrInst = dynamic_cast<IR::Instruction*>(ins->arg(0));
                bool baseIsLValue = addrInst && addrInst->isLValue();

                if (addrIRType->isArray()) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.array.base");
                    }
                    auto* arrayType = static_cast<IR::IRArrayType*>(addrIRType);
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
                else if (addrRuntimeType.kind == IR::RuntimeDescriptionKind::String) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.string.base");
                    }
                    // 语言层字符串是 RuntimeValue*，索引操作作用于其底层字符 payload
                    addr = unboxRaw(addr, addrIRType, curFn);
                    elementType = map(rootModule.context->getCharTy(), *context);
                }
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
                    addr = unboxRaw(addr, addrIRType, curFn);
                    elementType = map(pointeeTy, *context);
                }
                else if (addrRuntimeType.kind == IR::RuntimeDescriptionKind::Reference) {
                    const auto* refElement = addrRuntimeType.element;
                    auto* refElementTy = refElement ? refElement->storage : nullptr;
                    llvm::Value* refAddr = addr;

                    if (baseIsLValue) {
                        refAddr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.addr");
                    }

                    if (!refElementTy) {
                        throw std::runtime_error(
                            "Indexing failed: reference operand has no element type."
                        );
                    }

                    if (refElementTy->isArray()) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.array.base");
                        auto* boxedElement = builder->CreateGEP(
                            llvm::PointerType::getUnqual(*context), addr, {indexVal}, "indexing.ref.boxed.array");
                        instResult = boxedElement;
                        bind(ins, instResult);
                        break;
                    }
                    else if (refElement->kind == IR::RuntimeDescriptionKind::String) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.string.base");
                        addr = unboxRaw(addr, refElementTy, curFn);
                        elementType = map(rootModule.context->getCharTy(), *context);
                    }
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

                if (!addr) {
                    throw std::runtime_error("Indexing failed: null address operand.");
                }
                if (!elementType) {
                    throw std::runtime_error("Indexing failed: failed to resolve element type.");
                }

                auto ptr = builder->CreateGEP(elementType, addr, {indexVal}, "indexing.ptr");

                instResult = ptr;
                bind(ins, instResult);
                break;
            }
            case IR::OpKind::param: {
                auto insName = ins->getName();
                auto paramName = insName.split('.')[1];

                llvm::Value* realAddr = curFn->getParamAddress(paramName);

                bind(ins, realAddr);
                break;
            }
            case IR::OpKind::_typeof: {
                throw std::runtime_error("LLVM emission for typeof is deferred");
            }
            case IR::OpKind::_sizeof: {
                auto* llvmType = map(curFn->backend.typeOperands.at(ins)->storage, *context);
                auto size = curFn->parent->content->getDataLayout().getTypeAllocSize(llvmType);
                instResult = boxRaw(builder->getInt64(static_cast<std::uint64_t>(size)), runtimeType(ins), curFn);
                bind(ins, instResult);
                break;
            }
            case IR::OpKind::gaddr: {
                auto* address = toLLVMValue(ins->arg(0), curFn);

                /* 一等指针是装箱后的 RuntimeValue，payload 是取地址表达式表示的实际地址
                 * 传递的不是存储槽位本身的 RuntimeValue*
                 */
                if (storageType(ins)->isPointer()) {
                    bind(ins, boxRaw(address, runtimeType(ins), curFn));
                }
                else {
                    bind(ins, address);
                }
                break;
            }
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
            case IR::OpKind::load: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                if (isPointerSlot(dynamic_cast<const IR::Instruction*>(ins->arg(0)))) {
                    instResult = builder->CreateLoad(builder->getPtrTy(), addr, "load.slot");
                }
                else {
                    auto* type = storageType(ins);
                    auto* raw = builder->CreateLoad(map(type, *context), addr, "load.raw");
                    instResult = boxRaw(raw, runtimeType(ins), curFn);
                }

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::br: {
                auto targetBlockValue = toLLVMValue(ins->arg(0), curFn);

                auto currentBlock = llvm::cast<llvm::BasicBlock>(toLLVMValue(ins->getParent(), curFn));
                builder->SetInsertPoint(currentBlock);

                llvm::BasicBlock* targetBlock = llvm::cast<llvm::BasicBlock>(targetBlockValue);

                instResult = builder->CreateBr(targetBlock);

                bind(ins, instResult);
                break;
            }
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
            case IR::OpKind::call: {
                auto insName = ins->getName();
                auto fnName = insName.slice(5, insName.len() - 5);

                auto fn = curFn->parent->lookup(fnName)->content;

                const auto& arguments = ins->getOperands();
                std::vector<llvm::Value*> llvmArguments;
                bool openedTempScope = false;
                for (std::size_t i = 0; i < arguments.size(); i ++) {
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

                if (fn->getReturnType()->isVoidTy())
                    instResult = builder->CreateCall(fn, llvmArguments);
                else
                    instResult = builder->CreateCall(fn, llvmArguments, ins->getName().c_str());

                if (openedTempScope) {
                    curFn->gcLeaveScope();
                }

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
            case IR::OpKind::enter_scope: {
                curFn->gcEnterScope();
                break;
            }
            case IR::OpKind::leave_scope: {
                curFn->gcLeaveScope();
                break;
            }
            case IR::OpKind::empty:
                break;
            default:
                throw std::runtime_error(("Unsupported LLVM instruction: " +
                    fzlib::String(magic_enum::enum_name(ins->getKind())) + " in " + curFn->backend.sourceName).c_str());
        }
        if (instResult)
            bind(ins, instResult);
        return instResult;
    }

    // LLVMCodegen 开始
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

    // 调试输出
    void LLVMCodeGenerator::print() {
        for (auto mod: modules) {
            mod->content->print(llvm::outs(), nullptr);
        }
    }

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
