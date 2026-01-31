#include "LLVMCodegenerator.hpp"

llvm::Value* sakuraE::Codegen::LLVMCodeGenerator::codegen(IR::Instruction* ins) {
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
            break;
        }
        default:
            break;
    }
    if (instResult)
        store(ins, instResult);
    return instResult;
}