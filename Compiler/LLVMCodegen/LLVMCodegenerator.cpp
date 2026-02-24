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

        if (name == "main") gcCreateThread();
        gcRootCountStack.push(0);
        gcInsertSafepoint();

        std::size_t i = 0;
        for (auto& arg: content->args()) {
            arg.setName(irParams[i].first.c_str());

            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            paramAllocaMap[irParams[i].first.c_str()] = argAlloca;

            codegenContext.builder->CreateStore(&arg, argAlloca);

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

                instResult = compare(lhs, rhs, ins->getKind(), curFn);
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
                for (auto element: irArray->getArray()) {
                    arrayContent.push_back(toLLVMValue(element, curFn));
                }
                auto arrayType = ins->getType()->toLLVMType(*context);
                auto elementType = arrayType->getArrayElementType();

                llvm::Value* arrayPtr = curFn->createHeapAlloc(arrayType, runtime::ObjectType::Array, "tmparr");

                for (std::size_t i = 0; i < arrayContent.size(); i ++) {
                    auto ptr = builder->CreateGEP(elementType,
                                                            arrayPtr,
                                                            {builder->getInt32(i)});
                    builder->CreateStore(arrayContent[i], ptr);
                }

                bind(ins, arrayPtr);
                break;
            }
            case IR::OpKind::indexing: {
                llvm::Value* addr = toLLVMValue(ins->arg(0), curFn);
                llvm::Value* indexVal = toLLVMValue(ins->arg(1), curFn);

                auto addrIRType = ins->arg(0)->getType();
                llvm::Type* elementType = nullptr;

                if (addrIRType->isArray()) {
                    addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr);
                    elementType = static_cast<IR::IRArrayType*>(addrIRType)->getElementType()->toLLVMType(*context);
                }
                else if (addrIRType->isPointer()) {
                    addr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), addr);
                    elementType = static_cast<IR::IRArrayType*>(addrIRType)->getElementType()->toLLVMType(*context);
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
                auto tempStack = curFn->gcRootCountStack;
                while (!tempStack.empty()) {
                    uint32_t count = tempStack.top();
                    if (count > 0) {
                        curFn->gcPop(count);
                    }
                    tempStack.pop();
                }

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
                for (std::size_t i = 0; i < arguments.size(); i ++) {
                    auto argVal = toLLVMValue(arguments[i], curFn);
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(argVal)) {
                        llvm::Type* allocatedType = allocaInst->getAllocatedType();
                        argVal = builder->CreateLoad(allocatedType, allocaInst, "call.arg.load");
                    }
                    llvmArguments.push_back(argVal);
                }

                instResult = builder->CreateCall(fn, llvmArguments, ins->getName().c_str());

                bind(ins, instResult);
                break;
            }
            case IR::OpKind::enter_scope: {
                curFn->enterNewHeapScope();
                break;
            }
            case IR::OpKind::leave_scope: {
                curFn->leaveHeapScope();
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
            modules.push_back(new LLVMModule(mod->id(), *context, *this));
        }

        for (std::size_t i = 0; i < modules.size(); i ++) {
            modules[i]->impl(irModList[i]);
        }

        for (auto mod: modules) {
            mod->codegen();
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
