#include "LLVMCodegenerator.hpp"

namespace sakuraE {
    void LLVMCodeGenerator::codegen(IR::Module* mod) {

    }

    llvm::Value* LLVMCodeGenerator::codegen(IR::Block* block) {

    }

    llvm::Value* LLVMCodeGenerator::codegen(IR::Function* fn) {

    }

    llvm::Value* LLVMCodeGenerator::codegen(IR::Instruction* ins) {
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
            }
            default:
                break;
        }
        if (instResult)
            mapStore(ins, instResult);
        return instResult;
    }

}