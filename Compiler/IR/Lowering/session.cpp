#include "session.hpp"

#include "Compiler/IR/Backend/context/context.hpp"

namespace sakuraE::IR {
    IRType* LoweringSession::loweringBasicType(const TypeInfo* tyInfo) {
        if (!tyInfo) {
            throw std::runtime_error("cannot give a null typeinfo to lowering");
        }

        switch (tyInfo->getTypeID()) {
            case TypeID::Int32: {
                return irContext.getInt32Ty();
                break;
            }
            case TypeID::Int64: {
                return irContext.getInt64Ty();
                break;
            }
            case TypeID::UInt32: {
                return irContext.getUInt32Ty();
                break;
            }
            case TypeID::UInt64: {
                return irContext.getUInt64Ty();
                break;
            }
            case TypeID::Float32: {
                return irContext.getFloat32Ty();
                break;
            }
            case TypeID::Float64: {
                return irContext.getFloat64Ty();
                break;
            }
            case TypeID::Bool: {
                return irContext.getBoolTy();
                break;
            }
            case TypeID::Char: {
                return irContext.getCharTy();
                break;
            }
            case TypeID::Void: {
                return irContext.getVoidTy();
                break;
            }
            case TypeID::String: {
                return irContext.getPointerTo(irContext.getCharTy());
                break;
            }
            default: throw std::runtime_error("method: 'loweringBasicType' only receive basic type info");
        }
    }

    IRType *LoweringSession::lowerType(const TypeInfo *tyInfo) {
        IRType* result = nullptr;

        if (tyInfo->isArray()) {
            const auto& arrayType = std::get<ArrayTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            const auto& irArrayType = loweringBasicType(arrayType);
            result = irContext.getArrayTy(irArrayType, std::get<ArrayTypeInfo>(tyInfo->getComplexTypeInfo()).length());
        }

        else if (tyInfo->isPointer()) {
            const auto& pointerType = std::get<PointerTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            const auto& irPointeeType = loweringBasicType(pointerType);
            result = irContext.getPointerTo(irPointeeType);
        }

        else if (tyInfo->isRef()) {
            const auto& refType = std::get<RefTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            const auto& irRefType = loweringBasicType(refType);
            result = irContext.getRefTo(irRefType);
        }

        else if (tyInfo->isStruct()) {
            const auto& structDecl = std::get<StructTypeInfo>(tyInfo->getComplexTypeInfo()).getDeclId();

            if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
            if (const auto it = structMap.find(structDecl); it != structMap.end()) return it->second;

            const auto* declaration = structResolver->resolveStruct(structDecl);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");

            auto* lowered = irContext.createStructType(structDecl.module, declaration->getName());
            structMap.emplace(structDecl, lowered);
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
            result = lowered;
        }
        else {
            result = loweringBasicType(tyInfo);
        }
        typeMap.emplace(tyInfo, result);

        return result;
    }
}
