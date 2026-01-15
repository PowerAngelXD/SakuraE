#include "type.hpp"

namespace sakuraE::IR {
    static std::map<unsigned, IntegerType> integerTypes;
    static std::map<IRType*, PointerType> pointerTypes;
    static std::map<std::pair<IRType*, uint64_t>, ArrayType> arrayTypes;
    static std::map<std::pair<IRType*, std::vector<IRType*>>, FunctionType> funcTypes;

    IRType* IRType::getVoidTy() {
        static IRVoidType voidSingle;
        return &voidSingle;
    }

    IRType* IRType::getBoolTy() {
        static IntegerType boolSingle(1);
        return &boolSingle;
    }

    IRType* IRType::getCharTy() {
        static IntegerType charSingle(8);
        return &charSingle;
    }

    IRType* IRType::getInt32Ty() {
        static IntegerType i32Single(32);
        return &i32Single;
    }

    IRType* IRType::getInt64Ty() {
        static IntegerType i64Single(64);
        return &i64Single;
    }
    
    IRType* IRType::getIntNTy(unsigned bitWidth) {
        auto it = integerTypes.find(bitWidth);
        if (it != integerTypes.end()) {
            return &it->second;
        }
        auto newEntry = integerTypes.emplace(bitWidth, IntegerType(bitWidth));
        return &newEntry.first->second;
    }
    
    IRType* IRType::getFloatTy() {
        static FloatType floatSingle;
        return &floatSingle;
    }

    IRType* IRType::getPointerTo(IRType* elementType) {
        auto it = pointerTypes.find(elementType);
        if (it != pointerTypes.end()) {
            return &it->second;
        }
        auto newEntry = pointerTypes.emplace(elementType, PointerType(elementType));
        return &newEntry.first->second;
    }

    IRType* IRType::getArrayTy(IRType* elementType, uint64_t numElements) {
        auto key = std::make_pair(elementType, numElements);
        auto it = arrayTypes.find(key);
        if (it != arrayTypes.end()) {
            return &it->second;
        }
        auto newEntry = arrayTypes.emplace(key, ArrayType(elementType, numElements));

        return &newEntry.first->second;
    }

    IRType* IRType::getBlockTy() {
        static BlockType blockIndexSingle;

        return &blockIndexSingle;
    }

    IRType* IRType::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        auto key = std::make_pair(returnType, params);
        auto it = funcTypes.find(key);

        if (it != funcTypes.end()) {
            return &it->second;
        }
        auto newEntry = funcTypes.emplace(key, FunctionType(returnType, params));

        return &newEntry.first->second;
    }


    llvm::Type* IRVoidType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getVoidTy(ctx);
    }

    llvm::Type* FloatType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getFloatTy(ctx);
    }

    llvm::Type* IntegerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getIntNTy(ctx, bitWidth);
    }

    llvm::Type* PointerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::PointerType::get(ctx, 0);
    }

    llvm::Type* ArrayType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::ArrayType::get(elementType->toLLVMType(ctx), numElements);
    }

    llvm::Type* BlockType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getLabelTy(ctx);
    }

    llvm::Type* FunctionType::toLLVMType(llvm::LLVMContext& ctx) {
        std::vector<llvm::Type*> llvmParams;
        for (auto arg: paramsType) {
            llvmParams.push_back(arg->toLLVMType(ctx));
        }

        return llvm::FunctionType::get(returnType->toLLVMType(ctx), llvmParams, false);
    }
}
