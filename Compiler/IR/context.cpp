#include "Compiler/IR/context.hpp"

#include "Compiler/IR/model/struct.hpp"

#include <Compiler/IR/type/type.hpp>
#include <memory>
#include <stdexcept>

namespace sakuraE::IR {
    thread_local IRContext* IRContext::activeContext = nullptr;

    NamingContext::NamingContext(fzlib::String id): moduleID(std::move(id)) {}

    NamingContext::~NamingContext() = default;

    IRStructDecl* NamingContext::lookupStructDecl(const fzlib::String& name) const {
        const auto it = structDecls.find(name);
        return it == structDecls.end() ? nullptr : it->second.get();
    }

    IRStructType* NamingContext::lookupStructType(const fzlib::String& name) const {
        auto* decl = lookupStructDecl(name);
        return decl ? decl->getType() : nullptr;
    }

    IRStructDecl* NamingContext::declareOpaqueStruct(fzlib::String name, PositionInfo info) {
        if (lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Duplicate struct definition: '" + name + "'",
                              info);
        }
        auto opaqueType = std::unique_ptr<IRStructType>(new IRStructType(moduleID, name));
        auto decl = std::make_unique<IRStructDecl>(moduleID, name, std::move(opaqueType), std::move(info));
        auto* result = decl.get();
        structDecls.emplace(name, std::move(decl));
        return result;
    }

    void NamingContext::implStruct(fzlib::String name, std::vector<IRStructType::FieldInfo> fields,
                                   std::map<fzlib::String, Constant*> defaults, PositionInfo info) {
        if (!lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Unknown struct definition: '" + name + "'",
                              info);
        }

        if (lookupStructDecl(name)->getType()->isComplete()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "You cannot redefine a struct that has already been defined.",
                              info);
        }

        lookupStructDecl(name)->complete(std::move(fields), std::move(defaults));
    }

    IRStructDecl* NamingContext::defineStruct(fzlib::String name, std::vector<IRStructType::FieldInfo> fields, PositionInfo info) {
        if (lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Duplicate struct definition: '" + name + "'",
                              info);
        }

        auto type = std::unique_ptr<IRStructType>(
            new IRStructType(moduleID, name, std::move(fields)));
        auto decl = std::make_unique<IRStructDecl>(
            moduleID, name, std::move(type), std::move(info));
        auto* result = decl.get();
        structDecls.emplace(std::move(name), std::move(decl));
        return result;
    }

    IRContext::IRContext():
        llvmContext_(std::make_unique<llvm::LLVMContext>()),
        typeInfoPool_(new TypeInfoPool(*this)),
        previousContext(activeContext) {
        activeContext = this;
    }

    IRContext::~IRContext() {
        if (activeContext == this) {
            activeContext = previousContext;
        }
    }

    IRContext& IRContext::current() {
        if (!activeContext) {
            throw std::runtime_error("No active IRContext");
        }
        return *activeContext;
    }

    std::unique_ptr<llvm::LLVMContext> IRContext::releaseLLVMContext() {
        return std::move(llvmContext_);
    }

    IRType* IRContext::getVoidTy() {
        if (!voidType) voidType = std::unique_ptr<IRVoidType>(new IRVoidType());
        return voidType.get();
    }

    IRType* IRContext::getBoolTy() {
        if (!boolType) boolType = std::unique_ptr<IRIntegerType>(new IRIntegerType(1));
        return boolType.get();
    }

    IRType* IRContext::getCharTy() {
        if (!charType) charType = std::unique_ptr<IRIntegerType>(new IRIntegerType(8));
        return charType.get();
    }

    IRType* IRContext::getInt32Ty() {
        if (!int32Type) int32Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(32));
        return int32Type.get();
    }

    IRType* IRContext::getInt64Ty() {
        if (!int64Type) int64Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(64));
        return int64Type.get();
    }

    IRType* IRContext::getIntNTy(unsigned bitWidth) {
        auto it = integerTypes.find(bitWidth);
        if (it == integerTypes.end()) {
            it = integerTypes.emplace(bitWidth, std::unique_ptr<IRIntegerType>(new IRIntegerType(bitWidth))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getUInt32Ty() {
        if (!uint32Type) uint32Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(32, false));
        return uint32Type.get();
    }

    IRType* IRContext::getUInt64Ty() {
        if (!uint64Type) uint64Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(64, false));
        return uint64Type.get();
    }

    IRType* IRContext::getUIntNTy(unsigned bitWidth) {
        auto it = uintegerTypes.find(bitWidth);
        if (it == uintegerTypes.end()) {
            it = uintegerTypes.emplace(bitWidth, std::unique_ptr<IRIntegerType>(new IRIntegerType(bitWidth, false))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getFloat32Ty() {
        if (!float32Type) float32Type = std::unique_ptr<IRFloatType>(new IRFloatType(32));
        return float32Type.get();
    }

    IRType* IRContext::getFloat64Ty() {
        if (!float64Type) float64Type = std::unique_ptr<IRFloatType>(new IRFloatType(64));
        return float64Type.get();
    }

    IRType* IRContext::getTypeInfoTy() {
        if (!typeInfoType) typeInfoType = std::unique_ptr<IRTypeInfoType>(new IRTypeInfoType());
        return typeInfoType.get();
    }

    IRType* IRContext::getStringTy() {
        if (!stringType) stringType = std::unique_ptr<IRStringType>(new IRStringType());
        return stringType.get();
    }

    IRType* IRContext::getPointerTo(IRType* elementType) {
        auto it = pointerTypes.find(elementType);
        if (it == pointerTypes.end()) {
            it = pointerTypes.emplace(elementType, std::unique_ptr<IRPointerType>(new IRPointerType(elementType))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getRefTo(IRType* elementType) {
        auto it = refTypes.find(elementType);
        if (it == refTypes.end()) {
            it = refTypes.emplace(elementType, std::unique_ptr<IRRefType>(new IRRefType(elementType))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getArrayTy(IRType* elementType, uint64_t numElements) {
        const auto key = std::make_pair(elementType, numElements);
        auto it = arrayTypes.find(key);
        if (it == arrayTypes.end()) {
            it = arrayTypes.emplace(key, std::unique_ptr<IRArrayType>(new IRArrayType(elementType, numElements))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getBlockTy() {
        if (!blockType) blockType = std::unique_ptr<IRBlockType>(new IRBlockType());
        return blockType.get();
    }

    IRType* IRContext::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        const auto key = std::make_pair(returnType, params);
        auto it = functionTypes.find(key);
        if (it == functionTypes.end()) {
            it = functionTypes.emplace(key, std::unique_ptr<IRFunctionType>(new IRFunctionType(returnType, std::move(params)))).first;
        }
        return it->second.get();
    }
}
