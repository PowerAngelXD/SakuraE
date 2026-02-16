#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

#include <map>
#include <stdexcept>
#include <variant>

#include "Compiler/Utils/Logger.hpp"
#include "type.hpp"
#include "Compiler/Error/error.hpp"

namespace sakuraE::IR {
    enum TypeID {
        // Token
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float32,
        Float64,
        Bool,
        Char,
        String,
        Null,
        Custom,
        // Structure
        Array,
        Pointer,
        Ref
    };

    inline IRType* tid2IRType(TypeID tid) {
        switch (tid)
        {
        case TypeID::Int32:
            return IRType::getInt32Ty();
        case TypeID::Int64:
            return IRType::getInt64Ty();
        case TypeID::UInt32:
            return IRType::getUInt32Ty();
        case TypeID::UInt64:
            return IRType::getUInt64Ty();
        case TypeID::Float32:
            return IRType::getFloat32Ty();
        case TypeID::Float64:
            return IRType::getFloat64Ty();
        case TypeID::Char:
            return IRType::getCharTy();
        case TypeID::Bool:
            return IRType::getBoolTy();
        case TypeID::String:
            return IRType::getPointerTo(IRType::getCharTy());
        default:
            throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown type id to convert to IRType",
                                {0, 0, "InsideError"});
        }
    }

    class TypeInfo;

    class ArrayTypeInfo {
        std::vector<TypeInfo*> elementTypes;
    public:
        ArrayTypeInfo(std::vector<TypeInfo*> elements): elementTypes(elements) {}
        
        std::size_t length() { return elementTypes.size(); }
        TypeInfo* getElementTy() { return elementTypes[0]; }
    };

    class PointerTypeInfo {
        TypeInfo* elementType;
    public:
        PointerTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() { return elementType; }
    };

    class RefTypeInfo {
        TypeInfo* elementType;
    public:
        RefTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() { return elementType; }
    };

    class TypeInfo {
        TypeID typeID;
        
        std::variant<
            std::monostate,
            ArrayTypeInfo,
            PointerTypeInfo,
            RefTypeInfo
        > complexTypeInfo;
    public:
        TypeInfo(TypeID tid): typeID(tid) {}

        TypeInfo(std::vector<TypeInfo*> tids): 
            typeID(Array), complexTypeInfo(ArrayTypeInfo(tids)) {}
        
        TypeInfo(TypeID id, TypeInfo* elemntTid): 
            typeID(id), complexTypeInfo([&]() -> std::variant<std::monostate, ArrayTypeInfo, PointerTypeInfo, RefTypeInfo> {
                switch (id) {
                    case Pointer: return PointerTypeInfo(elemntTid);
                    case Ref: return RefTypeInfo(elemntTid);
                    default: throw std::runtime_error("Cannot use other typeid to create single element typeinfo");
                }
            }()) {}
        
        ~TypeInfo()=default;

        bool isArray() { return typeID == Array; }
        bool isPointer() { return typeID == Pointer; }
        bool isRef() { return typeID == Ref; }
        const TypeID& getTypeID() { return typeID; }

        IRType* toIRType() {
            if (isArray()) {
                auto arrTy = std::get<ArrayTypeInfo>(complexTypeInfo);
                return IRType::getArrayTy(arrTy.getElementTy()->toIRType(), arrTy.length());
            }
            else if (isPointer()) {
                auto ptrTy = std::get<PointerTypeInfo>(complexTypeInfo);
                return IRType::getPointerTo(ptrTy.getElementTy()->toIRType());
            }
            else if (isRef()) {
                auto refTy = std::get<RefTypeInfo>(complexTypeInfo);
                return IRType::getPointerTo(refTy.getElementTy()->toIRType());
            }
            else return tid2IRType(typeID);
        }

        static TypeInfo* makeTypeID(TypeID typeID);
        static TypeInfo* makeTypeID(std::vector<TypeInfo*> tArray);
        static void clearAll();
    };
}

#endif // !SAKURAE_TYPE_INFO_HPP