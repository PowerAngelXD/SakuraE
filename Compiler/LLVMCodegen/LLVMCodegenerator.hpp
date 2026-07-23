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
#include "Compiler/IR/model/function.hpp"
#include "Compiler/IR/model/instruction.hpp"
#include "Compiler/IR/model/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/value/value.hpp"
#include "includes/String.hpp"
#include "Runtime/gc.h"
#include "Runtime/rttype.h"

namespace sakuraE::Codegen {
    class LLVMCodeGenerator {
    public:
        IR::Program* program;
        llvm::LLVMContext* context;
        llvm::IRBuilder<>* builder;
    private:
        // 结构体定义 =========================================================
        enum class FunctionType {
            Definition,
            ExternalLinkage
        };

        struct LLVMModule;
        // LLVM 函数实例
        struct LLVMFunction {
            // 函数类型
            FunctionType type;
            // 函数链接名称
            fzlib::String linkageName;
            // 函数名称
            fzlib::String name;
            // LLVM IR 函数表示
            llvm::Function* content = nullptr;
            // 函数返回类型
            llvm::Type* returnType = nullptr;
            // 函数形式参数
            std::vector<std::pair<fzlib::String, llvm::Type*>> formalParams;
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
            // SAK IR 函数
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

            void gcRegisterManagedSlot(llvm::Value* addr, IR::IRType* irType) {
                if (irType && irType->isArray()) {
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

            /* 当前 GC 只把“真正的托管对象引用”纳入 root stack：
             * 1. string 对象
             * 2. array 对象，语义上对应堆分配的数组 payload
             * ref、取地址和索引等派生地址不视作 GC root
             */
            bool isManagedStringType(IR::IRType* ty) const {
                return ty && ty->isString();
            }

            bool isRawCharPointerType(IR::IRType* ty) const {
                if (!ty || !ty->isPointer()) {
                    return false;
                }

                auto* ptrTy = dynamic_cast<IR::IRPointerType*>(ty);
                return ptrTy && ptrTy->getElementType() == IR::IRType::getCharTy();
            }

            bool isManagedHeapType(IR::IRType* ty) const {
                if (!ty) {
                    return false;
                }

                if (ty->isArray()) {
                    return true;
                }

                return isManagedStringType(ty);
            }

            bool shouldTrackAsGCRoot(IR::IRValue* value) const {
                if (!value || !isManagedHeapType(value->getType())) {
                    return false;
                }

                if (auto* inst = dynamic_cast<IR::Instruction*>(value)) {
                    switch (inst->getKind()) {
                        case IR::OpKind::constant:
                        case IR::OpKind::create_array:
                        case IR::OpKind::call:
                        case IR::OpKind::load:
                            return true;
                        case IR::OpKind::gaddr:
                        case IR::OpKind::indexing:
                        case IR::OpKind::deref:
                        case IR::OpKind::param:
                        case IR::OpKind::create_alloca:
                            return false;
                        default:
                            return false;
                    }
                }

                return true;
            }

            bool shouldRegisterSlotAsGCRoot(IR::IRType* ty) const {
                return isManagedHeapType(ty);
            }

            llvm::AllocaInst* createRootedTemporary(
                llvm::Value* value,
                const fzlib::String& slotName,
                IR::IRType* irType) {
                // 将 SSA 值落到 entry block 的槽位，避免后续分配发生时值无法作为 root 被扫描
                auto* slot = createAlloca(value->getType(), nullptr, slotName);
                codegenContext.builder->CreateStore(value, slot);
                gcRegisterManagedSlot(slot, irType);
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
            void impl(IR::Function* source);
            // 开始生成 LLVM IR
            void codegen();
        };
        // LLVM 模块实例
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

            llvm::StructType* runtimeValueType() {
                return codegenContext.runtimeValueType();
            }

            llvm::Type* abiType(IR::IRType* type, const fzlib::String& name) {
                if (!type || type->getIRTypeID() == IR::IRTypeID::VoidTyID) {
                    return llvm::Type::getVoidTy(*codegenContext.context);
                }

                if (name == "__gc_alloc" || name == "__alloc" || name == "__free" ||
                    name == "__gc_register" || name == "__gc_pop") {
                    return type->toLLVMType(*codegenContext.context);
                }

                return llvm::PointerType::getUnqual(*codegenContext.context);
            }

            ~LLVMModule() {
                for (auto& pair : fnMap) {
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

            // 创建 LLVM 模块，并将 IR 模块转换为 LLVM 模块
            void impl(IR::Module* source);

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

        llvm::Type* abiType(IR::IRType* type, const fzlib::String& name) {
            if (!type || type->getIRTypeID() == IR::IRTypeID::VoidTyID) {
                return llvm::Type::getVoidTy(*context);
            }
            if (name == "__gc_alloc" || name == "__alloc" || name == "__free" ||
                name == "__gc_register" || name == "__gc_register_value" ||
                name == "__gc_pop" || name == "__gc_get_struct_type" ||
                name == "__runtime_type_info_basic" ||
                name == "__runtime_type_info_array" ||
                name == "__runtime_check_array_bounds" ||
                name == "__runtime_array_bounds_error") {
                return type->toLLVMType(*context);
            }
            return llvm::PointerType::getUnqual(*context);
        }

        llvm::Type* abiParamType(IR::IRType* type, const fzlib::String& name) {
            if ((name == "create_string" || name == "__gc_register_value") && type && type->isPointer()) {
                return type->toLLVMType(*context);
            }
            if (name == "__gc_get_struct_type") {
                return type->toLLVMType(*context);
            }
            return abiType(type, name);
        }

        llvm::Value* boxRaw(llvm::Value* raw, IR::IRType* type, LLVMFunction* curFn) {
            auto* allocFn = curFn->parent->lookup("__runtime_alloc_value");
            auto* resultSlot = builder->CreateCall(allocFn->content, {}, "runtime.value");
            builder->CreateStore(llvm::Constant::getNullValue(runtimeValueType()), resultSlot);
            builder->CreateStore(builder->getInt8(runtimeTypeTag(type)),
                                 builder->CreateStructGEP(runtimeValueType(), resultSlot, 0));

            auto* dataSlot = builder->CreateStructGEP(runtimeValueType(), resultSlot, 2);
            auto* rawSlot = builder->CreateBitCast(dataSlot, llvm::PointerType::getUnqual(*context));
            builder->CreateStore(raw, rawSlot);
            return resultSlot;
        }

        std::uint8_t runtimeTypeTag(IR::IRType* type) const {
            switch (type->getIRTypeID()) {
                case IR::IRTypeID::CharTyID: return 0;
                case IR::IRTypeID::Integer32TyID: return 1;
                case IR::IRTypeID::Integer64TyID: return 2;
                case IR::IRTypeID::UInteger32TyID: return 4;
                case IR::IRTypeID::UInteger64TyID: return 5;
                case IR::IRTypeID::Float32TyID: return 6;
                case IR::IRTypeID::Float64TyID: return 7;
                case IR::IRTypeID::BoolTyID: return 8;
                case IR::IRTypeID::StringTyID: return 9;
                case IR::IRTypeID::ArrayTyID: return 10;
                case IR::IRTypeID::TypeInfoTyID:
                    return 13;
                case IR::IRTypeID::PointerTyID:
                case IR::IRTypeID::RefTyID: return 12;
                default: return 12;
            }
        }

        llvm::Value* runtimeTypeInfo(IR::IRType* type, LLVMFunction* curFn) {
            auto callFactory = [&](const fzlib::String& name, std::vector<llvm::Value*> args) {
                auto* fn = curFn->parent->lookup(name);
                return builder->CreateCall(fn->content, args, "runtime.typeinfo");
            };

            std::uint8_t kind = 0;
            switch (type->getIRTypeID()) {
                case IR::IRTypeID::VoidTyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::Void); break;
                case IR::IRTypeID::CharTyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::I8); break;
                case IR::IRTypeID::Integer32TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::I32); break;
                case IR::IRTypeID::Integer64TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::I64); break;
                case IR::IRTypeID::UInteger32TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::U32); break;
                case IR::IRTypeID::UInteger64TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::U64); break;
                case IR::IRTypeID::Float32TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::F32); break;
                case IR::IRTypeID::Float64TyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::F64); break;
                case IR::IRTypeID::BoolTyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::Bool); break;
                case IR::IRTypeID::StringTyID: kind = static_cast<std::uint8_t>(runtime::RuntimeTypeKind::String); break;
                case IR::IRTypeID::PointerTyID: {
                    auto* pointer = static_cast<IR::IRPointerType*>(type);
                    return callFactory("__runtime_type_info_pointer", {
                        runtimeTypeInfo(pointer->getElementType(), curFn)
                    });
                }
                case IR::IRTypeID::RefTyID: {
                    auto* reference = static_cast<IR::IRRefType*>(type);
                    return callFactory("__runtime_type_info_reference", {
                        runtimeTypeInfo(reference->getElementType(), curFn)
                    });
                }
                case IR::IRTypeID::ArrayTyID: {
                    auto* array = static_cast<IR::IRArrayType*>(type);
                    return callFactory("__runtime_type_info_array", {
                        runtimeTypeInfo(array->getElementType(), curFn),
                        builder->getInt64(array->getNumElements())
                    });
                }
                default:
                    throw std::runtime_error("Unsupported IR type for Runtime::TypeInfo lowering");
            }

            return callFactory("__runtime_type_info_basic", {builder->getInt8(kind)});
        }

        llvm::Value* unboxRaw(llvm::Value* boxed, IR::IRType* type, LLVMFunction* curFn) {
            auto* dataSlot = builder->CreateStructGEP(runtimeValueType(), boxed, 2);
            auto* rawSlot = builder->CreateBitCast(dataSlot, llvm::PointerType::getUnqual(*context));
            return builder->CreateLoad(type->toLLVMType(*context), rawSlot, "unboxed.value");
        }

        llvm::Value* rawValue(IR::IRValue* value, LLVMFunction* curFn) {
            return unboxRaw(toLLVMValue(value, curFn), value->getType(), curFn);
        }

        llvm::Value* boxConstant(IR::Constant* constant, LLVMFunction* curFn) {
            switch (constant->getType()->getIRTypeID()) {
                case IR::IRTypeID::Integer32TyID:
                    return boxRaw(builder->getInt32(constant->getContentValue<std::int32_t>()), constant->getType(), curFn);
                case IR::IRTypeID::Integer64TyID:
                    return boxRaw(builder->getInt64(constant->getContentValue<std::int64_t>()), constant->getType(), curFn);
                case IR::IRTypeID::UInteger32TyID:
                    return boxRaw(builder->getInt32(constant->getContentValue<std::uint32_t>()), constant->getType(), curFn);
                case IR::IRTypeID::UInteger64TyID:
                    return boxRaw(builder->getInt64(constant->getContentValue<std::uint64_t>()), constant->getType(), curFn);
                case IR::IRTypeID::Float32TyID:
                    return boxRaw(llvm::ConstantFP::get(builder->getFloatTy(), constant->getContentValue<float>()), constant->getType(), curFn);
                case IR::IRTypeID::Float64TyID:
                    return boxRaw(llvm::ConstantFP::get(builder->getDoubleTy(), constant->getContentValue<double>()), constant->getType(), curFn);
                case IR::IRTypeID::CharTyID:
                    return boxRaw(builder->getInt8(constant->getContentValue<std::int8_t>()), constant->getType(), curFn);
                case IR::IRTypeID::BoolTyID:
                    return boxRaw(builder->getInt1(constant->getContentValue<bool>()), constant->getType(), curFn);
                case IR::IRTypeID::TypeInfoTyID:
                    return boxRaw(
                        runtimeTypeInfo(constant->getContentValue<IR::TypeInfo*>()->toIRType(), curFn),
                        constant->getType(),
                        curFn);
                default:
                    throw std::runtime_error(fzlib::String("Unsupported boxed constant type: " + constant->getType()->toString()).c_str());
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

        // 状态工具 ============================================================
        IR::Module* curIRModule() {
            return program->curMod();
        }

        IR::Function* curIRFunc() {
            return curIRModule()->curFunc();
        }

        // 在当前活动 IR 函数的作用域中查找指定名称的标识符
        template<typename T>
        IR::Symbol<T>* IRScopeLookup(fzlib::String n) {
            return curIRFunc()->fnScope().lookup(n);
        }
        // =====================================================================

        /* 资源 =================================================================
         * =====================================================================
         */
    public:
        LLVMCodeGenerator()=default;
        LLVMCodeGenerator(IR::Program* p) {
            program = p;
            context = &program->getContext().llvmContext();
            builder = new llvm::IRBuilder<>(*context);

            // 重置状态管理器
            program->reset();
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

        std::unique_ptr<llvm::LLVMContext> releaseContext() {
            if (!context) return nullptr;

            auto ptr = program->getContext().releaseLLVMContext();
            context = nullptr;
            return ptr;
        }
    private:
        llvm::Value* instgen(IR::Instruction* ins, LLVMFunction* curFn);

        // 工具方法 ============================================================
        llvm::Value* toLLVMConstant(IR::Constant* constant, LLVMFunction* curFn) {
            if (constant->getType()->getIRTypeID() != IR::IRTypeID::StringTyID) {
                return boxConstant(constant, curFn);
            }

            fzlib::String strVal = constant->getContentValue<fzlib::String>();
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
                return curFn->parent->fnMap[fn->getName()]->content;
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
            IR::IRType* lhsType,
            IR::IRType* rhsType,
            IR::OpKind kind,
            LLVMFunction* curFn
        ) {
            if (lhsType && rhsType && (lhsType->isString() || rhsType->isString())) {
                if (!(lhsType->isString() && rhsType->isString())) {
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
