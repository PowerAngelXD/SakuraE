#include "type_info.hpp"

namespace sakuraE::IR {
    static std::map<TypeID, TypeInfo*> primaryTypeIDPool;
    static std::map<std::vector<TypeInfo*>, TypeInfo*> arrayTypeIDPool;
    static std::map<TypeInfo*, TypeInfo*> pointerTypeIDPool;
    static std::map<TypeInfo*, TypeInfo*> refTypeIDPool;

    TypeInfo* TypeInfo::makeBasicTypeID(TypeID typeID) {
        auto it = primaryTypeIDPool.find(typeID);
        if (it != primaryTypeIDPool.end())
            return it->second;
        
        TypeInfo* info = new TypeInfo(typeID);
        primaryTypeIDPool.emplace(typeID, info);

        return info;
    }

    TypeInfo* TypeInfo::makeArrayTypeID(std::vector<TypeInfo*> tArray) {
        auto it = arrayTypeIDPool.find(tArray);
        if (it != arrayTypeIDPool.end())
            return it->second;
        
        TypeInfo* info = new TypeInfo(tArray);
        arrayTypeIDPool.emplace(tArray, info);

        return info;
    }

    TypeInfo* TypeInfo::makePointerTypeID(TypeInfo* typeID) {
        auto it = pointerTypeIDPool.find(typeID);
        if (it != pointerTypeIDPool.end())
            return it->second;
        
        TypeInfo* info = new TypeInfo(Pointer, typeID);
        pointerTypeIDPool.emplace(typeID, info);

        return info;
    }

    TypeInfo* TypeInfo::makeRefTypeID(TypeInfo* typeID) {
        auto it = refTypeIDPool.find(typeID);
        if (it != refTypeIDPool.end())
            return it->second;
        
        TypeInfo* info = new TypeInfo(Ref, typeID);
        refTypeIDPool.emplace(typeID, info);

        return info;
    }

    void TypeInfo::clearAll() {
        for (auto& pair : primaryTypeIDPool) delete pair.second;
        primaryTypeIDPool.clear();

        for (auto& pair : arrayTypeIDPool) delete pair.second;
        arrayTypeIDPool.clear();

        for (auto& pair : pointerTypeIDPool) delete pair.second;
        pointerTypeIDPool.clear();

        for (auto& pair : refTypeIDPool) delete pair.second;
        refTypeIDPool.clear();
    }
}
