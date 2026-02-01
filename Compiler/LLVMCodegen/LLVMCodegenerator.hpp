#ifndef SAKURAE_LLVMCODEGENERATOR_HPP
#define SAKURAE_LLVMCODEGENERATOR_HPP

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

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/generator.hpp"
#include "Compiler/IR/struct/function.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"

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

            llvm::BasicBlock* entryBlock = nullptr;
            // Instantiates an LLVM Function, performing the transformation from IR Function to LLVM Function.
            // Note: This call resets the insertion point to the entry block of the current function.
            void impl();
        };

        // Represent LLVM Module Instantce
        struct LLVMModule {
            fzlib::String ID;
            llvm::Module* content = nullptr;
            std::map<fzlib::String, LLVMFunction*> funcs;
            // State Manager
            fzlib::String activeFunctionName;

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

                    activeFunctionName = n;
                }
            }

            void declareFunction(fzlib::String n, IR::IRType* retT, IR::FormalParamsDefine formalP, PositionInfo info) {
                if (funcs.find(n) != funcs.end()) return;
                else {
                    llvm::Type* llvmReturnType = retT->toLLVMType(*context);

                    std::vector<std::pair<fzlib::String, llvm::Type*>> llvmFormalP;
                    for (auto param: formalP) {
                        llvmFormalP.emplace_back(param.first, param.second->toLLVMType(*context));
                    }

                    LLVMFunction* fn = new LLVMFunction(n, llvmReturnType, llvmFormalP, this, info);
                    funcs[n] = fn;

                    activeFunctionName = n;
                }
            }

            void implFunction(fzlib::String n) {
                // Undeclare
                if (funcs.find(n) == funcs.end()) declareFunction(n);
                // Just declare but not complete the function type
                else if (funcs[n] == nullptr) return ;
                // Fit
                else {
                    funcs[n]->impl();
                }
            }

            void implActive() {
                // Just declare but not complete the function type
                if (funcs[activeFunctionName] == nullptr) return ;
                // Fit
                else {
                    funcs[activeFunctionName]->impl();
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

            LLVMFunction* getActive() {
                if (!funcs[activeFunctionName]) {
                    throw std::runtime_error("activeFunction is null!");
                }
                return funcs[activeFunctionName];
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

        LLVMModule* getModule(long index) {
            return moduleList[index].second;
        }
        // =====================================================================

        // State Tools =========================================================
        IR::Module* curIRModule() {
            return program->curMod();
        }

        IR::Function* curIRFunc() {
            return curIRModule()->curFunc();
        }

        // Look up an identifier matching the conditions in the current active function's scope.
        template<typename T>
        IR::Symbol<T>* IRScopeLookup(fzlib::String n) {
            return curIRFunc()->fnScope().lookup(n);
        }

        template<typename T>
        IR::Symbol<T>* lookup(fzlib::String n) {
            return getCurrentUsingModule()->getActive()->scope.lookup(n);
        }
        // =====================================================================
    public:
        LLVMCodeGenerator(IR::Program* p) {
            program = p;
            context = new llvm::LLVMContext();
            builder = new llvm::IRBuilder<>(*context);

            createModule("MainModule");

            // Reset, for the state managing
            program->reset();
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