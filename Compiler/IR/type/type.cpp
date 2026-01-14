#include "type.hpp"

namespace sakuraE::IR {
    static std::map<unsigned, IntegerType> integerTypes;
    static std::map<Type*, PointerType> pointerTypes;
    static std::map<std::pair<Type*, uint64_t>, ArrayType> arrayTypes;
    static std::map<std::pair<Type*, std::vector<Type*>>, FunctionType> funcTypes;

    Type* Type::getVoidTy() {
        static VoidType voidSingle;
        return &voidSingle;
    }

    Type* Type::getBoolTy() {
        static IntegerType boolSingle(1);
        return &boolSingle;
    }

    Type* Type::getCharTy() {
        static IntegerType charSingle(8);
        return &charSingle;
    }

    Type* Type::getInt32Ty() {
        static IntegerType i32Single(32);
        return &i32Single;
    }

    Type* Type::getInt64Ty() {
        static IntegerType i64Single(64);
        return &i64Single;
    }
    
    Type* Type::getIntNTy(unsigned bitWidth) {
        auto it = integerTypes.find(bitWidth);
        if (it != integerTypes.end()) {
            return &it->second;
        }
        auto newEntry = integerTypes.emplace(bitWidth, IntegerType(bitWidth));
        return &newEntry.first->second;
    }
    
    Type* Type::getFloatTy() {
        static FloatType floatSingle;
        return &floatSingle;
    }

    Type* Type::getPointerTo(Type* elementType) {
        auto it = pointerTypes.find(elementType);
        if (it != pointerTypes.end()) {
            return &it->second;
        }
        auto newEntry = pointerTypes.emplace(elementType, PointerType(elementType));
        return &newEntry.first->second;
    }

    Type* Type::getArrayTy(Type* elementType, uint64_t numElements) {
        auto key = std::make_pair(elementType, numElements);
        auto it = arrayTypes.find(key);
        if (it != arrayTypes.end()) {
            return &it->second;
        }
        auto newEntry = arrayTypes.emplace(key, ArrayType(elementType, numElements));

        return &newEntry.first->second;
    }

    Type* Type::getBlockTy() {
        static BlockType blockIndexSingle;

        return &blockIndexSingle;
    }

    Type* Type::getFunctionTy(Type* returnType, std::vector<Type*> params) {
        auto key = std::make_pair(returnType, params);
        auto it = funcTypes.find(key);

        if (it != funcTypes.end()) {
            return &it->second;
        }
        auto newEntry = funcTypes.emplace(key, FunctionType(returnType, params));

        return &newEntry.first->second;
    }


    llvm::Type* VoidType::toLLVMType(llvm::LLVMContext& ctx) {
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
