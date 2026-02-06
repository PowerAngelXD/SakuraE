#include "LLVMCodegenerator.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/module.hpp"
#include <cstddef>
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

            declareFunction(func->getName(), retTy, params, func->getInfo());
        }

        for (auto irFn: funcs) {
            fnMap[irFn->getName()]->impl(irFn);
        }

        sourceModule = source;
    }

    void LLVMCodeGenerator::LLVMModule::codegen() {
        auto funcList = sourceModule->getFunctions();
        for (auto fn: funcList) {
            auto curFn = fnMap[fn->getName()];
            curFn->codegen();
        }
    }

    // LLVM Function
    void LLVMCodeGenerator::LLVMFunction::impl(IR::Function* source) {
        std::vector<llvm::Type*> params;
        for (auto param: formalParams) {
            params.push_back(param.second);
        }

        llvm::FunctionType* fnType = llvm::FunctionType::get(returnType, params, false);
        content = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, name.c_str(), parent->content);

        auto irParams = source->getFormalParams();

        for (auto block: source->getBlocks()) {
            llvm::BasicBlock* llvmBlock = llvm::BasicBlock::Create(*codegenContext.context, block->getName().c_str(), content);
            codegenContext.bind(block, llvmBlock);

            if (block->getName() == "entry") {
                entryBlock = llvmBlock;
            }
        }

        codegenContext.builder->SetInsertPoint(entryBlock);

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            codegenContext.builder->CreateStore(&arg, argAlloca);

            scope.declare(irParams[i].first, argAlloca, nullptr);
            i ++;
        }

        sourceFn = source;
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
                auto llvmConst = toLLVMConstant(constant);
                bind(ins, llvmConst);
                return toLLVMConstant(constant);
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
            case IR::OpKind::lgc_equal:
            case IR::OpKind::lgc_not_equal:
            case IR::OpKind::lgc_mr_than:
            case IR::OpKind::lgc_ls_than:
            case IR::OpKind::lgc_eq_mr_than:
            case IR::OpKind::lgc_eq_ls_than: {
                llvm::Value* lhs = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* rhs = toLLVMValue(ins->arg(1), curFn);
                
                instResult = compare(lhs, rhs, ins->getKind(), curFn);
                break;
            }
            case IR::OpKind::declare: {
                auto insName = ins->getName();
                auto identifierName = insName.split('.')[1];

                auto identifierType = ins->getType()->toLLVMType(*context);

                llvm::AllocaInst* alloca = curFn->createAlloca(identifierType, nullptr, identifierName);

                auto initVal = ins->arg(0);
                if (initVal) {
                    if (identifierType->isArrayTy()) {
                        auto arrSize = curFn->parent->content->getDataLayout().getTypeAllocSize(identifierType);
                        builder->CreateMemCpy(alloca, llvm::MaybeAlign(8), toLLVMValue(initVal, curFn), llvm::MaybeAlign(8), arrSize);
                    }
                    else {
                        builder->CreateStore(toLLVMValue(initVal, curFn), alloca);
                    }
                }
                else {
                    builder->CreateStore(llvm::Constant::getNullValue(identifierType), alloca);
                }

                bind(ins, alloca);
                curFn->scope.declare(identifierName, alloca, nullptr);

                break;
            }
            case IR::OpKind::assign: {
                auto insName = ins->arg(0)->getName();
                auto identifierName = insName.split('.')[1];

                // TODO: Type is not only AllocaInst
                auto alloca = llvm::dyn_cast<llvm::AllocaInst>(curFn->scope.lookup(identifierName)->address);
                auto val = toLLVMValue(ins->arg(1), curFn);

                if (alloca && val) {
                    // TODO: Ignore the different type (Assume they are the same)
                    if (val->getType()->isArrayTy()) {
                        auto arrSize = curFn->parent->content->getDataLayout().getTypeAllocSize(val->getType());
                        builder->CreateMemCpy(alloca, llvm::MaybeAlign(8), val, llvm::MaybeAlign(8), arrSize);
                    }
                    else {
                        builder->CreateStore(val, alloca);
                    }
                }

                bind(ins, alloca);
                break;
            }
            case IR::OpKind::create_array: {
                auto irArray = ins->getOperands();
                std::vector<llvm::Value*> arrayContent;
                for (auto element: irArray) {
                    arrayContent.push_back(toLLVMValue(element, curFn));
                }
                auto arrayType = ins->getType()->toLLVMType(*context);

                llvm::AllocaInst* alloca = curFn->createAlloca(arrayType, nullptr, ins->getName());

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
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = toLLVMValue(ins->arg(1), curFn);

                llvm::Type* type = ins->arg(0)->getType()->toLLVMType(*context);
                auto ptr = builder->CreateGEP(type, addr, {builder->getInt32(0), indexVal}, "element_ptr");
                
                instResult = builder->CreateLoad(type->getArrayElementType(), ptr, "element_val");

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

                auto fn = curFn->content;

                auto arguments = ins->getOperands();
                std::vector<llvm::Value*> llvmArguments;
                for (std::size_t i = 0; i < arguments.size(); i ++) {
                    llvmArguments.push_back(toLLVMValue(arguments[i], curFn));
                }

                instResult = builder->CreateCall(fn, llvmArguments, ins->getName().c_str());

                bind(ins, instResult);
                break;
            }
            default:
                break;
        }
        if (instResult)
            bind(ins, instResult);
        return instResult;
    }

    // LLVMCodegeneration start
    void LLVMCodeGenerator::start() {
        auto irModList = program->getMods();
        for (auto mod: irModList) {
            modules.push_back(LLVMModule(mod->id(), *context, *this));
        }

        for (std::size_t i = 0; i < modules.size(); i ++) {
            modules[i].impl(irModList[i]);
        }

        for (auto mod: modules) {
            mod.codegen();
        }
    }

    // Debug print
    void LLVMCodeGenerator::print() {
        for (auto mod: modules) {
            mod.content->print(llvm::outs(), nullptr);
        }
    }
}