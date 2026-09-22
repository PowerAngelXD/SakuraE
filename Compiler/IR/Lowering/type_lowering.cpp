#include "type_lowering.hpp"

#include "../Backend/context/context.hpp"
#include "../Semantic/model/struct.hpp"
#include <stdexcept>

namespace sakuraE::IR {
    void LoweringSession::prepareStructs() {
        if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
        // The registry is intentionally opaque; declarations are materialized lazily.
    }

    IRStructType* LoweringSession::lowerStruct(const StructDeclId& id) {
        if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
        if (const auto it = structMap.find(id); it != structMap.end()) return it->second;

        const auto* declaration = structResolver->resolveStruct(id);
        if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");

        auto* lowered = irContext.createStructType(id.module, declaration->getName());
        structMap.emplace(id, lowered);
        if (!declaration->isComplete()) return lowered;

        std::vector<IRStructType::FieldInfo> fields;
        fields.reserve(declaration->getFields().size());
        for (const auto& field : declaration->getFields()) {
            IRStructType::FieldInfo loweredField;
            loweredField.name = field.name;
            loweredField.type = lowerType(field.type);
            loweredField.info = field.info;
            fields.push_back(std::move(loweredField));
        }
        lowered->complete(std::move(fields));
        return lowered;
    }

    IRType* LoweringSession::lowerType(const TypeInfo* type) {
        if (!type) throw std::invalid_argument("Cannot lower a null TypeInfo");
        if (const auto it = typeMap.find(type); it != typeMap.end()) return it->second;

        const TypeInfo* base = type->getBase();
        IRType* lowered = nullptr;
        if (base->isArray()) {
            const auto& array = std::get<ArrayTypeInfo>(base->getComplexTypeInfo());
            lowered = irContext.getArrayTy(lowerType(array.getElementTy()), array.length());
        } else if (base->isPointer()) {
            const auto& pointer = std::get<PointerTypeInfo>(base->getComplexTypeInfo());
            lowered = irContext.getPointerTo(lowerType(pointer.getElementTy()));
        } else if (base->isRef()) {
            const auto& ref = std::get<RefTypeInfo>(base->getComplexTypeInfo());
            lowered = irContext.getRefTo(lowerType(ref.getElementTy()));
        } else if (base->isFunction()) {
            const auto& fn = std::get<FunctionTypeInfo>(base->getComplexTypeInfo());
            std::vector<IRType*> params;
            params.reserve(fn.getArgCount());
            for (const auto* param : fn.getArgTypes()) params.push_back(lowerType(param));
            lowered = irContext.getFunctionTy(lowerType(fn.getReturnType()), std::move(params));
        } else if (base->isStruct()) {
            lowered = lowerStruct(*base->getStructDeclId());
        } else {
            switch (base->getTypeID()) {
                case TypeID::Int32: lowered = irContext.getInt32Ty(); break;
                case TypeID::Int64: lowered = irContext.getInt64Ty(); break;
                case TypeID::UInt32: lowered = irContext.getUInt32Ty(); break;
                case TypeID::UInt64: lowered = irContext.getUInt64Ty(); break;
                case TypeID::Float32: lowered = irContext.getFloat32Ty(); break;
                case TypeID::Float64: lowered = irContext.getFloat64Ty(); break;
                case TypeID::Bool: lowered = irContext.getBoolTy(); break;
                case TypeID::Char: lowered = irContext.getCharTy(); break;
                case TypeID::Void: lowered = irContext.getVoidTy(); break;
                default: throw std::invalid_argument("Unsupported TypeInfo in lowering");
            }
        }
        typeMap.emplace(type, lowered);
        return lowered;
    }

    IRType* TypeLowering::lower(const TypeInfo* type) {
        return session.lowerType(type);
    }
}
