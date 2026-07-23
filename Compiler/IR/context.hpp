#ifndef SAKURAE_IR_CONTEXT_HPP
#define SAKURAE_IR_CONTEXT_HPP

#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace sakuraE::IR {
    class IRContext {
        std::unique_ptr<llvm::LLVMContext> llvmContext_;

        std::unique_ptr<IRVoidType> voidType;
        std::unique_ptr<IRIntegerType> boolType;
        std::unique_ptr<IRIntegerType> charType;
        std::unique_ptr<IRIntegerType> int32Type;
        std::unique_ptr<IRIntegerType> int64Type;
        std::unique_ptr<IRIntegerType> uint32Type;
        std::unique_ptr<IRIntegerType> uint64Type;
        std::unique_ptr<IRFloatType> float32Type;
        std::unique_ptr<IRFloatType> float64Type;
        std::unique_ptr<IRTypeInfoType> typeInfoType;
        std::unique_ptr<IRStringType> stringType;
        std::unique_ptr<IRBlockType> blockType;

        std::map<unsigned, std::unique_ptr<IRIntegerType>> integerTypes;
        std::map<unsigned, std::unique_ptr<IRIntegerType>> uintegerTypes;
        std::map<IRType*, std::unique_ptr<IRPointerType>> pointerTypes;
        std::map<IRType*, std::unique_ptr<IRRefType>> refTypes;
        std::map<std::pair<IRType*, uint64_t>, std::unique_ptr<IRArrayType>> arrayTypes;
        std::map<std::pair<IRType*, std::vector<IRType*>>, std::unique_ptr<IRFunctionType>> functionTypes;
        std::unique_ptr<TypeInfoPool> typeInfoPool_;

        IRContext* previousContext = nullptr;
        static thread_local IRContext* activeContext;

    public:
        IRContext();
        ~IRContext();

        IRContext(const IRContext&) = delete;
        IRContext& operator=(const IRContext&) = delete;
        IRContext(IRContext&&) = delete;
        IRContext& operator=(IRContext&&) = delete;

        static IRContext& current();

        llvm::LLVMContext& llvmContext() { return *llvmContext_; }
        std::unique_ptr<llvm::LLVMContext> releaseLLVMContext();
        TypeInfoPool& typeInfoPool() { return *typeInfoPool_; }

        IRType* getVoidTy();
        IRType* getBoolTy();
        IRType* getCharTy();
        IRType* getInt32Ty();
        IRType* getInt64Ty();
        IRType* getIntNTy(unsigned bitWidth);
        IRType* getUInt32Ty();
        IRType* getUInt64Ty();
        IRType* getUIntNTy(unsigned bitWidth);
        IRType* getFloat32Ty();
        IRType* getFloat64Ty();
        IRType* getTypeInfoTy();
        IRType* getStringTy();
        IRType* getPointerTo(IRType* elementType);
        IRType* getRefTo(IRType* elementType);
        IRType* getArrayTy(IRType* elementType, uint64_t numElements);
        IRType* getBlockTy();
        IRType* getFunctionTy(IRType* returnType, std::vector<IRType*> params);
    };
}

#endif
