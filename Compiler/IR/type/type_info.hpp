#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

#include <map>

#include "type.hpp"
#include "Compiler/Error/error.hpp"

namespace sakuraE::IR {
    enum TypeID {
        // Token
        Int32,
        Float,
        Bool,
        Char,
        String,
        Custom,
        // Structure
        Array
    };

    inline IRType* tid2IRType(TypeID tid) {
        switch (tid)
        {
        case TypeID::Int32:
            return IRType::getInt32Ty();
        case TypeID::Float:
            return IRType::getFloatTy();
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

    class TypeInfo {
        TypeID typeID;
        
        bool isArrayType = false;
        std::vector<TypeInfo*> elementTypes;
    public:
        TypeInfo(TypeID tid): typeID(tid) {}

        TypeInfo(std::vector<TypeInfo*> tids): 
            typeID(Array), isArrayType(true), elementTypes(tids) {}
        
        ~TypeInfo()=default;

        bool isArray() {
            return isArrayType;
        }

        const TypeID& getTypeID() {
            return typeID;
        }

        const std::vector<TypeInfo*>& getTypeArray() {
            if (isArray())
                return elementTypes;
            else
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Expected to get a non-array-type TypeInfo as a array-type",
                                {0, 0, "InsideProblem"});
        }

        IRType* toIRType() {
            if (isArrayType) {
                IRType* elemType = nullptr;

                if (elementTypes[0]->isArray()) {
                    elemType = elementTypes[0]->toIRType();
                } else {
                    elemType = tid2IRType(elementTypes[0]->typeID);
                }
        
                return IRType::getArrayTy(elemType, elementTypes.size());
            }

            return tid2IRType(typeID);
        }

        static TypeInfo* makeTypeID(TypeID typeID);
        static TypeInfo* makeTypeID(std::vector<TypeInfo*> tArray);
    };
}

#endif // !SAKURAE_TYPE_INFO_HPP