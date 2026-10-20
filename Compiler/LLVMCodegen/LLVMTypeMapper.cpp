#include "LLVMTypeMapper.hpp"

#include "Compiler/IR/Backend/type/type.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <stdexcept>
#include <vector>

namespace sakuraE::Codegen {
    namespace {
        struct StructMapping {
            const IR::IRStructType* type;
            const StructMapping* parent;
        };

        llvm::Type* mapType(IR::IRType* type, llvm::LLVMContext& context,
                            const StructMapping* activeStruct) {
            if (!type) throw std::invalid_argument("Cannot map a null storage type");

            switch (type->getIRTypeID()) {
                case IR::VoidTyID:
                    return llvm::Type::getVoidTy(context);
                case IR::BoolTyID:
                case IR::CharTyID:
                case IR::Integer32TyID:
                case IR::Integer64TyID:
                case IR::IntegerNTyID:
                case IR::UInteger32TyID:
                case IR::UInteger64TyID:
                case IR::UIntegerNTyID: {
                    const auto width = static_cast<IR::IRIntegerType*>(type)->getBitWidth();
                    if (width < llvm::IntegerType::MIN_INT_BITS || width > llvm::IntegerType::MAX_INT_BITS) {
                        throw std::invalid_argument("Unsupported integer storage width");
                    }
                    return llvm::IntegerType::get(context, width);
                }
                case IR::Float32TyID:
                case IR::Float64TyID:
                case IR::FloatNTyID:
                    switch (static_cast<IR::IRFloatType*>(type)->getBitWidth()) {
                        case 32: return llvm::Type::getFloatTy(context);
                        case 64: return llvm::Type::getDoubleTy(context);
                        default: throw std::invalid_argument("Unsupported floating-point storage width");
                    }
                case IR::PointerTyID:
                case IR::RefTyID:
                case IR::TypeInfoTyID:
                    return llvm::PointerType::getUnqual(context);
                case IR::ArrayTyID: {
                    const auto* array = static_cast<IR::IRArrayType*>(type);
                    auto* element = mapType(array->getElementType(), context, activeStruct);
                    if (!llvm::ArrayType::isValidElementType(element)) {
                        throw std::invalid_argument("Invalid array element storage type");
                    }
                    return llvm::ArrayType::get(element, array->getNumElements());
                }
                case IR::BlockTyID:
                    return llvm::Type::getLabelTy(context);
                case IR::FunctionTyID: {
                    const auto* function = static_cast<IR::IRFunctionType*>(type);
                    auto* result = mapType(function->getReturnType(), context, activeStruct);
                    if (!llvm::FunctionType::isValidReturnType(result)) {
                        throw std::invalid_argument("Invalid function return storage type");
                    }
                    std::vector<llvm::Type*> params;
                    params.reserve(function->getParamTypes().size());
                    for (auto* param : function->getParamTypes()) {
                        auto* mapped = mapType(param, context, activeStruct);
                        if (!llvm::FunctionType::isValidArgumentType(mapped)) {
                            throw std::invalid_argument("Invalid function parameter storage type");
                        }
                        params.push_back(mapped);
                    }
                    return llvm::FunctionType::get(result, params, false);
                }
                case IR::StructTyID: {
                    const auto* structure = static_cast<IR::IRStructType*>(type);
                    const auto name = "sakurae.struct." + structure->getModuleID() + "." + structure->getName();
                    auto* mapped = llvm::StructType::getTypeByName(context, name.c_str());
                    if (!mapped) mapped = llvm::StructType::create(context, name.c_str());
                    if (!structure->isComplete()) return mapped;

                    for (auto* frame = activeStruct; frame; frame = frame->parent) {
                        if (frame->type == structure) {
                            throw std::invalid_argument("Recursive by-value struct storage type");
                        }
                    }
                    const StructMapping frame {structure, activeStruct};
                    const bool finishBody = mapped->isOpaque();
                    std::vector<llvm::Type*> fields;
                    if (finishBody) fields.reserve(structure->getFields().size());
                    for (const auto& field : structure->getFields()) {
                        auto* element = mapType(field.type, context, &frame);
                        if (!llvm::StructType::isValidElementType(element)) {
                            throw std::invalid_argument("Invalid struct field storage type");
                        }
                        if (finishBody) fields.push_back(element);
                    }
                    // Revisit fields even when the body already exists: a nested opaque
                    // structure may have been completed since the previous mapping.
                    if (finishBody) mapped->setBody(fields, false);
                    return mapped;
                }
                case IR::StringTyID:
                    break; // Strings use pointer-to-char storage, not a distinct IR type.
            }
            throw std::invalid_argument("Unsupported storage type");
        }
    }

    llvm::Type* map(IR::IRType* type, llvm::LLVMContext& context) {
        return mapType(type, context, nullptr);
    }
}
