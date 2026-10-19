#include "session.hpp"

#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include <functional>

namespace sakuraE::IR {
    void LoweringSession::validateStruct(const StructDeclId& id) const {
        std::set<StructDeclId> visiting;
        std::set<StructDeclId> visited;
        std::function<void(const TypeInfo*)> visitType;
        std::function<void(const StructDeclId&)> visitStruct;

        visitType = [&](const TypeInfo* type) {
            if (!type) throw std::runtime_error("Struct field has no semantic type");
            if (type->isPointer() || type->isRef()) return;
            if (type->isArray()) {
                visitType(type->getElementType());
            }
            else if (type->isStruct()) {
                visitStruct(*type->getStructDeclId());
            }
            else if (type->isFunction() || type->getTypeID() == TypeID::Void) {
                throw std::runtime_error("Struct field requires a storage type");
            }
        };
        visitStruct = [&](const StructDeclId& declarationId) {
            if (visiting.contains(declarationId)) {
                throw std::runtime_error("Struct layout contains a by-value recursive cycle");
            }
            if (visited.contains(declarationId)) return;
            const auto* declaration = structResolver->resolveStruct(declarationId);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");
            if (!declaration->isComplete()) return;
            visiting.insert(declarationId);
            for (const auto& field : declaration->getFields()) visitType(field.type);
            visiting.erase(declarationId);
            visited.insert(declarationId);
        };
        visitStruct(id);
    }

    IRType* LoweringSession::loweringBasicType(const TypeInfo* tyInfo) const {
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
            case TypeID::TypeDescriptor: return irContext.getTypeInfoTy();
            default: throw std::runtime_error("method: 'loweringBasicType' only receive basic type info");
        }
    }

    IRType *LoweringSession::lowerType(const TypeInfo *tyInfo) {
        PendingLowering pending;
        auto* result = lowerTypeSafer(tyInfo, pending);
        // 本次转换未遇到未完成的结构体声明时，整张类型图已稳定，可以写入缓存；
        // 否则保留重新转换的机会，以便语义声明完成后在同一类型对象上补齐布局。
        if (!pending.opaqueStruct) {
            typeMap.insert(pending.results.begin(), pending.results.end());
        }
        return result;
    }

    IRType *LoweringSession::lowerTypeSafer(const TypeInfo *tyInfo, PendingLowering& pending) {
        if (!tyInfo) {
            throw std::runtime_error("cannot give a null typeinfo to lowering");
        }
        if (&tyInfo->getContext() != &irContext) {
            throw std::runtime_error("TypeInfo does not belong to this LoweringSession's IRContext");
        }
        if (const auto it = typeMap.find(tyInfo); it != typeMap.end()) return it->second;

        IRType* result = nullptr;

        if (tyInfo->isArray()) {
            const auto& arrayType = std::get<ArrayTypeInfo>(tyInfo->getComplexTypeInfo());
            result = irContext.getArrayTy(lowerTypeSafer(arrayType.getElementTy(), pending), arrayType.length());
        }
        else if (tyInfo->isPointer()) {
            const auto& pointeeType = std::get<PointerTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            result = irContext.getPointerTo(lowerTypeSafer(pointeeType, pending));
        }
        else if (tyInfo->isRef()) {
            const auto& refType = std::get<RefTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            result = irContext.getRefTo(lowerTypeSafer(refType, pending));
        }
        else if (tyInfo->isStruct()) {
            const auto& structDecl = std::get<StructTypeInfo>(tyInfo->getComplexTypeInfo()).getDeclId();

            if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
            const auto* declaration = structResolver->resolveStruct(structDecl);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");

            const auto* owner = structResolver->resolveNamingContext(structDecl);
            if (!owner) throw std::runtime_error("Semantic struct declaration has no NamingContext");
            auto* lowered = owner->getStructTy(irContext, structDecl);
            structMap.try_emplace(structDecl, lowered);
            if (!declaration->isComplete()) {
                pending.opaqueStruct = true;
                return lowered;
            }
            // 正在填充的结构体是递归回边，外层调用完成后其布局即完整
            if (fillingStructs.contains(structDecl)) return lowered;
            validateStruct(structDecl);

            fillingStructs.insert(structDecl);
            try {
                std::vector<IRStructType::FieldInfo> fields;
                fields.reserve(declaration->getFields().size());
                for (const auto& field : declaration->getFields()) {
                    IRStructType::FieldInfo loweredField;
                    loweredField.name = field.name;
                    loweredField.type = lowerTypeSafer(field.type, pending);
                    loweredField.info = field.info;
                    fields.push_back(std::move(loweredField));
                }
                if (!lowered->isComplete()) lowered->complete(std::move(fields));
            }
            catch (...) {
                fillingStructs.erase(structDecl);
                throw;
            }
            fillingStructs.erase(structDecl);
            result = lowered;
        }
        else if (tyInfo->isFunction()) {
            const auto& funcTy = std::get<FunctionTypeInfo>(tyInfo->getComplexTypeInfo());

            std::vector<IRType*> irArgs;
            for (const auto& arg : funcTy.getArgTypes()) {
                irArgs.push_back(lowerTypeSafer(arg, pending));
            }

            result = irContext.getFunctionTy(lowerTypeSafer(funcTy.getReturnType(), pending), irArgs);
        }
        else {
            result = loweringBasicType(tyInfo);
        }
        pending.results.emplace(tyInfo, result);

        return result;
    }
}
