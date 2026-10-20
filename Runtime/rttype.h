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

    struct RuntimeTypeInfo {
        const RuntimeTypeKind kind;
        const char* const name;
        const RuntimeTypeInfo* const element_type;
        const std::uint64_t element_count;
        const void* const layout;

        RuntimeTypeInfo(
            RuntimeTypeKind type_kind,
            const char* type_name,
            const RuntimeTypeInfo* element = nullptr,
            std::uint64_t count = 0,
            const void* type_layout = nullptr);

        RuntimeTypeInfo(const RuntimeTypeInfo&) = delete;
        RuntimeTypeInfo& operator=(const RuntimeTypeInfo&) = delete;
    };

    extern "C" const RuntimeTypeInfo* __runtime_type_info_basic(std::uint8_t kind);
    extern "C" const RuntimeTypeInfo* __runtime_type_info_pointer(const RuntimeTypeInfo* element);
    extern "C" const RuntimeTypeInfo* __runtime_type_info_reference(const RuntimeTypeInfo* element);
    extern "C" const RuntimeTypeInfo* __runtime_type_info_array(
        const RuntimeTypeInfo* element,
        std::uint64_t count);
    extern "C" const RuntimeTypeInfo* __runtime_type_info_struct(
        const char* name,
        const void* layout);
}

#endif // SAKURAE_RUNTIME_RTTYPE_H
