#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <variant>

#include "Compiler/Error/error.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "type.hpp"

namespace sakuraE::IR {
    class IRContext;
    class TypeInfo;

    enum TypeID {
        // 词法单元
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float32,
        Float64,
        Bool,
        Char,
        Void,
        String,
        Null,
        // 结构
        Array,
        Pointer,
        Ref,
        Struct
    };

    enum class TypeQualifier {
        Normal,
        Nullable
    };

    bool isAssignableTo(const TypeInfo* source, const TypeInfo* target);

    class ArrayTypeInfo {
        TypeInfo* elementType;
        std::uint64_t elementCount;

    public:
        ArrayTypeInfo(TypeInfo* element, std::uint64_t count):
            elementType(element), elementCount(count) {}

        std::uint64_t length() const { return elementCount; }
        TypeInfo* getElementTy() const { return elementType; }
    };

    class PointerTypeInfo {
        TypeInfo* elementType;

    public:
        explicit PointerTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() const { return elementType; }
    };

    class RefTypeInfo {
        TypeInfo* elementType;

    public:
        explicit RefTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() const { return elementType; }
    };

    class StructTypeInfo {
        IRStructType* type;

    public:
        explicit StructTypeInfo(IRStructType* structType): type(structType) {}

        IRStructType* getType() const { return type; }
    };

    class TypeInfo {
        friend class TypeInfoPool;

        IRContext& context;
        const TypeQualifier qualifier;
        TypeID typeID;
        std::variant<
            std::monostate,
            ArrayTypeInfo,
            PointerTypeInfo,
            RefTypeInfo,
            StructTypeInfo
        > complexTypeInfo;

        TypeInfo* base = nullptr;

        TypeInfo(IRContext& ctx, TypeID tid): context(ctx), qualifier(TypeQualifier::Normal), typeID(tid) {}

        TypeInfo(IRContext& ctx, TypeInfo* element, std::uint64_t count):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(Array),
            complexTypeInfo(ArrayTypeInfo(element, count)) {}

        TypeInfo(IRContext& ctx, TypeID id, TypeInfo* elementType):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(id),
            complexTypeInfo([&]() -> std::variant<std::monostate, ArrayTypeInfo, PointerTypeInfo, RefTypeInfo, StructTypeInfo> {
                switch (id) {
                    case Pointer: return PointerTypeInfo(elementType);
                    case Ref: return RefTypeInfo(elementType);
                    default:
                        throw std::runtime_error(
                            "Cannot use this TypeID to create a single-element TypeInfo");
                }
            }()) {}

        TypeInfo(IRContext& ctx, IRStructType* structType)
            : context(ctx), qualifier(TypeQualifier::Normal), typeID(Struct), complexTypeInfo(StructTypeInfo(structType)) {}

        TypeInfo(TypeInfo* b, TypeQualifier qk)
            : context(b->context), qualifier(qk), typeID(b->typeID),
              complexTypeInfo(b->complexTypeInfo), base(b) {
            if (qk != TypeQualifier::Nullable) {
                throw std::invalid_argument("Only nullable TypeInfo wrappers are supported");
            }
        }
    public:
        ~TypeInfo() = default;

        bool isArray() const { return typeID == Array; }
        bool isPointer() const { return typeID == Pointer; }
        bool isRef() const { return typeID == Ref; }
        bool isStruct() const { return typeID == Struct; }
        bool isBasic() const {
            return !isArray() && !isPointer() && !isRef() && !isStruct();
        }
        bool isNullable() const {
            return qualifier == TypeQualifier::Nullable;
        }
        const TypeID& getTypeID() const { return typeID; }

        TypeInfo* getBase() {
            return isNullable() ? base : this;
        }

        const TypeInfo* getBase() const {
            return isNullable() ? base : this;
        }

        TypeInfo* getElementType() const {
            if (!isArray()) return nullptr;
            return std::get<ArrayTypeInfo>(complexTypeInfo).getElementTy();
        }

        TypeInfo* getPointeeType() const {
            if (isPointer()) {
                return std::get<PointerTypeInfo>(complexTypeInfo).getElementTy();
            }
            if (isRef()) {
                return std::get<RefTypeInfo>(complexTypeInfo).getElementTy();
            }
            return nullptr;
        }

        IRStructType* getStructType() const {
            if (!isStruct()) return nullptr;
            return std::get<StructTypeInfo>(complexTypeInfo).getType();
        }

        IRType* toIRType() const;

        static TypeInfo* makeBasicTypeID(TypeID typeID);
        static TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        static TypeInfo* makePointerTypeID(TypeInfo* typeID);
        static TypeInfo* makeRefTypeID(TypeInfo* typeID);
        static TypeInfo* makeStructTypeID(IRStructType* type);
        static TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        static void clearAll();
    };

    class TypeInfoPool {
        IRContext& context;
        std::map<TypeID, std::unique_ptr<TypeInfo>> primaryTypes;
        std::map<std::pair<TypeInfo*, std::uint64_t>, std::unique_ptr<TypeInfo>> arrayTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> pointerTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> refTypes;
        std::map<IRStructType*, std::unique_ptr<TypeInfo>> structTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> nullableTypes;

    public:
        explicit TypeInfoPool(IRContext& ctx): context(ctx) {}

        TypeInfoPool(const TypeInfoPool&) = delete;
        TypeInfoPool& operator=(const TypeInfoPool&) = delete;

        TypeInfo* makeBasicTypeID(TypeID typeID);
        TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        TypeInfo* makePointerTypeID(TypeInfo* typeID);
        TypeInfo* makeRefTypeID(TypeInfo* typeID);
        TypeInfo* makeStructTypeID(IRStructType* type);
        TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        void clear();
    };
}

#endif
