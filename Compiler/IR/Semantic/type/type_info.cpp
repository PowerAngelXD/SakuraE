#include "type_info.hpp"

#include "Compiler/IR/Backend/context/context.hpp"

namespace sakuraE::IR {
    // 返回所属 IRContext 的驻留池；定义放在 .cpp 是因为需要 IRContext 的完整定义。
    TypeInfoContext& TypeInfo::getTypeContext() const {
        return context.typeInfoPool();
    }

    // 递归生成类型名。先处理 nullable 包装（T? 与 T 共享 TypeID/组件，必须最先判定，否则会被当作 T 打印），
    // 再处理各复合类型，最后按 TypeID 打印基础类型。
    fzlib::String TypeInfo::toString() const {
        const auto* type = this;
        if (type->isNullable()) return type->getBase()->toString() + "?";
        if (type->isPointer()) return type->getPointeeType()->toString() + "*";
        if (type->isRef()) return type->getPointeeType()->toString() + "&";
        if (type->isArray()) {
            const auto& array = std::get<ArrayTypeInfo>(type->getComplexTypeInfo());
            return array.getElementTy()->toString() + "[" + std::to_string(array.length()) + "]";
        }
        // 结构体只有声明 ID 可用，打印为 "<module>.struct#<localId>"，不打印源码名称。
        if (type->isStruct()) {
            const auto& id = *type->getStructDeclId();
            return id.module + ".struct#" + std::to_string(id.localId);
        }
        if (const auto* signature = type->getFunctionTypeInfo()) {
            fzlib::String result = "func(";
            for (std::size_t i = 0; i < signature->getArgCount(); ++i) {
                if (i) result += ", ";
                result += signature->getArgTypes()[i]->toString();
            }
            return result + ") -> " + signature->getReturnType()->toString();
        }
        switch (type->getTypeID()) {
            case TypeID::Int32: return "i32";
            case TypeID::Int64: return "i64";
            case TypeID::UInt32: return "ui32";
            case TypeID::UInt64: return "ui64";
            case TypeID::Float32: return "f32";
            case TypeID::Float64: return "f64";
            case TypeID::Bool: return "bool";
            case TypeID::Char: return "char";
            case TypeID::String: return "string";
            case TypeID::Void: return "void";
            case TypeID::TypeDescriptor: return "typeinfo";
            // 复合 TypeID 已在上面处理，到这里说明出现了未知 TypeID。
            default: throw std::invalid_argument("Unsupported semantic type");
        }
    }

    // 见头文件说明。由于 TypeInfo 已驻留，同一 context 内结构相同的类型指针相等，source == target 即类型相等。
    bool isAssignableTo(const TypeInfo* source, const TypeInfo* target) {
        if (!source || !target) return false;
        if (source == target) return true;

        // 唯一的隐式转换：非 nullable 的 T -> T?（getBase 去掉 nullable 包装后比较）。
        return source->getBase() == target->getBase()
            && !source->isNullable()
            && target->isNullable();
    }

    // 基础类型工厂。只接受无组件的 TypeID，复合 TypeID 必须通过对应工厂创建，否则抛 std::invalid_argument。
    TypeInfo* TypeInfoContext::makeBasicTypeID(TypeID typeID) {
        switch (typeID) {
            case Int32: case Int64: case UInt32: case UInt64:
            case Float32: case Float64: case Bool: case Char:
            case Void: case String: case TypeDescriptor:
                break;
            default:
                throw std::invalid_argument("Composite TypeInfo requires its component types or declaration ID");
        }

        // 首次请求时创建并驻留，之后返回同一对象。
        auto it = primaryTypes.find(typeID);
        if (it == primaryTypes.end()) {
            it = primaryTypes.emplace(
                typeID,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, typeID))).first;
        }
        return it->second.get();
    }

    // 以下复合类型工厂均遵循同一模式：先校验组件（非空、同 context），再按组件身份查表，未命中则创建。
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

    // 结构体类型工厂：仅按 StructDeclId 驻留，无组件可校验；声明是否存在由 Program 语义验证负责。
    TypeInfo* TypeInfoContext::makeStructTypeID(StructDeclId id) {
        auto it = structTypes.find(id);
        if (it == structTypes.end()) {
            it = structTypes.emplace(
                id,
                std::unique_ptr<TypeInfo>(new TypeInfo(context, id))).first;
        }
        return it->second.get();
    }

    // 函数类型工厂：返回类型与所有参数类型逐一校验；键包含完整参数列表，签名相同即复用同一 TypeInfo。
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

    // nullable 包装工厂。拒绝：空类型、已是 nullable 的类型（不支持 T??）、
    // 非结构体/数组类型（语言规定只有结构体与数组可以为 null，与 Constant::getNullHandle 一致）。
    // 这些属于用户源码错误，因此抛 SakuraError 而不是 std::invalid_argument。
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

        // 最后再校验 context 归属；nullable 包装与 base 共享 context。
        validateComponent(type);
        auto it = nullableTypes.find(type);
        if (it == nullableTypes.end()) {
            it = nullableTypes.emplace(
                type,
                std::unique_ptr<TypeInfo>(new TypeInfo(type, TypeQualifier::Nullable))).first;
        }
        return it->second.get();
    }

    // 清空全部驻留表。函数/引用/指针/数组/nullable 表中的 TypeInfo 只以非拥有指针引用组件，
    // 析构时不会访问组件，因此清空顺序不影响正确性。
    void TypeInfoContext::clear() {
        funcTypes.clear();
        refTypes.clear();
        pointerTypes.clear();
        arrayTypes.clear();
        structTypes.clear();
        primaryTypes.clear();
        nullableTypes.clear();
    }

    // 组件校验：拒绝空组件与来自其他 IRContext 的组件，防止驻留表混入跨 context 的 TypeInfo。
    void TypeInfoContext::validateComponent(const TypeInfo* type) const {
        if (!type || &type->getContext() != &context) {
            throw std::invalid_argument("TypeInfo component must belong to this IRContext");
        }
    }

    // TypeInfo 的静态工厂：全部转发到 IRContext::current() 的 TypeInfoContext。
    // 调用前必须确保目标 context 是当前线程的活动 context。
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
