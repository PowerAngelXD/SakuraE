#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <variant>
#include <cstdint>

#include "Compiler/Error/error.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "Compiler/IR/Backend/type/type.hpp"

namespace sakuraE::IR {
    class IRContext;
    class TypeInfo;

    struct StructDeclId {
        fzlib::String module;
        std::uint64_t localId = 0;

        bool operator==(const StructDeclId& other) const {
            return module == other.module && localId == other.localId;
        }
        bool operator<(const StructDeclId& other) const {
            return module < other.module || (module == other.module && localId < other.localId);
        }
    };

    struct SemanticField {
        fzlib::String name;
        TypeInfo* type = nullptr;
        PositionInfo info;
    };

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
        Struct,
        FunctionType
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
        StructDeclId declaration;

    public:
        explicit StructTypeInfo(StructDeclId id): declaration(std::move(id)) {}

        const StructDeclId& getDeclId() const { return declaration; }
    };

    class FunctionTypeInfo {
        TypeInfo* returnType;
        std::vector<TypeInfo*> argTypes;

    public:
        explicit FunctionTypeInfo(TypeInfo* retTy, std::vector<TypeInfo*> argTys): returnType(retTy), argTypes(std::move(argTys)) {}

        TypeInfo* getReturnType() const { return returnType; }
        std::vector<TypeInfo*>  getArgTypes() const { return argTypes; }
        std::size_t getArgCount() const { return argTypes.size(); }
    };

    using ComplexTypeUnion = std::variant<std::monostate, ArrayTypeInfo, PointerTypeInfo, RefTypeInfo, StructTypeInfo, FunctionTypeInfo>;
    class TypeInfo {
        friend class TypeInfoContext;

        IRContext& context;
        const TypeQualifier qualifier;
        TypeID typeID;
        ComplexTypeUnion complexTypeInfo;

        TypeInfo* base = nullptr;

        TypeInfo(IRContext& ctx, const TypeID tid): context(ctx), qualifier(TypeQualifier::Normal), typeID(tid) {}

        TypeInfo(IRContext& ctx, TypeInfo* element, std::uint64_t count):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(Array),
            complexTypeInfo(ArrayTypeInfo(element, count)) {}

        TypeInfo(IRContext& ctx, TypeID id, TypeInfo* elementType):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(id),
            complexTypeInfo([&]() -> ComplexTypeUnion {
                switch (id) {
                    case Pointer: return PointerTypeInfo(elementType);
                    case Ref: return RefTypeInfo(elementType);
                    default:
                        throw std::runtime_error(
                            "Cannot use this TypeID to create a single-element TypeInfo");
                }
            }()) {}

        TypeInfo(IRContext& ctx, TypeInfo* retTy, std::vector<TypeInfo*> argTys):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(FunctionType),
            complexTypeInfo(FunctionTypeInfo(retTy, argTys)) {}

        TypeInfo(IRContext& ctx, StructDeclId id)
            : context(ctx), qualifier(TypeQualifier::Normal), typeID(Struct), complexTypeInfo(StructTypeInfo(std::move(id))) {}

        TypeInfo(TypeInfo* b, const TypeQualifier qk)
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
        bool isFunction() const { return typeID == FunctionType; }
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

        TypeInfo* getFunctionType() const {
            if (!isFunction()) return nullptr;
            return std::get<FunctionTypeInfo>(complexTypeInfo).getReturnType();
        }

        const ComplexTypeUnion& getComplexTypeInfo() const { return complexTypeInfo; }

        const StructDeclId* getStructDeclId() const {
            if (!isStruct()) return nullptr;
            return &std::get<StructTypeInfo>(complexTypeInfo).getDeclId();
        }

        IRType* toIRType() const;

        static TypeInfo* makeBasicTypeID(const TypeID typeID);
        static TypeInfo* makeArrayTypeID(TypeInfo* element, const std::uint64_t count);
        static TypeInfo* makePointerTypeID(TypeInfo* typeID);
        static TypeInfo* makeRefTypeID(TypeInfo* typeID);
        static TypeInfo* makeStructTypeID(StructDeclId id);
        static TypeInfo* makeFunctionTypeID(TypeInfo* retTy, std::vector<TypeInfo*> argTys);
        static TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        static void clearAll();
    };

    class TypeInfoContext {
        IRContext& context;
        std::map<TypeID, std::unique_ptr<TypeInfo>> primaryTypes;
        std::map<std::pair<TypeInfo*, std::uint64_t>, std::unique_ptr<TypeInfo>> arrayTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> pointerTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> refTypes;
        std::map<StructDeclId, std::unique_ptr<TypeInfo>> structTypes;
        std::map<std::pair<TypeInfo*, std::vector<TypeInfo*>>, std::unique_ptr<TypeInfo>> funcTypes;
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> nullableTypes;

    public:
        explicit TypeInfoContext(IRContext& ctx): context(ctx) {}

        TypeInfoContext(const TypeInfoContext&) = delete;
        TypeInfoContext& operator=(const TypeInfoContext&) = delete;

        TypeInfo* makeBasicTypeID(TypeID typeID);
        TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        TypeInfo* makePointerTypeID(TypeInfo* typeID);
        TypeInfo* makeRefTypeID(TypeInfo* typeID);
        TypeInfo* makeStructTypeID(StructDeclId id);
        TypeInfo* makeFunctionTypeID(TypeInfo* retTy, std::vector<TypeInfo*> argTys);
        TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        void clear();
    };
}

#endif
