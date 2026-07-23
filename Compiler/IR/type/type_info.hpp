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
        TypeID typeID;
        std::variant<
            std::monostate,
            ArrayTypeInfo,
            PointerTypeInfo,
            RefTypeInfo,
            StructTypeInfo
        > complexTypeInfo;

        TypeInfo(IRContext& ctx, TypeID tid): context(ctx), typeID(tid) {}

        TypeInfo(IRContext& ctx, TypeInfo* element, std::uint64_t count):
            context(ctx), typeID(Array),
            complexTypeInfo(ArrayTypeInfo(element, count)) {}

        TypeInfo(IRContext& ctx, TypeID id, TypeInfo* elementType):
            context(ctx), typeID(id),
            complexTypeInfo([&]() -> std::variant<std::monostate, ArrayTypeInfo, PointerTypeInfo, RefTypeInfo, StructTypeInfo> {
                switch (id) {
                    case Pointer: return PointerTypeInfo(elementType);
                    case Ref: return RefTypeInfo(elementType);
                    default:
                        throw std::runtime_error(
                            "Cannot use this TypeID to create a single-element TypeInfo");
                }
            }()) {}

        TypeInfo(IRContext& ctx, IRStructType* structType):
            context(ctx), typeID(Struct), complexTypeInfo(StructTypeInfo(structType)) {}

    public:
        ~TypeInfo() = default;

        bool isArray() const { return typeID == Array; }
        bool isPointer() const { return typeID == Pointer; }
        bool isRef() const { return typeID == Ref; }
        bool isStruct() const { return typeID == Struct; }
        const TypeID& getTypeID() const { return typeID; }

        IRType* toIRType() const;

        static TypeInfo* makeBasicTypeID(TypeID typeID);
        static TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        static TypeInfo* makePointerTypeID(TypeInfo* typeID);
        static TypeInfo* makeRefTypeID(TypeInfo* typeID);
        static TypeInfo* makeStructTypeID(IRStructType* type);
        static void clearAll();
    };

    class TypeInfoPool {
        IRContext& context;
        std::map<TypeID, std::unique_ptr<TypeInfo>> primaryTypes;
        std::map<std::pair<TypeInfo*, std::uint64_t>, std::unique_ptr<TypeInfo>> arrayTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> pointerTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> refTypes;
        std::map<IRStructType*, std::unique_ptr<TypeInfo>> structTypes;

    public:
        explicit TypeInfoPool(IRContext& ctx): context(ctx) {}

        TypeInfoPool(const TypeInfoPool&) = delete;
        TypeInfoPool& operator=(const TypeInfoPool&) = delete;

        TypeInfo* makeBasicTypeID(TypeID typeID);
        TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        TypeInfo* makePointerTypeID(TypeInfo* typeID);
        TypeInfo* makeRefTypeID(TypeInfo* typeID);
        TypeInfo* makeStructTypeID(IRStructType* type);
        void clear();
    };
}

#endif
