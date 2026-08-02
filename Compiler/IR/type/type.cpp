#include "type.hpp"
#include "Compiler/IR/context.hpp"
#include <Compiler/Error/error.hpp>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Casting.h>
#include <stdexcept>

namespace sakuraE::IR {
    IRType* IRType::unwrapPointer() {
        if (!isPointer()) return this;
        else {
            auto ptr = static_cast<IRPointerType*>(this);
            return ptr->getElementType();
        }
    }

    IRType* IRType::getStorageType() {
        if (!isPointer()) return this;
        else {
            auto result = this;
            auto checker = [&]() -> bool {
                if (result->isPointer()) {
                    auto ptr = static_cast<IRPointerType*>(result);
                    if (ptr->elementType->getIRTypeID() == CharTyID) return true;
                    else return false;
                }
                else return true;
            };

            while (!checker()) {
                result = static_cast<IRPointerType*>(result)->getElementType();
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
            case StringTyID:
            case Float32TyID:
            case Float64TyID:
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
            case StructTyID: {
                auto lStruct = static_cast<IRStructType*>(this);
                auto rStruct = static_cast<IRStructType*>(ty);
                return lStruct->parentModID == rStruct->parentModID &&
                    lStruct->name == rStruct->name;
            }
            default: return false;
        }
    }

    IRType* IRType::getVoidTy() {
        return IRContext::current().getVoidTy();
    }

    IRType* IRType::getBoolTy() {
        return IRContext::current().getBoolTy();
    }

    IRType* IRType::getCharTy() {
        return IRContext::current().getCharTy();
    }

    IRType* IRType::getInt32Ty() {
        return IRContext::current().getInt32Ty();
    }

    IRType* IRType::getInt64Ty() {
        return IRContext::current().getInt64Ty();
    }

    IRType* IRType::getIntNTy(unsigned bitWidth) {
        return IRContext::current().getIntNTy(bitWidth);
    }

    IRType* IRType::getUInt32Ty() {
        return IRContext::current().getUInt32Ty();
    }
    IRType* IRType::getUInt64Ty() {
        return IRContext::current().getUInt64Ty();
    }
    IRType* IRType::getUIntNTy(unsigned bitWidth) {
        return IRContext::current().getUIntNTy(bitWidth);
    }

    IRType* IRType::getTypeInfoTy() {
        return IRContext::current().getTypeInfoTy();
    }

    IRType* IRType::getStringTy() {
        return IRContext::current().getStringTy();
    }

    IRType* IRType::getFloat32Ty() {
        return IRContext::current().getFloat32Ty();
    }

    IRType* IRType::getFloat64Ty() {
        return IRContext::current().getFloat64Ty();
    }

    IRType* IRType::getPointerTo(IRType* elementType) {
        return IRContext::current().getPointerTo(elementType);
    }

    IRType* IRType::getRefTo(IRType* elementType) {
        return IRContext::current().getRefTo(elementType);
    }

    IRType* IRType::getArrayTy(IRType* elementType, uint64_t numElements) {
        return IRContext::current().getArrayTy(elementType, numElements);
    }

    IRType* IRType::getBlockTy() {
        return IRContext::current().getBlockTy();
    }

    IRType* IRType::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        return IRContext::current().getFunctionTy(returnType, std::move(params));
    }

    llvm::Type* IRVoidType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getVoidTy(ctx);
    }

    llvm::Type* IRFloatType::toLLVMType(llvm::LLVMContext& ctx) {
        if (bitWidth == 32) return llvm::Type::getFloatTy(ctx);
        else if (bitWidth == 64) return llvm::Type::getDoubleTy(ctx);
        else throw std::runtime_error("Not support other float");
    }

    llvm::Type* IRIntegerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getIntNTy(ctx, bitWidth);
    }

    llvm::Type* IRPointerType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::PointerType::get(ctx, 0);
    }

    llvm::Type* IRStringType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::PointerType::getUnqual(ctx);
    }

    llvm::Type* IRRefType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::PointerType::get(ctx, 0);
    }

    llvm::Type* IRArrayType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::ArrayType::get(elementType->toLLVMType(ctx), numElements);
    }

    llvm::Type* IRBlockType::toLLVMType(llvm::LLVMContext& ctx) {
        return llvm::Type::getLabelTy(ctx);
    }

    llvm::Type* IRStructType::toLLVMType(llvm::LLVMContext& ctx) {
        fzlib::String structTypeName = "sakurae.struct." + parentModID + "." + name;
        auto structType = llvm::StructType::getTypeByName(ctx, structTypeName.c_str());
        if (!structType) {
            structType = llvm::StructType::create(ctx, structTypeName.c_str());
        }

        if (!isCompleteType) return structType;

        if (structType->isOpaque()) {
            std::vector<llvm::Type*> memberTypes;
            memberTypes.reserve(fields.size());
            for (auto& field: fields) {
                memberTypes.push_back(field.type->toLLVMType(ctx));
            }
            structType->setBody(memberTypes, false);
        }

        return structType;
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

    // 转换为字符串

    fzlib::String IRVoidType::toString() {
        return "void";
    }

    fzlib::String IRFloatType::toString() {
        if (bitWidth == 32) return "f32";
        else if (bitWidth == 64) return "f64";
        else return "fN";
    }

    fzlib::String IRIntegerType::toString() {
        if (bitWidth == 8) return "char";
        else return "i" + std::to_string(bitWidth) ;
    }

    fzlib::String IRTypeInfoType::toString() {
        return "tinfo";
    }

    fzlib::String IRStringType::toString() {
        return "string";
    }

    fzlib::String IRPointerType::toString() {
        return elementType->toString() + "*";
    }

    fzlib::String IRRefType::toString() {
        return elementType->toString() + "&";
    }

    fzlib::String IRArrayType::toString() {
        return "" + elementType->toString() + "[" + std::to_string(numElements) + "]";
    }

    fzlib::String IRBlockType::toString() {
        return "irblock";
    }

    fzlib::String IRFunctionType::toString() {
        fzlib::String result = "fn->" + returnType->toString() + ",";
        for (auto type: paramsType) {
            result += type->toString() + "|";
        }

        return result;
    }

    fzlib::String IRStructType::toString() {
        fzlib::String result = "struct {";
        for (std::size_t i = 0; i < fields.size(); i ++) {
            result += fields[i].type->toString();
            if (i == fields.size() - 1);
            else result += ", ";
        }
        result += "}";
        return result;
    }

    const fzlib::String& IRStructType::getName() const { return name; }

    std::size_t IRStructType::getCount() const { return fields.size(); }

    const std::vector<IRStructType::FieldInfo>& IRStructType::getFields() const {
        return fields;
    }

    bool IRStructType::isComplete() const {
        return isCompleteType;
    }

    std::optional<IRStructType::FieldInfo> IRStructType::findMember(
        const fzlib::String& target) const {
        const auto it = fieldIndices.find(target);
        if (it == fieldIndices.end()) return std::nullopt;
        return fields[it->second];
    }

    void IRStructType::complete(std::vector<IRStructType::FieldInfo> fs) {
        fields = std::move(fs);
        isCompleteType = true;

        for (std::size_t i = 0; i < fields.size(); i ++) {
            fieldIndices[fields[i].name] = i;
        }
    }

}
