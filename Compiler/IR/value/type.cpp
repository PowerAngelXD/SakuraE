#include "type.hpp"
#include "constant.hpp"
#include <llvm/IR/Type.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DerivedTypes.h> // Include for IntegerType, ArrayType etc.
#include <utility> // for std::pair

namespace sakuraE::IR {
    Type* Type::getVoidTy() {
        static VoidType Singleton;
        return &Singleton;
    }

    Type* Type::getBoolTy() {
        static IntegerType Singleton(1);
        return &Singleton;
    }

    Type* Type::getCharTy() {
        static IntegerType Singleton(8);
        return &Singleton;
    }

    Type* Type::getInt32Ty() {
        static IntegerType Singleton(32);
        return &Singleton;
    }

    Type* Type::getInt64Ty() {
        static IntegerType Singleton(64);
        return &Singleton;
    }
    
    Type* Type::getIntNTy(unsigned bitWidth) {
        static std::map<unsigned, IntegerType> IntegerTypes;
        auto it = IntegerTypes.find(bitWidth);
        if (it != IntegerTypes.end()) {
            return &it->second;
        }
        auto newEntry = IntegerTypes.emplace(bitWidth, IntegerType(bitWidth));
        return &newEntry.first->second;
    }
    
    Type* Type::getFloatTy() {
        static FloatType Singleton;
        return &Singleton;
    }

    Type* Type::getPointerTo(Type* elementType) {
        static std::map<Type*, PointerType> PointerTypes;
        auto it = PointerTypes.find(elementType);
        if (it != PointerTypes.end()) {
            return &it->second;
        }
        auto newEntry = PointerTypes.emplace(elementType, PointerType(elementType));
        return &newEntry.first->second;
    }

    Type* Type::getArrayTy(Type* elementType, uint64_t numElements) {
        static std::map<std::pair<Type*, uint64_t>, ArrayType> ArrayTypes;
        auto key = std::make_pair(elementType, numElements);
        auto it = ArrayTypes.find(key);
        if (it != ArrayTypes.end()) {
            return &it->second;
        }
        auto newEntry = ArrayTypes.emplace(key, ArrayType(elementType, numElements));
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
        return llvm::PointerType::get(elementType->toLLVMType(ctx), 0);
    }

    llvm::Type* ArrayType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::ArrayType::get(elementType->toLLVMType(ctx), numElements);
    }

    llvm::Type* Constant::toLLVMType(llvm::LLVMContext& ctx) {
        if (type) {
            return type->toLLVMType(ctx);
        }
        return nullptr;
    }
}
