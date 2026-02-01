#ifndef SAKURAE_LLVMCODEGENERATOR_HPP
#define SAKURAE_LLVMCODEGENERATOR_HPP

#include <map>
#include <memory>
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

#include "Compiler/IR/generator.hpp"

namespace sakuraE::Codegen {
    class LLVMCodeGenerator {
        IR::Program* program;
        static llvm::LLVMContext* context;
        static llvm::IRBuilder<>* builder;
        
        // Struct Definition ==================================================
        struct LLVMModule;
        // Represent LLVM Function Instantce
        struct LLVMFunction {
            fzlib::String name;
            llvm::Function* content = nullptr;
            llvm::Type* returnType = nullptr;
            std::vector<std::pair<fzlib::String, llvm::Type*>> formalParams;
            IR::Scope<llvm::Value*> scope;
            LLVMModule* parent = nullptr;
            
            LLVMFunction(fzlib::String n, llvm::Type* retT, std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, LLVMModule* p, PositionInfo info):
                name(n), content(nullptr), returnType(retT), formalParams(formalP), scope(IR::Scope<llvm::Value*>(info)), parent(p) {}

            ~LLVMFunction() {
                name.free();
            }

            // Instantiates an LLVM Function, performing the transformation from IR Function to LLVM Function.
            // Note: This call resets the insertion point to the entry block of the current function.
            llvm::BasicBlock* impl();
        };

        // Represent LLVM Module Instantce
        struct LLVMModule {
            fzlib::String ID;
            llvm::Module* content = nullptr;
            std::map<fzlib::String, LLVMFunction*> funcs;

            LLVMModule(fzlib::String id, llvm::LLVMContext& ctx):
                ID(id), content(nullptr) {}
            
            ~LLVMModule() {
                ID.free();
            }

            void declareFunction(fzlib::String n) {
                if (funcs.find(n) == funcs.end())
                    funcs[n] = nullptr;
                else return;
            }

            void declareFunction(fzlib::String n, llvm::Type* retT, std::vector<std::pair<fzlib::String, llvm::Type*>> formalP, PositionInfo info) {
                if (funcs.find(n) != funcs.end()) return;
                else {
                    LLVMFunction* fn = new LLVMFunction(n, retT, formalP, this, info);
                    funcs[n] = fn;
                }
            }

            LLVMFunction* get(fzlib::String n) {
                if (funcs.find(n) == funcs.end()) {
                    declareFunction(n);
                    return nullptr;
                }
                else {
                    return funcs[n];
                }
            }
        };

        // ====================================================================

        // Instruction Referring ==============================================
        std::map<IR::IRValue*, llvm::Value*> instructionMap;
        // Get IRValue to llvm Value referrence
        inline llvm::Value* getRef(IR::IRValue* sakIRVal) {
            return instructionMap[sakIRVal];
        }

        // Create a new IRValue to llvm Value referrence
        inline void store(IR::IRValue* sakIRVal, llvm::Value* llvmIRVal) {
            instructionMap[sakIRVal] = llvmIRVal;
        }

        // Create referring
        void buildMapping(IR::Instruction* ins) {
            llvm::Value* result = instgen(ins);
            if (result) {
                store(ins, result);
            }
        }
        // =====================================================================

        // Module Manager ======================================================
        std::vector<std::pair<fzlib::String, LLVMModule*>> moduleList;
        long curModuleIndex = -1;

        void createModule(fzlib::String id) {
            for (auto mod: moduleList) {
                if (mod.first == id) return ;
            }
            moduleList.emplace_back(id, new LLVMModule(id, *context));
            curModuleIndex ++;
        }

        LLVMModule* getCurrentUsingModule() {
            return moduleList[curModuleIndex].second;
        }
        // =====================================================================
    public:
        LLVMCodeGenerator(IR::Program* p) {
            program = p;
            context = new llvm::LLVMContext();
            builder = new llvm::IRBuilder<>(*context);

            createModule("MainModule");
        }

    private:
        llvm::Value* instgen(IR::Instruction* ins);

        // Tool Methods =========================================================
        llvm::Value* toLLVMConstant(IR::Constant* constant) {
            switch (constant->getType()->getIRTypeID()){
                case IR::IRTypeID::IntegerTyID: {
                    return llvm::ConstantInt::get(constant->getType()->toLLVMType(*context), constant->getContentValue<int>());
                }
                case IR::IRTypeID::FloatTyID:
                    return llvm::ConstantFP::get(constant->getType()->toLLVMType(*context), constant->getContentValue<double>());
                case IR::IRTypeID::PointerTyID: {
                    auto ptrType = dynamic_cast<IR::IRPointerType*>(constant->getType());
                    if (ptrType->getElementType() == IR::IRType::getCharTy()) {
                        // Is String
                        fzlib::String strVal = constant->getContentValue<fzlib::String>();
                        return builder->CreateGlobalString(strVal.c_str());
                    }
                    break;
                }
                default:
                    return nullptr;
            }

            return nullptr;
        }

        llvm::Value* toLLVMValue(IR::IRValue* value) {
            if (instructionMap.find(value) != instructionMap.end()) {
                return getRef(value);
            }
            else if (auto* constant = dynamic_cast<IR::Constant*>(value)) {
                return toLLVMConstant(constant);
            }
            return nullptr;
        }
        // =====================================================================
    };
}

#endif // !SAKURAE_LLVMCODEGENERATOR_HPP