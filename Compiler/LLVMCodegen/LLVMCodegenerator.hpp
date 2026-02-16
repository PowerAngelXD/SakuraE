#ifndef SAKURAE_LLVMCODEGENERATOR_HPP
#define SAKURAE_LLVMCODEGENERATOR_HPP

#include <cstddef>
#include <cstdint>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Use.h>
#include <llvm/Support/Casting.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/PromoteMemToReg.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>


#include "Compiler/Error/error.hpp"
#include "Compiler/IR/generator.hpp"
#include "Compiler/IR/struct/function.hpp"
#include "Compiler/IR/struct/instruction.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/value/value.hpp"
#include "includes/String.hpp"
#include "Runtime/gc.h"

namespace sakuraE::Codegen {
    class LLVMCodeGenerator {
    public:
        IR::Program* program;
        llvm::LLVMContext* context;
        llvm::IRBuilder<>* builder;
    private:
        // Struct Definition ==================================================
        enum class FunctionType {
            Definition,
            ExternalLinkage
        };

        struct LLVMModule;
        // Represent LLVM Function Instantce
        struct LLVMFunction {
            // Function Type
            FunctionType type;
            // Function linkage name
            fzlib::String linkageName;
            // Function Name
            fzlib::String name;
            // LLVM IR Function represent
            llvm::Function* content = nullptr;
            // Function Return Type
            llvm::Type* returnType = nullptr;
            // Function Formal Params
            std::vector<std::pair<fzlib::String, llvm::Type*>> formalParams;
            // Scope for current Function
            IR::Scope<llvm::Value*> scope;
            // Parent Module
            LLVMModule* parent = nullptr;
            // Parent LLVMCodeGenerator
            LLVMCodeGenerator& codegenContext;
            // Params Alloca Map
            std::map<fzlib::String, llvm::AllocaInst*> paramAllocaMap;
            // Count gc root
            std::stack<uint32_t> gcRootCountStack;
            // SAK IR Function
            IR::Function* sourceFn;
            
            LLVMFunction(FunctionType ty,
                        fzlib::String n, 
                        llvm::Type* retT, 
                        std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, 
                        LLVMModule* p, 
                        LLVMCodeGenerator& codegen,
                        PositionInfo info):
                type(ty), linkageName(n), name(n), content(nullptr), returnType(retT), formalParams(formalP), scope(IR::Scope<llvm::Value*>(info)), parent(p), codegenContext(codegen) {}
            
            LLVMFunction(FunctionType ty,
                        fzlib::String n, 
                        fzlib::String lkn,
                        llvm::Type* retT, 
                        std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, 
                        LLVMModule* p, 
                        LLVMCodeGenerator& codegen,
                        PositionInfo info):
                type(ty), linkageName(lkn), name(n), content(nullptr), returnType(retT), formalParams(formalP), scope(IR::Scope<llvm::Value*>(info)), parent(p), codegenContext(codegen) {}

            void gcCreateThread() {
                auto fn = parent->lookup("__gc_create_thread");
                codegenContext.builder->CreateCall(fn->content, {});
            }

            void gcInsertSafepoint() {
                auto fn = parent->lookup("__gc_safe_point");
                codegenContext.builder->CreateCall(fn->content, {});
            }

            llvm::Value* gcAlloc(llvm::Value* size, runtime::ObjectType ty) {
                auto fn = parent->lookup("__gc_alloc");
                return codegenContext.builder->CreateCall(fn->content,  {
                    size, codegenContext.builder->getInt32(ty)
                });
            }

            llvm::Value* gcAlloc(int size, runtime::ObjectType ty) {
                auto fn = parent->lookup("__gc_alloc");
                auto sTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
                return codegenContext.builder->CreateCall(fn->content,  {
                    llvm::ConstantInt::get(sTy, size), codegenContext.builder->getInt32(ty)
                });
            }

            void gcRegisterRoot(llvm::Value* addr) {
                auto fn = parent->lookup("__gc_register");
                auto ptr = codegenContext.builder->CreateBitCast(addr, llvm::PointerType::getUnqual(*codegenContext.context));

                codegenContext.builder->CreateCall(fn->content, {ptr});

                if (!gcRootCountStack.empty()) gcRootCountStack.top() ++;
            }

            void gcPop(size_t times) {
                if (times == 0) return;
                auto fn = parent->lookup("__gc_pop");
                codegenContext.builder->CreateCall(fn->content, {codegenContext.builder->getInt32(times)});
            }

            void gcCollect() {
                auto fn = parent->lookup("__gc_collect");
                codegenContext.builder->CreateCall(fn->content, {});
            }

            void enterNewHeapScope() {
                gcRootCountStack.push(0);
            }

            void leaveHeapScope() {
                if (gcRootCountStack.empty()) return;
                uint32_t count = gcRootCountStack.top();
                gcPop(count);
                gcRootCountStack.pop();
            }

            llvm::AllocaInst* createAlloca(llvm::Type *ty, llvm::Value *arraySize = nullptr, fzlib::String n = "") {
                llvm::BasicBlock* currentBlock = codegenContext.builder->GetInsertBlock();
                llvm::BasicBlock::iterator currentPoint = codegenContext.builder->GetInsertPoint();

                codegenContext.builder->SetInsertPoint(entryBlock, entryBlock->getFirstInsertionPt());
                llvm::AllocaInst* alloca = codegenContext.builder->CreateAlloca(ty, arraySize, n.c_str());

                if (ty->isPointerTy()) gcRegisterRoot(alloca);
                codegenContext.builder->SetInsertPoint(currentBlock, currentPoint);

                return alloca;
            }

            llvm::Value* createHeapAlloc(llvm::Type* t, runtime::ObjectType objTy, fzlib::String n) {
                size_t size = parent->content->getDataLayout().getTypeAllocSize(t);
                llvm::Type* sizeTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);

                llvm::Value* sizeVal = llvm::ConstantInt::get(sizeTy, size);

                return gcAlloc(sizeVal, objTy);
            }

            llvm::Value* getParamAddress(fzlib::String n) {
                if (paramAllocaMap.find(n) != paramAllocaMap.end()) {
                    return paramAllocaMap[n];
                }
                return nullptr;
            }

            llvm::BasicBlock* entryBlock = nullptr;
            // Instantiates an LLVM Function, performing the transformation from IR Function to LLVM Function.
            // Note: This call resets the insertion point to the entry block of the current function.
            void impl(IR::Function* source);
            // Start LLVM IR Code generation
            void codegen();
        };
        // Represent LLVM Module Instantce
        struct LLVMModule {
            fzlib::String ID;
            llvm::Module* content = nullptr;
            std::map<fzlib::String, LLVMFunction*> fnMap;
            std::map<fzlib::String, fzlib::String> fnNameMapping;
            LLVMCodeGenerator& codegenContext;

            std::vector<LLVMModule*> useList;
            IR::Module* sourceModule;
            

            LLVMModule(fzlib::String id, llvm::LLVMContext& ctx, LLVMCodeGenerator& codegen):
                ID(id), content(nullptr), codegenContext(codegen) {}

            ~LLVMModule() {
                for (auto& pair : fnMap) {
                    if (pair.second) delete pair.second;
                }
            }

            void declareFunction(FunctionType ty, fzlib::String n, llvm::Type* retT, std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, PositionInfo info) {
                if (fnMap.find(n) != fnMap.end()) return;
                else {
                    LLVMFunction* fn = new LLVMFunction(ty, n, retT, formalP, this, codegenContext, info);
                    fnMap[n] = fn;
                }
            }

            void declareFunction(FunctionType ty, fzlib::String n, fzlib::String lkn, llvm::Type* retT, std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, PositionInfo info) {
                if (fnMap.find(n) != fnMap.end()) return;
                else {
                    LLVMFunction* fn = new LLVMFunction(ty, n, lkn, retT, formalP, this, codegenContext, info);
                    fnNameMapping[lkn] = n;
                    fnMap[n] = fn;
                }
            }

            LLVMFunction* lookup(fzlib::String n) {
                if (fnNameMapping.contains(n)) n = fnNameMapping[n];

                if (fnMap.find(n) != fnMap.end()) {
                    return fnMap[n];
                }

                throw std::runtime_error(fzlib::String("Try to call a unknown function: \"" + n + "\"").c_str());
            }

            // Instantiates an LLVM Module, performing the transformation from IR Module to LLVM Module.
            void impl(IR::Module* source);

            // Start LLVM IR Code generation
            void codegen();
        };

        // ====================================================================

        // Instruction Referring ==============================================
        std::map<IR::IRValue*, llvm::Value*> instructionMap;
        // Get IRValue to llvm Value referrence
        inline llvm::Value* getRef(IR::IRValue* sakIRVal) {
            return instructionMap[sakIRVal];
        }

        // Create a new IRValue to llvm Value referrence
        inline void bind(IR::IRValue* sakIRVal, llvm::Value* llvmIRVal) {
            instructionMap[sakIRVal] = llvmIRVal;
        }
        // =====================================================================

        // Module ==============================================================
        std::vector<LLVMModule*> modules;
        // =====================================================================

        // State Tools =========================================================
        IR::Module* curIRModule() {
            return program->curMod();
        }

        IR::Function* curIRFunc() {
            return curIRModule()->curFunc();
        }

        // Look up an identifier matching the target name in the current active IR-function's scope.
        template<typename T>
        IR::Symbol<T>* IRScopeLookup(fzlib::String n) {
            return curIRFunc()->fnScope().lookup(n);
        }
        // =====================================================================

        // Resources ===========================================================
        std::map<fzlib::String, llvm::Value*> stringPool;
        // =====================================================================
    public:
        LLVMCodeGenerator()=default;
        LLVMCodeGenerator(IR::Program* p) {
            program = p;
            context = new llvm::LLVMContext();
            builder = new llvm::IRBuilder<>(*context);

            // Reset, for the state managing
            program->reset();
        }
        ~LLVMCodeGenerator() {
            if (context) delete context;
            if (builder) delete builder;

            for (auto mod: modules) delete mod;
        }   

        void start();
        std::vector<LLVMModule*> getModules() {
            return modules;
        }
        void print();

        std::unique_ptr<llvm::LLVMContext> releaseContext() {
            if (!context) return nullptr;

            auto ptr = std::unique_ptr<llvm::LLVMContext>(context);
            context = nullptr;
            return ptr;
        }
    private:
        llvm::Value* compare(IR::Instruction* ins, LLVMFunction* curFn);
        llvm::Value* instgen(IR::Instruction* ins, LLVMFunction* curFn);

        // Tool Methods =========================================================
        llvm::Value* toLLVMConstant(IR::Constant* constant, LLVMFunction* curFn) {
            switch (constant->getType()->getIRTypeID()){
                case IR::IRTypeID::Integer32TyID: 
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<int>());
                case IR::IRTypeID::Integer64TyID: 
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<long long>());
                case IR::IRTypeID::UInteger32TyID: 
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<unsigned int>());
                case IR::IRTypeID::UInteger64TyID: 
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<unsigned long long>());
                case IR::IRTypeID::Float32TyID:
                    return llvm::ConstantFP::get(constant->getType()->toLLVMType(*context), constant->getContentValue<float>());
                case IR::IRTypeID::Float64TyID:
                    return llvm::ConstantFP::get(constant->getType()->toLLVMType(*context), constant->getContentValue<double>());
                case IR::IRTypeID::PointerTyID: {
                    auto ptrType = dynamic_cast<IR::IRPointerType*>(constant->getType());
                    if (ptrType->getElementType() == IR::IRType::getCharTy()) {
                        // Is String
                        fzlib::String strVal = constant->getContentValue<fzlib::String>();

                        if (stringPool.contains(strVal)) return stringPool[strVal];

                        auto strVar = builder->CreateGlobalString(strVal.c_str(), "tmpstr");

                        auto string_creater = curFn->parent->lookup("create_string");

                        llvm::Value* heapStr = builder->CreateCall(string_creater->content, {strVar}, "heap_str");
                        stringPool[strVal] = heapStr;

                        return heapStr;
                    }
                    break;
                }
                case IR::IRTypeID::CharTyID: {
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<char>());
                }
                case IR::IRTypeID::BoolTyID: {
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<bool>());
                }
                default:
                    return nullptr;
            }

            return nullptr;
        }

        llvm::Value* toLLVMValue(IR::IRValue* value, LLVMFunction* curFn) {
            if (instructionMap.find(value) != instructionMap.end()) {
                return getRef(value);
            }
            else if (auto constant = dynamic_cast<IR::Constant*>(value)) {
                return toLLVMConstant(constant, curFn);
            }
            else if (auto inst = dynamic_cast<IR::Instruction*>(value)) {
                return instgen(inst, curFn);
            }
            else if (auto fn = dynamic_cast<IR::Function*>(value)) {
                return curFn->parent->fnMap[fn->getName()]->content;
            }
            throw std::runtime_error(fzlib::String("Unknown mapping for: " + value->getName()).c_str());
        }

        bool hasLLVMValue(IR::IRValue* value) {
            return instructionMap.contains(value);
        }
        // =====================================================================

        // Calculation =========================================================

    private:
        llvm::Type* promote(llvm::Value*& lhs, llvm::Value*& rhs) {
            auto lTy = lhs->getType();
            auto rTy = rhs->getType();
            if (lTy == rTy) return lTy;

            if (lTy->isFloatingPointTy() || rTy->isFloatingPointTy()) {
                llvm::Type* targetTy = (lTy->isDoubleTy() || rTy->isDoubleTy()) ? 
                                        builder->getDoubleTy() : builder->getFloatTy();
                
                if (lTy->isIntegerTy()) lhs = builder->CreateSIToFP(lhs, targetTy, "lhs.fpromoted");
                else if (lTy != targetTy) lhs = builder->CreateFPExt(lhs, targetTy, "lhs.fpromoted");

                if (rTy->isIntegerTy()) rhs = builder->CreateSIToFP(rhs, targetTy, "rhs.fpromoted");
                else if (rTy != targetTy) rhs = builder->CreateFPExt(rhs, targetTy, "rhs.fpromoted");

                return targetTy;
            }

            if (lTy->isIntegerTy() && rTy->isIntegerTy()) {
                unsigned lWidth = lTy->getIntegerBitWidth();
                unsigned rWidth = rTy->getIntegerBitWidth();
                unsigned maxWidth = std::max(lWidth, rWidth);
                llvm::Type* targetTy = llvm::Type::getIntNTy(*context, maxWidth);

                if (lWidth < maxWidth) lhs = builder->CreateSExt(lhs, targetTy, "lhs.iext");
                if (rWidth < maxWidth) rhs = builder->CreateSExt(rhs, targetTy, "rhs.iext");

                return targetTy;
            }
            return nullptr;
        }

    public:
        llvm::Value* add(llvm::Value* lhs, llvm::Value* rhs) {
            auto targetTy = promote(lhs, rhs);
            if (!targetTy) return nullptr;
            return targetTy->isFloatingPointTy() ? 
                builder->CreateFAdd(lhs, rhs, "addftmp") : builder->CreateAdd(lhs, rhs, "addtmp");
        }

        llvm::Value* sub(llvm::Value* lhs, llvm::Value* rhs) {
            auto targetTy = promote(lhs, rhs);
            if (!targetTy) return nullptr;
            return targetTy->isFloatingPointTy() ? 
                builder->CreateFSub(lhs, rhs, "subftmp") : builder->CreateSub(lhs, rhs, "subtmp");
        }

        llvm::Value* mul(llvm::Value* lhs, llvm::Value* rhs) {
            auto targetTy = promote(lhs, rhs);
            if (!targetTy) return nullptr;
            return targetTy->isFloatingPointTy() ? 
                builder->CreateFMul(lhs, rhs, "mulftmp") : builder->CreateMul(lhs, rhs, "multmp");
        }

        llvm::Value* div(llvm::Value* lhs, llvm::Value* rhs) {
            auto targetTy = promote(lhs, rhs);
            if (!targetTy) return nullptr;
            return targetTy->isFloatingPointTy() ? 
                builder->CreateFDiv(lhs, rhs, "divftmp") : builder->CreateSDiv(lhs, rhs, "divtmp");
        }

        llvm::Value* mod(llvm::Value* lhs, llvm::Value* rhs) {
            auto targetTy = promote(lhs, rhs);
            if (!targetTy) return nullptr;
            return targetTy->isFloatingPointTy() ? 
                builder->CreateFRem(lhs, rhs, "remftmp") : builder->CreateSRem(lhs, rhs, "remtmp");
        }

        llvm::Value* compare(llvm::Value* lhs, llvm::Value* rhs, IR::OpKind kind, LLVMFunction* curFn) {
            auto targetTy = promote(lhs, rhs);
            
            if (targetTy && targetTy->isFloatingPointTy()) {
                llvm::FCmpInst::Predicate pred;
                switch (kind) {
                    case IR::OpKind::lgc_equal:       pred = llvm::FCmpInst::FCMP_OEQ; break;
                    case IR::OpKind::lgc_not_equal:   pred = llvm::FCmpInst::FCMP_ONE; break;
                    case IR::OpKind::lgc_mr_than:      pred = llvm::FCmpInst::FCMP_OGT; break;
                    case IR::OpKind::lgc_ls_than:      pred = llvm::FCmpInst::FCMP_OLT; break;
                    case IR::OpKind::lgc_eq_mr_than:   pred = llvm::FCmpInst::FCMP_OGE; break;
                    case IR::OpKind::lgc_eq_ls_than:   pred = llvm::FCmpInst::FCMP_OLE; break;
                    default: return nullptr;
                }
                // Correcting the typo FCInst to FCmpInst
                return builder->CreateFCmp(pred, lhs, rhs, "fcmp.tmp");
            }

            if (targetTy && targetTy->isIntegerTy()) {
                llvm::ICmpInst::Predicate pred;
                switch (kind) {
                    case IR::OpKind::lgc_equal:       pred = llvm::ICmpInst::ICMP_EQ;  break;
                    case IR::OpKind::lgc_not_equal:   pred = llvm::ICmpInst::ICMP_NE;  break;
                    case IR::OpKind::lgc_mr_than:      pred = llvm::ICmpInst::ICMP_SGT; break;
                    case IR::OpKind::lgc_ls_than:      pred = llvm::ICmpInst::ICMP_SLT; break;
                    case IR::OpKind::lgc_eq_mr_than:   pred = llvm::ICmpInst::ICMP_SGE; break;
                    case IR::OpKind::lgc_eq_ls_than:   pred = llvm::ICmpInst::ICMP_SLE; break;
                    default: return nullptr;
                }
                return builder->CreateICmp(pred, lhs, rhs, "icmp.tmp");
            }

            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                if (kind == IR::OpKind::lgc_equal || kind == IR::OpKind::lgc_not_equal) {
                    llvm::FunctionCallee strcmpFunc = curFn->parent->content->getOrInsertFunction(
                        "strcmp", builder->getInt32Ty(), builder->getPtrTy(), builder->getPtrTy()
                    );
                    llvm::Value* res = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp.tmp");

                    if (kind == IR::OpKind::lgc_equal)
                        return builder->CreateICmpEQ(res, builder->getInt32(0), "str.eq");
                    else
                        return builder->CreateICmpNE(res, builder->getInt32(0), "str.ne");
                }
            }

            return nullptr;
        }

        // =====================================================================

        // Optimizer ===========================================================
        void moduleOptimize(llvm::Module* mod) {
            llvm::LoopAnalysisManager LAM;
            llvm::FunctionAnalysisManager FAM;
            llvm::CGSCCAnalysisManager CGAM;
            llvm::ModuleAnalysisManager MAM;

            llvm::PassBuilder PB;
            PB.registerModuleAnalyses(MAM);
            PB.registerCGSCCAnalyses(CGAM);
            PB.registerFunctionAnalyses(FAM);
            PB.registerLoopAnalyses(LAM);
            PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

            llvm::FunctionPassManager FPM;
            FPM.addPass(llvm::PromotePass()); 

            for (auto &F : *mod) {
                if (!F.isDeclaration()) {
                    FPM.run(F, FAM);
                }
            }
        }
    public:
        void optimize() {
            for (auto mod: modules) {
                moduleOptimize(mod->content);
            }
        }
        // =====================================================================

    };
}

#endif // !SAKURAE_LLVMCODEGENERATOR_HPP
