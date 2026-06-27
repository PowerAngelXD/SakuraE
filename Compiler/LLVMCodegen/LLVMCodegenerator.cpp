#include "LLVMCodegenerator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/module.hpp"
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
    // LLVM Module
    void LLVMCodeGenerator::LLVMModule::impl(IR::Module* source) {
        content = new llvm::Module(ID.c_str(), *codegenContext.context);

        auto funcs = source->getFunctions();

        for (auto func: funcs) {
            auto retTy = func->getReturnType()->toLLVMType(*codegenContext.context);
            auto irParams = func->getFormalParams();
            std::vector<std::pair<fzlib::String, llvm::Type*>> params;

            for (auto param: irParams) {
                params.emplace_back(param.first, param.second->toLLVMType(*codegenContext.context));
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

                auto retTy = func->getReturnType()->toLLVMType(*codegenContext.context);
                auto irParams = func->getFormalParams();
                std::vector<std::pair<fzlib::String, llvm::Type*>> params;

                for (auto param: irParams) {
                    params.emplace_back(param.first, param.second->toLLVMType(*codegenContext.context));
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

    // LLVM Function
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
        gcEnterScope();

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            arg.setName(irParams[i].first.c_str());

            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            paramAllocaMap[irParams[i].first.c_str()] = argAlloca;

            codegenContext.builder->CreateStore(&arg, argAlloca);

            // 参数如果承载的是 GC 托管对象引用，需要在函数入口立即注册进 root stack。
            if (shouldRegisterSlotAsGCRoot(irParams[i].second)) {
                gcRegisterRoot(argAlloca);
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
    }

    // Instruction generation
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
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = add(lhs, rhs);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::sub: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = sub(lhs, rhs);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mul: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = mul(lhs, rhs);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::div: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = div(lhs, rhs);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::mod: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = mod(lhs, rhs);

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::lgc_equal:
            case IR::OpKind::lgc_not_equal:
            case IR::OpKind::lgc_mr_than:
            case IR::OpKind::lgc_ls_than:
            case IR::OpKind::lgc_eq_mr_than:
            case IR::OpKind::lgc_eq_ls_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);

                instResult = compare(lhs, rhs, ins->arg(0)->getType(), ins->arg(1)->getType(), ins->getKind(), curFn);
                break;
            }
            case IR::OpKind::create_alloca: {
                auto insName = ins->getName();
                auto identifierName = insName.split('.')[1];

                auto idIRType = ins->getType();
                if (idIRType->isComplexType()) {
                    idIRType = IR::IRType::getPointerTo(idIRType);
                }

                auto identifierType = idIRType->toLLVMType(*context);

                llvm::AllocaInst* alloca = curFn->createAlloca(identifierType, nullptr, identifierName);

                auto initVal = ins->arg(0);
                if (initVal) {
                    builder->CreateStore(toLLVMValue(initVal, curFn), alloca);
                }
                else {
                    builder->CreateStore(llvm::Constant::getNullValue(identifierType), alloca);
                }

                if (curFn->shouldRegisterSlotAsGCRoot(ins->getType())) {
                    curFn->gcRegisterRoot(alloca);
                }

                bind(ins, alloca);
                curFn->scope.declare(identifierName, alloca, nullptr);

                break;
            }
            case IR::OpKind::store: {
                llvm::Value* destAddr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* srcVal = toLLVMValue(ins->arg(1), curFn);

                if (destAddr && srcVal) {
                    builder->CreateStore(srcVal, destAddr);
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

                        auto* rootedSlot = curFn->createRootedTemporary(elementValue, "gc.array.elem");
                        elementValue = builder->CreateLoad(rootedSlot->getAllocatedType(), rootedSlot, "array.elem.rooted");
                    }

                    arrayContent.push_back(elementValue);
                }
                auto arrayType = ins->getType()->toLLVMType(*context);
                auto elementType = arrayType->getArrayElementType();  

                // array object 的 payload 是实际数组内容，header 中只记录扫描规则与元素个数。
                llvm::Value* gcType = curFn->parent->llvmTy2GCType(arrayType);
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
                    auto* protectedSlot = curFn->createRootedTemporary(arrayPtr, "gc.array.result");
                    protectValue(ins, protectedSlot);
                }

                bind(ins, arrayPtr);
                break;
            }
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = toLLVMValue(ins->arg(1), curFn);

                auto addrIRType = ins->arg(0)->getType();
                llvm::Type* elementType = nullptr;
                auto* addrInst = dynamic_cast<IR::Instruction*>(ins->arg(0));
                bool baseIsLValue = addrInst && addrInst->isLValue();

                if (addrIRType->isArray()) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.array.base");
                    }
                    elementType = static_cast<IR::IRArrayType*>(addrIRType)->getElementType()->toLLVMType(*context);
                }
                else if (addrIRType->isString()) {
                    if (baseIsLValue) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr, "indexing.string.base");
                    }
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
                        auto* arrayTy = static_cast<IR::IRArrayType*>(refElementTy);
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.array.base");
                        elementType = arrayTy->getElementType()->toLLVMType(*context);
                    }
                    else if (refElementTy->isString()) {
                        addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), refAddr, "indexing.ref.string.base");
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
            case IR::OpKind::gaddr: {
                bind(ins, toLLVMValue(ins->arg(0), curFn));
                break;
            }
            case IR::OpKind::deref: {
                instResult = toLLVMValue(ins->arg(0), curFn);

                if (ins->arg(0)->getType()->isRef()) {
                    auto ty = ins->getType()->toLLVMType(*context);
                    instResult = builder->CreateLoad(ty, instResult);
                }

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::load: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Type* type = ins->getType()->toLLVMType(*context);

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
                auto cond = toLLVMValue(ins->arg(0), curFn);
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

                    // 某个参数如果是 GC 对象引用，而后面还有新的参数求值或 callee 内部分配，
                    // 就必须先 spill 到一个已注册 root 的临时槽位里，避免它在调用期间被误回收。
                    if (curFn->shouldTrackAsGCRoot(arguments[i])) {
                        if (!openedTempScope) {
                            curFn->gcEnterScope();
                            openedTempScope = true;
                        }

                        auto* rootedSlot = curFn->createRootedTemporary(argVal, "gc.call.arg");
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
                    auto* protectedSlot = curFn->createRootedTemporary(instResult, "gc.call.result");
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

    // LLVMCodegen start
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

    // Debug print
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
