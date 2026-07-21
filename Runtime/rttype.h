/*
    SakuraE Runtime Library
    rttype.h

    Immutable runtime type metadata used by RTTI and reflection.
*/

#ifndef SAKURAE_RUNTIME_RTTYPE_H
#define SAKURAE_RUNTIME_RTTYPE_H

#include <cstdint>

namespace sakuraE::runtime {
    enum class RuntimeTypeKind : std::uint8_t {
        Void,
        I8,
        I32,
        I64,
        U8,
        U32,
        U64,
        F32,
        F64,
        Bool,
        String,
        Pointer,
        Reference,
        Array,
        Struct,
        Function
    };

    struct TypeInfo {
        const RuntimeTypeKind kind;
        const char* const name;
        const TypeInfo* const element_type;
        const std::uint64_t element_count;
        const void* const layout;

        TypeInfo(
            RuntimeTypeKind type_kind,
            const char* type_name,
            const TypeInfo* element = nullptr,
            std::uint64_t count = 0,
            const void* type_layout = nullptr);

        TypeInfo(const TypeInfo&) = delete;
        TypeInfo& operator=(const TypeInfo&) = delete;
    };

    extern "C" const TypeInfo* __runtime_type_info_basic(std::uint8_t kind);
    extern "C" const TypeInfo* __runtime_type_info_pointer(const TypeInfo* element);
    extern "C" const TypeInfo* __runtime_type_info_reference(const TypeInfo* element);
    extern "C" const TypeInfo* __runtime_type_info_array(
        const TypeInfo* element,
        std::uint64_t count);
    extern "C" const TypeInfo* __runtime_type_info_struct(
        const char* name,
        const void* layout);
}

#endif // SAKURAE_RUNTIME_RTTYPE_H
