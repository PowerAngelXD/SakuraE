#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

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

        static TypeInfo* makeTypeID(TypeID typeID);
        static TypeInfo* makeTypeID(std::vector<TypeInfo*> tArray);
    };
}

#endif // !SAKURAE_TYPE_INFO_HPP