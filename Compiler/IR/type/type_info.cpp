#include "type_info.hpp"

#include "Compiler/IR/context.hpp"

namespace sakuraE::IR {
    namespace {
        IRType* tid2IRType(IRContext& context, TypeID typeID) {
            switch (typeID) {
                case TypeID::Int32:
                    return context.getInt32Ty();
                case TypeID::Int64:
                    return context.getInt64Ty();
                case TypeID::UInt32:
                    return context.getUInt32Ty();
                case TypeID::UInt64:
                    return context.getUInt64Ty();
                case TypeID::Float32:
                    return context.getFloat32Ty();
                case TypeID::Float64:
                    return context.getFloat64Ty();
                case TypeID::Char:
                    return context.getCharTy();
                case TypeID::Bool:
                    return context.getBoolTy();
                case TypeID::String:
                    return context.getStringTy();
                case TypeID::Void:
                    return context.getVoidTy();
                default:
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                      "Unknown TypeID to convert to IRType",
                                      {0, 0, "InsideError"});
            }
        }
    }

    IRType* TypeInfo::toIRType() const {
        if (isArray()) {
            const auto& arrayType = std::get<ArrayTypeInfo>(complexTypeInfo);
            return context.getArrayTy(
                arrayType.getElementTy()->toIRType(),
                arrayType.length());
        }

        if (isPointer()) {
            const auto& pointerType = std::get<PointerTypeInfo>(complexTypeInfo);
            return context.getPointerTo(pointerType.getElementTy()->toIRType());
        }

        if (isRef()) {
            const auto& refType = std::get<RefTypeInfo>(complexTypeInfo);
            return context.getRefTo(refType.getElementTy()->toIRType());
        }

        if (isStruct()) {
            return std::get<StructTypeInfo>(complexTypeInfo).getType();
        }

        return tid2IRType(context, typeID);
    }

    TypeInfo* TypeInfoPool::makeBasicTypeID(TypeID typeID) {
        if (typeID == TypeID::Struct) {
            throw std::runtime_error("A struct TypeInfo requires an IRStructType");
        }

        auto it = primaryTypes.find(typeID);
        if (it == primaryTypes.end()) {
            it = primaryTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoPool::makeArrayTypeID(TypeInfo* element, std::uint64_t count) {
        const auto key = std::make_pair(element, count);
        auto it = arrayTypes.find(key);
        if (it == arrayTypes.end()) {
            it = arrayTypes.emplace(
                key,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, element, count))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoPool::makePointerTypeID(TypeInfo* typeID) {
        auto it = pointerTypes.find(typeID);
        if (it == pointerTypes.end()) {
            it = pointerTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, Pointer, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoPool::makeRefTypeID(TypeInfo* typeID) {
        auto it = refTypes.find(typeID);
        if (it == refTypes.end()) {
            it = refTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, Ref, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoPool::makeStructTypeID(IRStructType* type) {
        if (!type) {
            throw std::invalid_argument("Cannot create TypeInfo for a null IRStructType");
        }

        auto it = structTypes.find(type);
        if (it == structTypes.end()) {
            it = structTypes.emplace(
                type,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, type))).first;
        }
        return it->second.get();
    }

    void TypeInfoPool::clear() {
        refTypes.clear();
        pointerTypes.clear();
        arrayTypes.clear();
        structTypes.clear();
        primaryTypes.clear();
    }

    TypeInfo* TypeInfo::makeBasicTypeID(TypeID typeID) {
        return IRContext::current().typeInfoPool().makeBasicTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeArrayTypeID(TypeInfo* element, std::uint64_t count) {
        return IRContext::current().typeInfoPool().makeArrayTypeID(element, count);
    }

    TypeInfo* TypeInfo::makePointerTypeID(TypeInfo* typeID) {
        return IRContext::current().typeInfoPool().makePointerTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeRefTypeID(TypeInfo* typeID) {
        return IRContext::current().typeInfoPool().makeRefTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeStructTypeID(IRStructType* type) {
        return IRContext::current().typeInfoPool().makeStructTypeID(type);
    }

    void TypeInfo::clearAll() {
        IRContext::current().typeInfoPool().clear();
    }
}
