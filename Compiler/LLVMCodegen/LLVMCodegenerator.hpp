#ifndef SAKURAE_LLVMCODEGENERATOR_HPP
#define SAKURAE_LLVMCODEGENERATOR_HPP

#include <cstddef>
#include <cstdint>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Use.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <unordered_set>

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
#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Backend/model/module.hpp"
#include "Compiler/IR/Backend/model/function.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include "Compiler/IR/Semantic/model/instruction.hpp"
#include "Compiler/IR/Semantic/model/scope.hpp"
#include "Compiler/IR/Semantic/model/struct.hpp"
#include "Compiler/IR/Backend/type/type.hpp"
#include "Compiler/IR/Semantic/value/value.hpp"
#include "includes/String.hpp"
#include "Runtime/gc.h"
#include "Runtime/rttype.h"

namespace sakuraE::Codegen {
    class LLVMCodeGenerator {
    public:
        const IR::BackendModule& rootModule;
        llvm::LLVMContext* context;
        llvm::IRBuilder<>* builder;
    private:
        // 结构体定义 =========================================================

        struct LLVMModule;
        // LLVM 函数实例
        struct LLVMFunction {
            const IR::BackendFunction& backend;
            bool external;
            llvm::Function* content = nullptr;
            // 当前函数的作用域
            IR::Scope<llvm::Value*> scope;
            // 父模块
            LLVMModule* parent = nullptr;
            // 父级 LLVMCodeGenerator
            LLVMCodeGenerator& codegenContext;
            // 参数 Alloca 映射
            std::map<fzlib::String, llvm::AllocaInst*> paramAllocaMap;
            // 当前函数的活动 GC 作用域深度
            uint32_t gcScopeDepth = 0;

            LLVMFunction(const IR::BackendFunction& source,
                         bool isExternal,
                         LLVMModule* p,
                         LLVMCodeGenerator& codegen):
                backend(source), external(isExternal),
                scope(source.source->getInfo()), parent(p), codegenContext(codegen) {}

            void gcEnterScope() {
                // 进入函数或临时表达式作用域，后续注册的 root 会按栈序撤销
                auto fn = parent->lookup("__gc_enter_scope");
                codegenContext.builder->CreateCall(fn->content, {});
                gcScopeDepth ++;
            }

            void gcLeaveScope() {
                if (gcScopeDepth == 0) return;
                auto fn = parent->lookup("__gc_leave_scope");
                codegenContext.builder->CreateCall(fn->content, {});
                gcScopeDepth --;
            }

            void gcLeaveAllScopes() {
                while (gcScopeDepth > 0) {
                    gcLeaveScope();
                }
            }

            llvm::Value* gcAlloc(llvm::Value* size, llvm::Value* gcTy, llvm::Value* elemCount = nullptr) {
                // 所有托管对象都通过统一入口分配，GC 可据此维护对象链表和类型信息
                auto fn = parent->lookup("__gc_alloc");

                if (!elemCount) {
                    elemCount = codegenContext.builder->getInt64(0);
                }

                return codegenContext.builder->CreateCall(fn->content, {
                    size,
                    gcTy,
                    elemCount
                });
            }

            llvm::Value* gcAlloc(int size, llvm::Value* gcTy, uint64_t elemCount = 0) {
                auto fn = parent->lookup("__gc_alloc");
                auto sTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);

                return codegenContext.builder->CreateCall(fn->content, {
                    llvm::ConstantInt::get(sTy, size),
                    gcTy,
                    codegenContext.builder->getInt64(elemCount)
                });
            }

            void gcRegisterRoot(llvm::Value* addr) {
                // addr 指向 LLVM alloca 槽位，运行时会在收集时读取槽位中的最新对象指针
                auto fn = parent->lookup("__gc_register");
                auto ptr = codegenContext.builder->CreateBitCast(addr, llvm::PointerType::getUnqual(*codegenContext.context));

                codegenContext.builder->CreateCall(fn->content, {ptr});
            }

            void gcRegisterValueRoot(llvm::Value* addr) {
                auto fn = parent->lookup("__gc_register_value_slot");
                auto ptr = codegenContext.builder->CreateBitCast(addr,
                    llvm::PointerType::getUnqual(*codegenContext.context));
                codegenContext.builder->CreateCall(fn->content, {ptr});
            }

            void gcRegisterManagedSlot(llvm::Value* addr, const IR::RuntimeTypeDescription& type) {
                if (type.representation == IR::ValueRepresentation::ManagedArray) {
                    gcRegisterRoot(addr);
                }
                else {
                    gcRegisterValueRoot(addr);
                }
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

            bool shouldTrackAsGCRoot(const IR::IRValue* value) const {
                return backend.tracksRoot(value);
            }

            llvm::AllocaInst* createRootedTemporary(
                llvm::Value* value,
                const fzlib::String& slotName,
                const IR::RuntimeTypeDescription& type) {
                // 将 SSA 值落到 entry block 的槽位，避免后续分配发生时值无法作为 root 被扫描
                auto* slot = createAlloca(value->getType(), nullptr, slotName);
                codegenContext.builder->CreateStore(value, slot);
                gcRegisterManagedSlot(slot, type);
                return slot;
            }

            llvm::AllocaInst* createAlloca(llvm::Type *ty, llvm::Value *arraySize = nullptr, fzlib::String n = "") {
                llvm::BasicBlock* currentBlock = codegenContext.builder->GetInsertBlock();
                llvm::BasicBlock::iterator currentPoint = codegenContext.builder->GetInsertPoint();

                codegenContext.builder->SetInsertPoint(entryBlock, ++ entryBlock->getFirstInsertionPt());
                llvm::AllocaInst* alloca = codegenContext.builder->CreateAlloca(ty, arraySize, n.c_str());

                codegenContext.builder->SetInsertPoint(currentBlock, currentPoint);

                return alloca;
            }

            llvm::Value* createHeapAlloc(llvm::Type* t, llvm::Value* gcTy, llvm::Value* elemCount) {
                // 数组 payload 不包含 header；header 由 Runtime::__gc_alloc 在 payload 前方创建
                size_t size = parent->content->getDataLayout().getTypeAllocSize(t);
                llvm::Type* sizeTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
                llvm::Value* sizeVal = llvm::ConstantInt::get(sizeTy, size);

                return gcAlloc(sizeVal, gcTy, elemCount);
            }

            llvm::Value* getParamAddress(fzlib::String n) {
                if (paramAllocaMap.find(n) != paramAllocaMap.end()) {
                    return paramAllocaMap[n];
                }
                return nullptr;
            }

            llvm::BasicBlock* entryBlock = nullptr;
            /* 创建 LLVM 函数，并将 IR 函数转换为 LLVM 函数
             * 注意：此调用会将当前插入点重置到当前函数的 entry 基本块
             */
            void impl();
            // 开始生成 LLVM IR
            void codegen();
        };
        // LLVM 模块实例
        struct LLVMModule {
            const IR::BackendModule& backend;
            const fzlib::String& ID;
            llvm::Module* content = nullptr;
            std::map<const IR::BackendFunction*, LLVMFunction*> functions;
            LLVMCodeGenerator& codegenContext;

            LLVMModule(const IR::BackendModule& source, LLVMCodeGenerator& codegen):
                backend(source), ID(source.ID), codegenContext(codegen) {}



            ~LLVMModule() {
                for (auto& pair : functions) {
                    if (pair.second) delete pair.second;
                }
            }

            llvm::Value* getAtomicGCType() {
                auto callee = content->getOrInsertFunction(
                    "__gc_get_atomic_type",
                    llvm::FunctionType::get(codegenContext.builder->getPtrTy(), false)
                );
                return codegenContext.builder->CreateCall(callee, {});
            }

            llvm::Value* getArrayGCType(bool isPtr, uint32_t memberSize, llvm::Value* memTy, uint64_t length = 0) {
                // 将 LLVM 数组布局转换为运行时扫描描述符，length 用于嵌入式数组递归扫描
                auto callee = content->getOrInsertFunction(
                    "__gc_get_array_type_with_length",
                    llvm::FunctionType::get(
                        codegenContext.builder->getPtrTy(),
                        {
                            llvm::Type::getInt1Ty(*codegenContext.context),
                            llvm::Type::getInt32Ty(*codegenContext.context),
                            llvm::Type::getInt64Ty(*codegenContext.context),
                            codegenContext.builder->getPtrTy()
                        },
                        false
                    )
                );
                return codegenContext.builder->CreateCall(
                    callee,
                    {
                        codegenContext.builder->getInt1(isPtr),
                        codegenContext.builder->getInt32(memberSize),
                        codegenContext.builder->getInt64(length),
                        memTy
                    }
                );
            }

            llvm::Value* getRuntimeValueArrayGCType(uint32_t memberSize, uint64_t length) {
                auto callee = content->getOrInsertFunction(
                    "__gc_get_runtime_value_array_type",
                    llvm::FunctionType::get(
                        codegenContext.builder->getPtrTy(),
                        {
                            llvm::Type::getInt32Ty(*codegenContext.context),
                            llvm::Type::getInt64Ty(*codegenContext.context)
                        },
                        false
                    )
                );
                return codegenContext.builder->CreateCall(callee, {
                    codegenContext.builder->getInt32(memberSize),
                    codegenContext.builder->getInt64(length)
                });
            }

            llvm::Value* llvmTy2GCType(llvm::Type* ty) {
                if (!ty) {
                    return getAtomicGCType();
                }

                if (ty->isArrayTy()) {
                    auto* arrTy = llvm::cast<llvm::ArrayType>(ty);
                    llvm::Type* elemTy = arrTy->getElementType();
                    bool elemIsPtr = elemTy->isPointerTy();
                    uint32_t elemSize = static_cast<uint32_t>(
                        content->getDataLayout().getTypeAllocSize(elemTy)
                    );
                    llvm::Value* elemGcTy = elemIsPtr ? getAtomicGCType() : llvmTy2GCType(elemTy);

                    return getArrayGCType(elemIsPtr, elemSize, elemGcTy, arrTy->getNumElements());
                }

                if (ty->isPointerTy()) {
                    uint32_t ptrSize = static_cast<uint32_t>(content->getDataLayout().getPointerSize());
                    return getArrayGCType(true, ptrSize, getAtomicGCType());
                }

                return getAtomicGCType();
            }

            LLVMFunction* lookup(const fzlib::String& name) const {
                const auto* function = backend.lookup(name);
                auto found = functions.find(function);
                if (found != functions.end()) {
                    return found->second;
                }
                throw std::runtime_error(fzlib::String("Try to call an unknown function: \"" + name + "\"").c_str());
            }

            // 创建 LLVM 模块，并将 IR 模块转换为 LLVM 模块
            void impl();

            // 开始生成 LLVM IR
            void codegen();
        };

        // ====================================================================

        llvm::StructType* runtimeValueType() {
            auto* existing = llvm::StructType::getTypeByName(*context, "sakurae.RuntimeValue");
            if (existing) return existing;

            auto* padding = llvm::ArrayType::get(llvm::Type::getInt8Ty(*context), 7);
            // RawValue 在 C++ ABI 下占用 32 字节，并按 8 字节对齐
            auto* data = llvm::ArrayType::get(llvm::Type::getInt64Ty(*context), 4);
            return llvm::StructType::create(*context,
                {llvm::Type::getInt8Ty(*context), padding, data},
                "sakurae.RuntimeValue");
        }

        llvm::Type* abiType(IR::IRType* type, IR::ABIValueKind kind) {
            switch (kind) {
                case IR::ABIValueKind::Void:
                    return llvm::Type::getVoidTy(*context);
                case IR::ABIValueKind::Raw:
                    return type->toLLVMType(*context);
                case IR::ABIValueKind::Boxed:
                    return llvm::PointerType::getUnqual(*context);
            }
            throw std::runtime_error("Unknown backend ABI value kind");
        }

        llvm::Value* boxRaw(llvm::Value* raw, const IR::RuntimeTypeDescription& type, LLVMFunction* curFn) {
            if (type.representation != IR::ValueRepresentation::Boxed || type.tag == IR::RuntimeValueTag::None) {
                throw std::runtime_error("Cannot box a non-boxed backend representation");
            }
            auto* allocFn = curFn->parent->lookup("__runtime_alloc_value");
            auto* resultSlot = builder->CreateCall(allocFn->content, {}, "runtime.value");
            builder->CreateStore(llvm::Constant::getNullValue(runtimeValueType()), resultSlot);
            builder->CreateStore(builder->getInt8(static_cast<std::uint8_t>(type.tag)),
                                builder->CreateStructGEP(runtimeValueType(), resultSlot, 0));

            auto* dataSlot = builder->CreateStructGEP(runtimeValueType(), resultSlot, 2);
            auto* rawSlot = builder->CreateBitCast(dataSlot, llvm::PointerType::getUnqual(*context));
            builder->CreateStore(raw, rawSlot);
            return resultSlot;
        }

        llvm::Value* runtimeTypeInfo(const IR::RuntimeTypeDescription& type, LLVMFunction* curFn) {
            auto callFactory = [&](const fzlib::String& name, std::vector<llvm::Value*> args) {
                auto* fn = curFn->parent->lookup(name);
                return builder->CreateCall(fn->content, args, "runtime.typeinfo");
            };
            using Kind = IR::RuntimeDescriptionKind;
            runtime::RuntimeTypeKind kind;
            switch (type.kind) {
                case Kind::Void: kind = runtime::RuntimeTypeKind::Void; break;
                case Kind::I8: kind = runtime::RuntimeTypeKind::I8; break;
                case Kind::I32: kind = runtime::RuntimeTypeKind::I32; break;
                case Kind::I64: kind = runtime::RuntimeTypeKind::I64; break;
                case Kind::U32: kind = runtime::RuntimeTypeKind::U32; break;
                case Kind::U64: kind = runtime::RuntimeTypeKind::U64; break;
                case Kind::F32: kind = runtime::RuntimeTypeKind::F32; break;
                case Kind::F64: kind = runtime::RuntimeTypeKind::F64; break;
                case Kind::Bool: kind = runtime::RuntimeTypeKind::Bool; break;
                case Kind::String: kind = runtime::RuntimeTypeKind::String; break;
                case Kind::Pointer:
                case Kind::Reference:
                case Kind::Array: {
                    if (!type.element) throw std::runtime_error("Runtime type description has no element");
                    auto* element = runtimeTypeInfo(*type.element, curFn);
                    if (type.kind == Kind::Array) {
                        return callFactory("__runtime_type_info_array", {element, builder->getInt64(type.count)});
                    }
                    return callFactory(type.kind == Kind::Pointer ? "__runtime_type_info_pointer" :
                        "__runtime_type_info_reference", {element});
                }
                case Kind::Struct:
                    if (type.identity.len() == 0) throw std::runtime_error("Runtime struct description has no identity");
                    return callFactory("__runtime_type_info_struct", {
                        builder->CreateGlobalString(type.identity.c_str(), "runtime.struct.identity"),
                        llvm::ConstantPointerNull::get(builder->getPtrTy())});
                default:
                    throw std::runtime_error("Unsupported backend description for runtime metadata factory");
            }
            return callFactory("__runtime_type_info_basic", {
                builder->getInt8(static_cast<std::uint8_t>(kind))});
        }

        llvm::Value* unboxRaw(llvm::Value* boxed, IR::IRType* type, LLVMFunction* curFn) {
            auto* dataSlot = builder->CreateStructGEP(runtimeValueType(), boxed, 2);
            auto* rawSlot = builder->CreateBitCast(dataSlot, llvm::PointerType::getUnqual(*context));
            return builder->CreateLoad(type->toLLVMType(*context), rawSlot, "unboxed.value");
        }

        const IR::BackendFunction* activeFunction = nullptr;

        IR::IRType* storageType(const IR::IRValue* value) const {
            if (!activeFunction) {
                throw std::runtime_error("LLVM emission requires an active backend function");
            }
            return activeFunction->storageType(value);
        }

        const IR::RuntimeTypeDescription& runtimeType(const IR::IRValue* value) const {
            if (!activeFunction) throw std::runtime_error("LLVM emission requires an active backend function");
            return activeFunction->runtimeType(value);
        }

        // Language locals/parameters and managed array elements hold pointers, not raw scalars.
        bool isPointerSlot(const IR::Instruction* address) const {
            if (!address) return false;
            switch (address->getKind()) {
                case IR::OpKind::create_alloca:
                case IR::OpKind::param:
                case IR::OpKind::deref:
                    return true;
                case IR::OpKind::indexing: {
                    auto* base = storageType(address->arg(0));
                    return base->isArray() || (base->isRef() &&
                        static_cast<IR::IRRefType*>(base)->getElementType()->isArray());
                }
                default:
                    return false;
            }
        }

        llvm::Value* rawValue(IR::IRValue* value, LLVMFunction* curFn) {
            auto* emitted = toLLVMValue(value, curFn);
            if (runtimeType(value).representation != IR::ValueRepresentation::Boxed) return emitted;
            return unboxRaw(emitted, storageType(value), curFn);
        }

        llvm::Value* boxConstant(IR::Constant* constant, LLVMFunction* curFn) {
            using Kind = IR::ConstantKind;
            const auto& type = runtimeType(constant);
            switch (curFn->backend.constants.at(constant)) {
                case Kind::Null:
                    if (type.representation != IR::ValueRepresentation::ManagedArray &&
                        type.representation != IR::ValueRepresentation::Handle) {
                        throw std::runtime_error("Null constant requires a backend handle representation");
                    }
                    return llvm::ConstantPointerNull::get(builder->getPtrTy());
                case Kind::I32: return boxRaw(builder->getInt32(constant->getContentValue<std::int32_t>()), type, curFn);
                case Kind::I64: return boxRaw(builder->getInt64(constant->getContentValue<std::int64_t>()), type, curFn);
                case Kind::U32: return boxRaw(builder->getInt32(constant->getContentValue<std::uint32_t>()), type, curFn);
                case Kind::U64: return boxRaw(builder->getInt64(constant->getContentValue<std::uint64_t>()), type, curFn);
                case Kind::F32: return boxRaw(llvm::ConstantFP::get(builder->getFloatTy(), constant->getContentValue<float>()), type, curFn);
                case Kind::F64: return boxRaw(llvm::ConstantFP::get(builder->getDoubleTy(), constant->getContentValue<double>()), type, curFn);
                case Kind::I8: return boxRaw(builder->getInt8(constant->getContentValue<std::int8_t>()), type, curFn);
                case Kind::Bool: return boxRaw(builder->getInt1(constant->getContentValue<bool>()), type, curFn);
                case Kind::Descriptor:
                    return boxRaw(runtimeTypeInfo(*curFn->backend.typeOperands.at(constant), curFn), type, curFn);
                default: throw std::runtime_error("Constant requires its dedicated backend emitter");
            }
        }

        // 指令引用 ============================================================
        std::map<IR::IRValue*, llvm::Value*> instructionMap;
        std::map<IR::IRValue*, llvm::AllocaInst*> protectedValueSlots;
        // 获取 IRValue 对应的 LLVM Value 引用
        inline llvm::Value* getRef(IR::IRValue* sakIRVal) {
            return instructionMap[sakIRVal];
        }

        // 创建新的 IRValue 到 LLVM Value 的引用
        inline void bind(IR::IRValue* sakIRVal, llvm::Value* llvmIRVal) {
            instructionMap[sakIRVal] = llvmIRVal;
        }

        inline void protectValue(IR::IRValue* sakIRVal, llvm::AllocaInst* slot) {
            protectedValueSlots[sakIRVal] = slot;
        }
        // =====================================================================

        // 模块 =================================================================
        std::vector<LLVMModule*> modules;
        // =====================================================================


        /* 资源 =================================================================
         * =====================================================================
         */
    public:
        LLVMCodeGenerator(const IR::BackendModule& module, llvm::LLVMContext& llvmContext):
            rootModule(module), context(&llvmContext), builder(nullptr) {
            if (!module.context || &module.context->llvmContext() != &llvmContext) {
                throw std::invalid_argument("LLVM emission requires the backend module's LLVM context");
            }
            builder = new llvm::IRBuilder<>(llvmContext);
        }
        ~LLVMCodeGenerator() {
            if (builder) delete builder;

            for (auto mod: modules) delete mod;
        }

        void start();
        std::vector<LLVMModule*> getModules() {
            return modules;
        }
        void print();
        fzlib::String toString();

    private:
        llvm::Value* instgen(IR::Instruction* ins, LLVMFunction* curFn);
        llvm::Value* emitArrayConstant(IR::Constant* constant, LLVMFunction* curFn);

        // 工具方法 ============================================================
        llvm::Value* toLLVMConstant(IR::Constant* constant, LLVMFunction* curFn) {
            if (curFn->backend.constants.at(constant) == IR::ConstantKind::Array) {
                return emitArrayConstant(constant, curFn);
            }
            if (curFn->backend.constants.at(constant) != IR::ConstantKind::String) {
                return boxConstant(constant, curFn);
            }

            const auto& strVal = constant->getContentValue<fzlib::String>();
            auto strVar = builder->CreateGlobalString(strVal.c_str(), "tmpstr");
            auto stringCreator = curFn->parent->lookup("create_string");
            return builder->CreateCall(stringCreator->content, {strVar}, "boxed.string");
        }

        llvm::Value* toLLVMValue(IR::IRValue* value, LLVMFunction* curFn) {
            if (protectedValueSlots.contains(value)) {
                auto* slot = protectedValueSlots[value];
                return builder->CreateLoad(slot->getAllocatedType(), slot, "gc.protected.load");
            }
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
                return curFn->parent->lookup(fn->getName())->content;
            }
            throw std::runtime_error(fzlib::String("Unknown mapping for: " + value->getName()).c_str());
        }

        bool hasLLVMValue(IR::IRValue* value) {
            return instructionMap.contains(value) || protectedValueSlots.contains(value);
        }
        // =====================================================================

        // 计算 =================================================================

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

        llvm::Value* compare(
            llvm::Value* lhs,
            llvm::Value* rhs,
            const IR::RuntimeTypeDescription& lhsType,
            const IR::RuntimeTypeDescription& rhsType,
            IR::OpKind kind,
            LLVMFunction* curFn
        ) {
            if (lhsType.kind == IR::RuntimeDescriptionKind::String || rhsType.kind == IR::RuntimeDescriptionKind::String) {
                if (lhsType.kind != IR::RuntimeDescriptionKind::String || rhsType.kind != IR::RuntimeDescriptionKind::String) {
                    throw std::runtime_error("String values cannot be compared with raw pointer values.");
                }

                if (kind != IR::OpKind::lgc_equal && kind != IR::OpKind::lgc_not_equal) {
                    throw std::runtime_error("Only '==' and '!=' are supported for string values.");
                }

                llvm::FunctionCallee strcmpFunc = curFn->parent->content->getOrInsertFunction(
                    "strcmp", builder->getInt32Ty(), builder->getPtrTy(), builder->getPtrTy()
                );
                llvm::Value* res = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp.tmp");

                if (kind == IR::OpKind::lgc_equal) {
                    return builder->CreateICmpEQ(res, builder->getInt32(0), "str.eq");
                }
                return builder->CreateICmpNE(res, builder->getInt32(0), "str.ne");
            }

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
                // 修正 FCInst 到 FCmpInst 的类型名称错误
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

            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy() &&
                kind == IR::OpKind::lgc_equal) {
                return builder->CreateICmpEQ(lhs, rhs, "ptr.eq");
            }

            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy() &&
                kind == IR::OpKind::lgc_not_equal) {
                return builder->CreateICmpNE(lhs, rhs, "ptr.ne");
            }

            return nullptr;
        }

        // =====================================================================

        // 优化器 ===============================================================
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

#endif /* !SAKURAE_LLVMCODEGENERATOR_HPP */
