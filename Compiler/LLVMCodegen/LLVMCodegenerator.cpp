#include "LLVMCodegenerator.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include <cstddef>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>

namespace sakuraE::Codegen {
    void LLVMCodeGenerator::LLVMModule::impl() {

    }

    void LLVMCodeGenerator::LLVMFunction::impl() {
        std::vector<llvm::Type*> params;
        for (auto param: formalParams) {
            params.push_back(param.second);
        }

        llvm::FunctionType* fnType = llvm::FunctionType::get(returnType, params, false);
        content = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, name.c_str(), parent->content);

        auto irFunc = codegenContext.curIRModule()->curFunc();
        auto irParams = irFunc->getFormalParams();

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            llvm::AllocaInst* argAlloca = codegenContext.createAlloca(arg.getType(), nullptr, irParams[i].first);

            codegenContext.builder->CreateStore(&arg, argAlloca);

            scope.declare(irParams[i].first, argAlloca, nullptr);
            i ++;
        }

        for (auto block: irFunc->getBlocks()) {
            llvm::BasicBlock* llvmBlock = llvm::BasicBlock::Create(*codegenContext.context, block->getName().c_str(), content);
            codegenContext.bind(block, llvmBlock);

            if (block->getName() == "entry") {
                entryBlock = llvmBlock;
            }
        }

        codegenContext.builder->SetInsertPoint(entryBlock);
    }

    llvm::Value* LLVMCodeGenerator::instgen(IR::Instruction* ins) {
        llvm::Value* instResult = nullptr;
        switch (ins->getKind())
        {
            case IR::OpKind::constant: {
                auto constant = dynamic_cast<IR::Constant*>(ins->arg(0));
                return toLLVMConstant(constant);
            }
            case IR::OpKind::add: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));

                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateAdd(lhs, rhs, "add.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFAdd(promotedLhs, rhs, "add.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFAdd(lhs, promotedRhs, "add.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        instResult = builder->CreateFAdd(lhs, rhs, "add.tmp");
                    }
                }
                break;
            }
            case IR::OpKind::sub: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateSub(lhs, rhs, "sub.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFSub(promotedLhs, rhs, "sub.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFSub(lhs, promotedRhs, "sub.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        instResult = builder->CreateFSub(lhs, rhs, "sub.tmp");
                    }
                }
                break;
            }
            case IR::OpKind::mul: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));

                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateMul(lhs, rhs, "mul.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFMul(promotedLhs, rhs, "mul.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFMul(lhs, promotedRhs, "mul.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        instResult = builder->CreateFMul(lhs, rhs, "mul.tmp");
                    }
                }
                break;
            }
            case IR::OpKind::div: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));

                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateSDiv(lhs, rhs, "div.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFDiv(promotedLhs, rhs, "div.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFDiv(lhs, promotedRhs, "div.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        instResult = builder->CreateFDiv(lhs, rhs, "div.tmp");
                    }
                }
                break;
            }
            case IR::OpKind::lgc_equal: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpEQ(lhs, rhs, "eq.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpOEQ(promotedLhs, rhs, "eq.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpOEQ(lhs, promotedRhs, "eq.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpOEQ(lhs, rhs, "eq.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpEQ(lhs, rhs, "eq.tmp");
                }
                break;
            }
            case IR::OpKind::lgc_not_equal: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpNE(lhs, rhs, "neq.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpONE(promotedLhs, rhs, "neq.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpONE(lhs, promotedRhs, "neq.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpONE(lhs, rhs, "neq.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpEQ(lhs, rhs, "neq.tmp");
                }
                break;
            }
            case IR::OpKind::lgc_ls_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpSLT(lhs, rhs, "ls.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpOLT(promotedLhs, rhs, "ls.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpOLT(lhs, promotedRhs, "ls.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpOLT(lhs, rhs, "ls.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpULT(lhs, rhs, "ls.tmp");
                }
                break;
            }
            case IR::OpKind::lgc_mr_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpSGT(lhs, rhs, "gt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpOGT(promotedLhs, rhs, "gt.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpOGT(lhs, promotedRhs, "gt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpOGT(lhs, rhs, "gt.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpUGT(lhs, rhs, "gt.tmp");
                }
                break;
            }
            case IR::OpKind::lgc_eq_ls_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpSLE(lhs, rhs, "eqlt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpOLE(promotedLhs, rhs, "eqlt.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpOLE(lhs, promotedRhs, "eqlt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpOLE(lhs, rhs, "eqlt.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpULE(lhs, rhs, "eqlt.tmp");
                }
                break;
            }
            case IR::OpKind::lgc_eq_mr_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0));
                llvm::Value* rhs = toLLVMValue(ins->arg(1));
                if (lhs->getType()->isIntegerTy(32)) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        instResult = builder->CreateICmpSGE(lhs, rhs, "eqgt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        llvm::Value* promotedLhs = builder->CreateSIToFP(lhs, llvm::Type::getDoubleTy(*context), "lhs.promoted");
                        instResult = builder->CreateFCmpOGE(promotedLhs, rhs, "eqgt.tmp");
                    }
                }
                else if (lhs->getType()->isDoubleTy()) {
                    if (rhs->getType()->isIntegerTy(32)) {
                        llvm::Value* promotedRhs = builder->CreateSIToFP(rhs, llvm::Type::getDoubleTy(*context), "rhs.promoted");
                        instResult = builder->CreateFCmpOGE(lhs, promotedRhs, "eqgt.tmp");
                    }
                    else if (rhs->getType()->isDoubleTy()) {
                        builder->CreateFCmpOGE(lhs, rhs, "eqgt.tmp");
                    }
                }
                else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                    llvm::PointerType *ptrL = llvm::cast<llvm::PointerType>(lhs->getType());
                    llvm::PointerType *ptrR = llvm::cast<llvm::PointerType>(rhs->getType());
                    if (ptrL->getAddressSpace() != 0 || ptrR->getAddressSpace() != 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                            "Expected to compare two different space pointers",
                                            {0, 0, "SystemError"});
                    }
                    instResult = builder->CreateICmpUGE(lhs, rhs, "eqgt.tmp");
                }
                break;
            }
            case IR::OpKind::declare: {
                auto insName = ins->getName();
                auto identifierName = insName.split('.')[1];

                auto identifierType = ins->getType()->toLLVMType(*context);

                llvm::AllocaInst* alloca = createAlloca(identifierType, nullptr, identifierName);

                auto initVal = ins->arg(0);
                if (initVal) {
                    if (identifierType->isArrayTy()) {
                        auto arrSize = getCurrentUsingModule()->content->getDataLayout().getTypeAllocSize(identifierType);
                        builder->CreateMemCpy(alloca, llvm::MaybeAlign(8), toLLVMValue(initVal), llvm::MaybeAlign(8), arrSize);
                    }
                    else {
                        builder->CreateStore(toLLVMValue(initVal), alloca);
                    }
                }
                else {
                    builder->CreateStore(llvm::Constant::getNullValue(identifierType), alloca);
                }

                bind(ins, alloca);
                getCurrentUsingModule()->getActive()->scope.declare(identifierName, alloca, nullptr);

                break;
            }
            case IR::OpKind::assign: {
                auto insName = ins->arg(0)->getName();
                auto identifierName = insName.split('.')[1];

                // TODO: Type is not only AllocaInst
                auto alloca = llvm::dyn_cast<llvm::AllocaInst>(lookup<llvm::Value*>(identifierName)->address);
                auto val = toLLVMValue(ins->arg(1));

                if (alloca && val) {
                    // TODO: Ignore the different type (Assume they are the same)
                    if (val->getType()->isArrayTy()) {
                        auto arrSize = getCurrentUsingModule()->content->getDataLayout().getTypeAllocSize(val->getType());
                        builder->CreateMemCpy(alloca, llvm::MaybeAlign(8), val, llvm::MaybeAlign(8), arrSize);
                    }
                    else {
                        builder->CreateStore(val, alloca);
                    }
                }

                break;
            }
            case IR::OpKind::create_array: {
                auto irArray = ins->getOperands();
                std::vector<llvm::Value*> arrayContent;
                for (auto element: irArray) {
                    arrayContent.push_back(toLLVMValue(element));
                }
                auto arrayType = ins->getType()->toLLVMType(*context);

                llvm::AllocaInst* alloca = createAlloca(arrayType, nullptr, ins->getName());

                for (std::size_t i = 0; i < arrayContent.size(); i ++) {
                    auto ptr = builder->CreateGEP(arrayType, 
                                                            alloca, 
                                                            {builder->getInt32(0), builder->getInt32(i)});
                    builder->CreateStore(arrayContent[i], ptr);
                }

                bind(ins, alloca);
                break;
            }
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0));
                llvm::Value* indexVal = toLLVMValue(ins->arg(1));

                llvm::Type* type = ins->arg(0)->getType()->toLLVMType(*context);
                auto ptr = builder->CreateGEP(type, addr, {builder->getInt32(0), indexVal}, "element_ptr");
                
                auto result = builder->CreateLoad(type->getArrayElementType(), ptr, "element_val");

                bind(ins, result);
                break;
            }
            case IR::OpKind::load: {
                llvm::Value* addr = toLLVMValue(ins->arg(0));
                llvm::Type* type = ins->getType()->toLLVMType(*context);

                auto result = builder->CreateLoad(type, addr, "load.tmp");

                bind(ins, result);
                break;
            }
            case IR::OpKind::br: {
                auto targetBlockValue = toLLVMValue(ins->arg(0));

                llvm::BasicBlock* targetBlock = llvm::cast<llvm::BasicBlock>(targetBlockValue);

                auto result = builder->CreateBr(targetBlock);

                bind(ins, result);
                break;
            }
            case IR::OpKind::cond_br: {
                auto cond = toLLVMValue(ins->arg(0));
                auto trueBlockValue = toLLVMValue(ins->arg(1));
                auto falseBlockValue = toLLVMValue(ins->arg(2));

                llvm::BasicBlock* trueBlock = llvm::cast<llvm::BasicBlock>(trueBlockValue);
                llvm::BasicBlock* falseBlock = llvm::cast<llvm::BasicBlock>(falseBlockValue);

                auto result = builder->CreateCondBr(cond, trueBlock, falseBlock);

                bind(ins, result);
                break;
            }
            case IR::OpKind::ret: {
                if (ins->getOperands().empty()) {
                    auto result = builder->CreateRetVoid();

                    bind(ins, result);
                } 
                else {
                    llvm::Value* retVal = toLLVMValue(ins->arg(0));
                    auto result = builder->CreateRet(retVal);

                    bind(ins, result);
                }
                break;
            }
            case IR::OpKind::call: {
                auto insName = ins->getName();
                auto fnName = insName.split('.')[1];

                auto fn = getCurrentUsingModule()->get(fnName)->content;

                auto arguments = ins->getOperands();
                std::vector<llvm::Value*> llvmArguments;
                for (std::size_t i = 0; i < arguments.size(); i ++) {
                    llvmArguments.push_back(toLLVMValue(arguments[i]));
                }

                auto result = builder->CreateCall(fn, llvmArguments, ins->getName().c_str());

                bind(ins, result);
                break;
            }
            default:
                break;
        }
        if (instResult)
            bind(ins, instResult);
        return instResult;
    }
}