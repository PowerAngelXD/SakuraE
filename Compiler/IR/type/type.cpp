#include "type.hpp"
#include "constant.hpp"
#include <llvm/IR/Type.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DerivedTypes.h> // Include for IntegerType, ArrayType etc.
#include <utility> // for std::pair

namespace sakuraE::IR {
    static std::map<unsigned, IntegerType> integerTypes;
    static std::map<Type*, PointerType> pointerTypes;
    static std::map<std::pair<Type*, uint64_t>, ArrayType> arrayTypes;

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
