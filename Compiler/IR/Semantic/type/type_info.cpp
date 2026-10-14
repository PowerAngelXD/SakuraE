#include "type_info.hpp"

#include "Compiler/IR/Backend/context/context.hpp"

namespace sakuraE::IR {
    bool isAssignableTo(const TypeInfo* source, const TypeInfo* target) {
        if (!source || !target) return false;
        if (source == target) return true;

        return source->getBase() == target->getBase()
            && !source->isNullable()
            && target->isNullable();
    }

    TypeInfo* TypeInfoContext::makeBasicTypeID(TypeID typeID) {
        switch (typeID) {
            case Int32: case Int64: case UInt32: case UInt64:
            case Float32: case Float64: case Bool: case Char:
            case Void: case String: case TypeDescriptor:
                break;
            default:
                throw std::invalid_argument("Composite TypeInfo requires its component types or declaration ID");
        }

        auto it = primaryTypes.find(typeID);
        if (it == primaryTypes.end()) {
            it = primaryTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoContext::makeArrayTypeID(TypeInfo* element, std::uint64_t count) {
        validateComponent(element);
        const auto key = std::make_pair(element, count);
        auto it = arrayTypes.find(key);
        if (it == arrayTypes.end()) {
            it = arrayTypes.emplace(
                key,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, element, count))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoContext::makePointerTypeID(TypeInfo* typeID) {
        validateComponent(typeID);
        auto it = pointerTypes.find(typeID);
        if (it == pointerTypes.end()) {
            it = pointerTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, Pointer, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoContext::makeRefTypeID(TypeInfo* typeID) {
        validateComponent(typeID);
        auto it = refTypes.find(typeID);
        if (it == refTypes.end()) {
            it = refTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, Ref, typeID))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoContext::makeStructTypeID(StructDeclId id) {
        auto it = structTypes.find(id);
        if (it == structTypes.end()) {
            it = structTypes.emplace(
                id,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, id))).first;
        }
        return it->second.get();
    }

    TypeInfo *TypeInfoContext::makeFunctionTypeID(TypeInfo *retTy, std::vector<TypeInfo *> argTys) {
        validateComponent(retTy);
        for (const auto* argument : argTys) validateComponent(argument);
        const auto key = std::make_pair(retTy, argTys);
        auto it = funcTypes.find(key);
        if (it == funcTypes.end()) {
            it = funcTypes.emplace(
                key,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, retTy, argTys))).first;
        }
        return it->second.get();
    }

    TypeInfo* TypeInfoContext::wrapTypeAsNullable(TypeInfo* type, PositionInfo info) {
        if (!type) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Cannot wrap a Null Type!",
                info
            );
        }
        else if (type->isNullable()) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Cannot wrap a 'T?' as 'T?\\?'!",
                info
            );
        }
        else if (!type->isStruct() && !type->isArray()) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Only struct and array types can be nullable.",
                info
            );
        }

        validateComponent(type);
        auto it = nullableTypes.find(type);
        if (it == nullableTypes.end()) {
            it = nullableTypes.emplace(
                type,
                std::unique_ptr<TypeInfo>(new TypeInfo(type, TypeQualifier::Nullable))).first;
        }
        return it->second.get();
    }

    void TypeInfoContext::clear() {
        funcTypes.clear();
        refTypes.clear();
        pointerTypes.clear();
        arrayTypes.clear();
        structTypes.clear();
        primaryTypes.clear();
        nullableTypes.clear();
    }

    void TypeInfoContext::validateComponent(const TypeInfo* type) const {
        if (!type || &type->getContext() != &context) {
            throw std::invalid_argument("TypeInfo component must belong to this IRContext");
        }
    }

    TypeInfo* TypeInfo::makeBasicTypeID(const TypeID typeID) {
        return IRContext::current().getTypeInfoManager().makeBasicTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeArrayTypeID(TypeInfo* element, const std::uint64_t count) {
        return IRContext::current().getTypeInfoManager().makeArrayTypeID(element, count);
    }

    TypeInfo* TypeInfo::makePointerTypeID(TypeInfo* typeID) {
        return IRContext::current().getTypeInfoManager().makePointerTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeRefTypeID(TypeInfo* typeID) {
        return IRContext::current().getTypeInfoManager().makeRefTypeID(typeID);
    }

    TypeInfo* TypeInfo::makeStructTypeID(StructDeclId id) {
        return IRContext::current().getTypeInfoManager().makeStructTypeID(std::move(id));
    }

    TypeInfo* TypeInfo::makeFunctionTypeID(TypeInfo* retTy, std::vector<TypeInfo*> argTys) {
        return IRContext::current().getTypeInfoManager().makeFunctionTypeID(retTy, std::move(argTys));
    }

    TypeInfo* TypeInfo::wrapTypeAsNullable(TypeInfo* type, PositionInfo info) {
        return IRContext::current().getTypeInfoManager().wrapTypeAsNullable(type, std::move(info));
    }

    void TypeInfo::clearAll() {
        IRContext::current().getTypeInfoManager().clear();
    }
}
