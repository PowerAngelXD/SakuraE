#include "LLVMCodegenerator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/model/instruction.hpp"
#include "Compiler/IR/model/module.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/value/array.hpp"
#include "Compiler/IR/value/constant.hpp"
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
    // LLVM 模块
    void LLVMCodeGenerator::LLVMModule::impl(IR::Module* source) {
        content = new llvm::Module(ID.c_str(), *codegenContext.context);

        auto funcs = source->getFunctions();

        for (auto func: funcs) {
            auto retTy = codegenContext.abiType(func->getReturnType(), func->getRawName());
            auto irParams = func->getFormalParams();
            std::vector<std::pair<fzlib::String, llvm::Type*>> params;

            for (auto param: irParams) {
                params.emplace_back(param.first, codegenContext.abiParamType(param.second, func->getRawName()));
            }

            if (source->id() == "__runtime")
                declareFunction(FunctionType::ExternalLinkage, func->getName(), func->getRawName(), retTy, params, func->getInfo());
            else
                declareFunction(FunctionType::Definition, func->getName(), retTy, params, func->getInfo());
        }

        for (auto usingMod: source->getUsingList()) {
            for (auto func: usingMod->getFunctions()) {
                if (fnMap.contains(func->getName())) {
                    throw SakuraError(OccurredTerm::COMPILING,
                                    "Duplicate declaration of '" + func->getName() + "', originating from module '" + usingMod->id() + "'.",
                                    func->getInfo());
                }

                auto retTy = codegenContext.abiType(func->getReturnType(), func->getRawName());
                auto irParams = func->getFormalParams();
                std::vector<std::pair<fzlib::String, llvm::Type*>> params;

                for (auto param: irParams) {
                    params.emplace_back(param.first, codegenContext.abiParamType(param.second, func->getRawName()));
                }

                declareFunction(FunctionType::ExternalLinkage, func->getName(), func->getRawName(), retTy, params, func->getInfo());
                lookup(func->getName())->impl(func);

            }
        }

        for (auto irFn: funcs) {
            lookup(irFn->getName())->impl(irFn);
        }

        sourceModule = source;
    }

    void LLVMCodeGenerator::LLVMModule::codegen() {
        auto funcList = sourceModule->getFunctions();
        for (auto fn: funcList) {
            LLVMFunction* curFn = lookup(fn->getName());

            if (curFn->type == FunctionType::Definition)
                curFn->codegen();

            std::string stdstr;
            llvm::raw_string_ostream rstrs(stdstr);
            if (llvm::verifyFunction(*curFn->content, &rstrs)) {
                rstrs.flush();
                throw std::runtime_error((fzlib::String("LLVM Verification Failed for module " +
                    curFn->content->getName().str() + ":\n" + stdstr + "\n Error IR: \n") + this->codegenContext.toString()).c_str());
            }
        }
    }

    // LLVM 函数
    void LLVMCodeGenerator::LLVMFunction::impl(IR::Function* source) {
        sourceFn = source;

        std::vector<llvm::Type*> params;
        for (auto param: formalParams) {
            params.push_back(param.second);
        }

        llvm::FunctionType* fnType = llvm::FunctionType::get(returnType, params, false);
        content = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, linkageName.c_str(), parent->content);

        if (type == FunctionType::ExternalLinkage) return ;

        auto irParams = source->getFormalParams();

        for (auto block: source->getBlocks()) {
            llvm::BasicBlock* llvmBlock = llvm::BasicBlock::Create(*codegenContext.context, block->getName().c_str(), content);
            codegenContext.bind(block, llvmBlock);

            if (block->getName() == "entry") {
                entryBlock = llvmBlock;
            }
        }

        codegenContext.builder->SetInsertPoint(entryBlock);
        // 函数级 root scope 覆盖参数、局部变量和所有临时 root
        gcEnterScope();

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            arg.setName(irParams[i].first.c_str());

            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            paramAllocaMap[irParams[i].first.c_str()] = argAlloca;

            codegenContext.builder->CreateStore(&arg, argAlloca);

            // 参数如果承载的是 GC 托管对象引用，需要在函数入口立即注册进 root stack
            if (shouldRegisterSlotAsGCRoot(irParams[i].second)) {
                gcRegisterManagedSlot(argAlloca, irParams[i].second);
            }

            scope.declare(irParams[i].first, argAlloca, nullptr);
            i ++;
        }
    }

    void LLVMCodeGenerator::LLVMFunction::codegen() {
        auto irBlocks = sourceFn->getBlocks();
        for (auto irBlock: irBlocks) {
            codegenContext.builder->SetInsertPoint(llvm::cast<llvm::BasicBlock>(codegenContext.toLLVMValue(irBlock, this)));

            for (auto inst: irBlock->getInstructions()) {
                codegenContext.instgen(inst, this);
            }
        }

        /* 允许 main 函数自然结束，源语言要求 main 返回 i32，而 LLVM ABI 返回装箱后的
         * RuntimeValue*，这对只用于输出结果的小型运行时示例很有用
         */
        if (sourceFn->getName() == "main") {
            for (auto& block: *content) {
                if (block.getTerminator()) {
                    continue;
                }
                codegenContext.builder->SetInsertPoint(&block);
                auto* boxedZero = codegenContext.boxRaw(
                    codegenContext.builder->getInt32(0),
                    IR::IRType::getInt32Ty(), this);
                codegenContext.builder->CreateRet(boxedZero);
            }
        }
    }

    // 指令生成
    llvm::Value* LLVMCodeGenerator::instgen(IR::Instruction* ins, LLVMFunction* curFn) {
        llvm::Value* instResult = nullptr;
        if (hasLLVMValue(ins)) return toLLVMValue(ins, curFn);
        switch (ins->getKind())
        {
            case IR::OpKind::constant: {
                auto constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                auto llvmConst = toLLVMConstant(constant, curFn);
                bind(ins, llvmConst);
                return toLLVMConstant(constant, curFn);
            }
            case IR::OpKind::add: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(add(lhs, rhs), ins->getType(), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::sub: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(sub(lhs, rhs), ins->getType(), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mul: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(mul(lhs, rhs), ins->getType(), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::div: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(div(lhs, rhs), ins->getType(), curFn);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mod: {
                llvm::Value* lhs = rawValue(ins->arg(0), curFn);
                llvm::Value* rhs = rawValue(ins->arg(1), curFn);

                instResult = boxRaw(mod(lhs, rhs), ins->getType(), curFn);

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

                instResult = boxRaw(compare(lhs, rhs, ins->arg(0)->getType(), ins->arg(1)->getType(), ins->getKind(), curFn), ins->getType(), curFn);
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
                else if (ins->getType()->isArray()) {
                    auto* sourceArrayType = static_cast<IR::IRArrayType*>(ins->getType());
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

                if (curFn->shouldRegisterSlotAsGCRoot(ins->getType())) {
                    curFn->gcRegisterManagedSlot(alloca, ins->getType());
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
                    if (llvm::isa<llvm::AllocaInst>(destAddr)) {
                        builder->CreateStore(srcVal, destAddr);
                    }
                    else if (auto* destination = dynamic_cast<IR::Instruction*>(ins->arg(0));
                             destination && destination->getKind() == IR::OpKind::indexing &&
                             destination->arg(0)->getType()->isArray()) {
                        // 托管数组的元素是 RuntimeValue*，因此索引赋值需要替换包装指针
                        builder->CreateStore(srcVal, destAddr);
                    }
                    else {
                        builder->CreateStore(unboxRaw(srcVal, ins->arg(1)->getType(), curFn), destAddr);
                    }
                    bind(ins, srcVal);
                }
                else {
                    throw std::runtime_error("Assign failed: Null operand.");
                }
                break;
            }
            case IR::OpKind::create_array: {
                auto value = ins->arg(0);
                auto irArrayConst = dynamic_cast<IR::Constant*>(value);
                auto irArray = irArrayConst->getContentValue<IR::IRArray*>();

                std::vector<llvm::Value*> arrayContent;
                bool openedTempScope = false;
                for (auto element: irArray->getArray()) {
                    llvm::Value* elementValue = toLLVMValue(element, curFn);

                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(elementValue)) {
                        llvm::Type* allocatedType = allocaInst->getAllocatedType();
                        elementValue = builder->CreateLoad(allocatedType, allocaInst, "array.elem.load");
                    }

                    if (curFn->shouldTrackAsGCRoot(element)) {
                        if (!openedTempScope) {
                            curFn->gcEnterScope();
                            openedTempScope = true;
                        }

                        auto* rootedSlot = curFn->createRootedTemporary(
                            elementValue,
                            "gc.array.elem",
                            element->getType());
                        elementValue = builder->CreateLoad(rootedSlot->getAllocatedType(), rootedSlot, "array.elem.rooted");
                    }

                    arrayContent.push_back(elementValue);
                }
                /* 完全装箱模式下，数组的每个元素都存储为 RuntimeValue*，与源元素类型无关
                 * 分配大小和元素寻址都必须使用装箱后的布局
                 */
                auto* sourceArrayType = static_cast<IR::IRArrayType*>(ins->getType());
                auto arrayType = llvm::ArrayType::get(
                    llvm::PointerType::getUnqual(*context),
                    sourceArrayType->getNumElements());
                auto elementType = arrayType->getArrayElementType();

                /* array 对象的 payload 是实际数组内容，header 中只记录扫描规则与元素个数
                 * 元素求值阶段先建立临时 root，确保构造数组期间的嵌套对象不会被回收
                 * 完全装箱模式下数组元素是 RuntimeValue*，GC 必须先检查每个包装对象，
                 * 再继续追踪其中的 payload
                 */
                llvm::Value* gcType = curFn->parent->getRuntimeValueArrayGCType(
                    static_cast<uint32_t>(curFn->parent->content->getDataLayout().getTypeAllocSize(elementType)),
                    irArray->getSize());
                llvm::Value* elemCount = builder->getInt64(irArray->getSize());
                llvm::Value* arrayPtr = curFn->createHeapAlloc(arrayType, gcType, elemCount);

                for (std::size_t i = 0; i < arrayContent.size(); i ++) {
                    auto ptr = builder->CreateGEP(elementType,
                                                            arrayPtr,
                                                            {builder->getInt32(i)});
                    builder->CreateStore(arrayContent[i], ptr);
                }

                if (openedTempScope) {
                    curFn->gcLeaveScope();
                }

                if (curFn->shouldTrackAsGCRoot(ins)) {
                    auto* protectedSlot = curFn->createRootedTemporary(
                        arrayPtr,
                        "gc.array.result",
                        ins->getType());
                    protectValue(ins, protectedSlot);
                }

                bind(ins, arrayPtr);
                break;
            }
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = rawValue(ins->arg(1), curFn);

                auto addrIRType = ins->arg(0)->getType();
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
                    switch (ins->arg(1)->getType()->getIRTypeID()) {
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
                else if (addrIRType->isString()) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.string.base");
                    }
                    // 语言层字符串是 RuntimeValue*，索引操作作用于其底层字符 payload
                    addr = unboxRaw(addr, IR::IRType::getStringTy(), curFn);
                    elementType = IR::IRType::getCharTy()->toLLVMType(*context);
                }
                else if (addrIRType->isPointer()) {
                    auto* ptrTy = static_cast<IR::IRPointerType*>(addrIRType);
                    auto* pointeeTy = ptrTy->getElementType();

                    if (!pointeeTy) {
                        throw std::runtime_error("Indexing failed: pointer operand has no element type.");
                    }
                    if (!curFn->isRawCharPointerType(addrIRType)) {
                        throw std::runtime_error(
                            "Indexing failed: only character pointers are currently supported for pointer indexing."
                        );
                    }

                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.ptr.base");
                    }
                    // 指针变量存储装箱后的指针值，索引操作需要取得 RuntimeValue::Pointer 中的实际地址
                    addr = unboxRaw(addr, addrIRType, curFn);
                    elementType = pointeeTy->toLLVMType(*context);
                }
                else if (addrIRType->isRef()) {
                    auto* refTy = static_cast<IR::IRRefType*>(addrIRType);
                    auto* refElementTy = refTy->getElementType();
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
                    else if (refElementTy->isString()) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.string.base");
                        addr = unboxRaw(addr, IR::IRType::getStringTy(), curFn);
                        elementType = IR::IRType::getCharTy()->toLLVMType(*context);
                    }
                    else if (curFn->isRawCharPointerType(refElementTy)) {
                        auto* ptrTy = static_cast<IR::IRPointerType*>(refElementTy);
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.ptr.base");
                        elementType = ptrTy->getElementType()->toLLVMType(*context);
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
                break;
            }
            case IR::OpKind::_sizeof: {
                auto* sourceType = ins->getTypeOperand();
                if (!sourceType) {
                    throw std::runtime_error("sizeof instruction is missing its source type.");
                }

                auto* llvmType = sourceType->toLLVMType(*context);
                auto size = curFn->parent->content->getDataLayout().getTypeAllocSize(llvmType);
                instResult = boxRaw(
                    builder->getInt64(static_cast<std::uint64_t>(size)),
                    ins->getType(),
                    curFn
                );
                bind(ins, instResult);
                break;
            }
            case IR::OpKind::gaddr: {
                auto* address = toLLVMValue(ins->arg(0), curFn);

                /* 一等指针是装箱后的 RuntimeValue，payload 是取地址表达式表示的实际地址
                 * 传递的不是存储槽位本身的 RuntimeValue*
                 */
                if (ins->getType()->isPointer()) {
                    bind(ins, boxRaw(address, ins->getType(), curFn));
                }
                else {
                    bind(ins, address);
                }
                break;
            }
            case IR::OpKind::deref: {
                instResult = toLLVMValue(ins->arg(0), curFn);

                if (ins->arg(0)->getType()->isPointer()) {
                    /* 指针表达式在语言值边界处进行装箱
                     * 解引用操作作用于其中存储的地址
                     */
                    instResult = unboxRaw(instResult, ins->arg(0)->getType(), curFn);
                }
                else if (ins->arg(0)->getType()->isRef()) {
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
                llvm::Type* type = ins->getType()->toLLVMType(*context);

                sutils::println("try to load:" + ins->getType()->toString() + ", Name: " + ins->getName());

                instResult = builder->CreateLoad(type, addr, "load.tmp");

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
                auto fnName = insName.split('.')[1];

                auto fn = curFn->parent->lookup(fnName)->content;

                auto arguments = ins->getOperands();
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
                            arguments[i]->getType());
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
                        ins->getType());
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
            default:
                break;
        }
        if (instResult)
            bind(ins, instResult);
        return instResult;
    }

    // LLVMCodegen 开始
    void LLVMCodeGenerator::start() {
        auto irModList = program->getMods();
        for (auto mod: irModList) {
            modules.push_back(new LLVMModule(mod->id(), *context, *this));
        }

        for (std::size_t i = 0; i < modules.size(); i ++) {
            modules[i]->impl(irModList[i]);
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
