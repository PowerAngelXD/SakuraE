#include "type.hpp"
#include <llvm/Support/Casting.h>

namespace sakuraE::IR {
    static std::map<unsigned, IRIntegerType> IRIntegerTypes;
    static std::map<unsigned, IRIntegerType> IRUIntegerTypes;
    static std::map<IRType*, IRPointerType> IRPointerTypes;
    static std::map<std::pair<IRType*, uint64_t>, IRArrayType> arrayTypes;
    static std::map<std::pair<IRType*, std::vector<IRType*>>, IRFunctionType> funcTypes;

    IRType* IRType::unboxComplex() {
        if (!isComplexType()) return this;
        else {
            IRType* result = this;

            auto isBasicType = [&]()->bool {
                if (!result->isComplexType()) return true;

                if (result->irTypeID == PointerTyID) {
                    auto ptr = static_cast<IRPointerType*>(result);
                    if (ptr->getElementType()->irTypeID == IRTypeID::CharTyID) return true;
                }
                else if (result->irTypeID == ArrayTyID) {
                    return true;
                }

                return false;
            };

            while (!isBasicType()) {
                if (result->irTypeID == ArrayTyID) {
                    auto arrTy = static_cast<IRArrayType*>(result);
                    result = arrTy->getElementType();
                }
                else if (result->irTypeID == PointerTyID) {
                    auto ptrTy = static_cast<IRPointerType*>(result);
                    result = ptrTy->getElementType();
                }
            }

            return result;
        }
    }

    bool IRType::isEqual(IRType* ty) {
        if (this == ty) return true;
        if (!ty) return false;
        if (irTypeID != ty->irTypeID) return false;

        switch (irTypeID) {
            case Integer32TyID:
            case Integer64TyID:
            case UInteger32TyID:
            case UInteger64TyID:
            case CharTyID:
            case BoolTyID:
            case TypeInfoTyID:
            case FloatTyID:
            case VoidTyID:
                return true;
            case IntegerNTyID:
            case UIntegerNTyID: {
                auto lInt = static_cast<IRIntegerType*>(this);
                auto rInt = static_cast<IRIntegerType*>(ty);
                return lInt->getBitWidth() == rInt->getBitWidth();
            }
            case ArrayTyID: {
                auto lArr = static_cast<IRArrayType*>(this);
                auto rArr = static_cast<IRArrayType*>(ty);
                return lArr->getNumElements() == rArr->getNumElements() &&
                    lArr->getElementType()->isEqual(rArr->getElementType());
            }
            case PointerTyID: {
                auto lPtr = static_cast<IRPointerType*>(this);
                auto rPtr = static_cast<IRPointerType*>(ty);
                return lPtr->getElementType()->isEqual(rPtr->getElementType());
            }
            case FunctionTyID: {
                auto lFn = static_cast<IRFunctionType*>(this);
                auto rFn = static_cast<IRFunctionType*>(ty);
                auto& lParams = lFn->paramsType;
                auto& rParams = rFn->paramsType;
                if (lParams.size() != rParams.size()) return false;
                else {
                    for (std::size_t i = 0; i < lParams.size(); i ++) {
                        if (!lParams[i]->isEqual(rParams[i])) return false;
                    }
                }
                return lFn->returnType->isEqual(rFn->returnType);
            }
            default: return false;
        }
    }

    IRType* IRType::getVoidTy() {
        static IRVoidType voidSingle;
        return &voidSingle;
    }

    IRType* IRType::getBoolTy() {
        static IRIntegerType boolSingle(1);
        return &boolSingle;
    }

    IRType* IRType::getCharTy() {
        static IRIntegerType charSingle(8);
        return &charSingle;
    }

    IRType* IRType::getInt32Ty() {
        static IRIntegerType i32Single(32);
        return &i32Single;
    }

    IRType* IRType::getInt64Ty() {
        static IRIntegerType i64Single(64);
        return &i64Single;
    }
    
    IRType* IRType::getIntNTy(unsigned bitWidth) {
        auto it = IRIntegerTypes.find(bitWidth);
        if (it != IRIntegerTypes.end()) {
            return &it->second;
        }
        auto newEntry = IRIntegerTypes.emplace(bitWidth, IRIntegerType(bitWidth));
        return &newEntry.first->second;
    }

    IRType* IRType::getUInt32Ty() {
        static IRIntegerType ui32Single(32, false);
        return &ui32Single;
    }
    IRType* IRType::getUInt64Ty() {
        static IRIntegerType ui64Single(64, false);
        return &ui64Single;
    }
    IRType* IRType::getUIntNTy(unsigned bitWidth) {
        auto it = IRUIntegerTypes.find(bitWidth);
        if (it != IRUIntegerTypes.end()) {
            return &it->second;
        }
        auto newEntry = IRUIntegerTypes.emplace(bitWidth, IRIntegerType(bitWidth, false));
        return &newEntry.first->second;
    }

    IRType* IRType::getTypeInfoTy() {
        static IRTypeInfoType tinfoSingle;
        return &tinfoSingle;
    }
    
    IRType* IRType::getFloatTy() {
        static IRFloatType floatSingle;
        return &floatSingle;
    }

    IRType* IRType::getPointerTo(IRType* elementType) {
        auto it = IRPointerTypes.find(elementType);
        if (it != IRPointerTypes.end()) {
            return &it->second;
        }
        auto newEntry = IRPointerTypes.emplace(elementType, IRPointerType(elementType));
        return &newEntry.first->second;
    }

    IRType* IRType::getArrayTy(IRType* elementType, uint64_t numElements) {
        auto key = std::make_pair(elementType, numElements);
        auto it = arrayTypes.find(key);
        if (it != arrayTypes.end()) {
            return &it->second;
        }
        auto newEntry = arrayTypes.emplace(key, IRArrayType(elementType, numElements));

        return &newEntry.first->second;
    }

    IRType* IRType::getBlockTy() {
        static IRBlockType blockIndexSingle;

        return &blockIndexSingle;
    }

    IRType* IRType::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        auto key = std::make_pair(returnType, params);
        auto it = funcTypes.find(key);

        if (it != funcTypes.end()) {
            return &it->second;
        }
        auto newEntry = funcTypes.emplace(key, IRFunctionType(returnType, params));

        return &newEntry.first->second;
    }


    llvm::Type* IRVoidType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getVoidTy(ctx);
    }

    llvm::Type* IRFloatType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getFloatTy(ctx);
    }

    llvm::Type* IRIntegerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getIntNTy(ctx, bitWidth);
    }

    llvm::Type* IRPointerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::PointerType::get(ctx, 0);
    }

    llvm::Type* IRArrayType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::ArrayType::get(elementType->toLLVMType(ctx), numElements);
    }

    llvm::Type* IRBlockType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getLabelTy(ctx);
    }

    llvm::Type* IRFunctionType::toLLVMType(llvm::LLVMContext& ctx) {
        std::vector<llvm::Type*> llvmParams;
        for (auto arg: paramsType) {
            llvmParams.push_back(arg->toLLVMType(ctx));
        }

        return llvm::FunctionType::get(returnType->toLLVMType(ctx), llvmParams, false);
    }

    llvm::Type* IRTypeInfoType::toLLVMType(llvm::LLVMContext& ctx) {
        llvm::StructType* structTy = llvm::StructType::getTypeByName(ctx, "sakuraE.TypeInfo");
    
        if (!structTy) {
            structTy = llvm::StructType::create(ctx, "sakuraE.TypeInfo");
            structTy->setBody({
                llvm::Type::getInt32Ty(ctx),
                llvm::PointerType::getUnqual(ctx)
            });
        }
    
        return llvm::PointerType::getUnqual(ctx);
    }

    // toString

    fzlib::String IRVoidType::toString() {
        return "<VoidType>";
    }

    fzlib::String IRFloatType::toString() {
        return "<FloatType>";
    }

    fzlib::String IRIntegerType::toString() {
        return "<Int" + std::to_string(bitWidth) + "Type>";
    }

    fzlib::String IRTypeInfoType::toString() {
        return "<TypeInfo>";
    }

    fzlib::String IRPointerType::toString() {
        return "<" + elementType->toString() + "PointerType>";
    }

    fzlib::String IRArrayType::toString() {
        return "<" + elementType->toString() + "[" + std::to_string(numElements) + "]>";
    }

    fzlib::String IRBlockType::toString() {
        return "<IRBlockType>";
    }

    fzlib::String IRFunctionType::toString() {
        fzlib::String result = "<IRFunctionType -> " + returnType->toString() + ", [";
        for (auto type: paramsType) {
            result += type->toString() + " ";
        }
        result += "]>";

        return result;
    }
}
