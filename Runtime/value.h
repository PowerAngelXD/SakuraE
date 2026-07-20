/*
    SakuraE Runtime Library
    value.h

    Stable runtime representation shared by runtime libraries and generated code.
*/

#ifndef SAKURAE_RUNTIME_VALUE_H
#define SAKURAE_RUNTIME_VALUE_H

#include <cstddef>
#include <cstdint>

namespace sakuraE::runtime {
    enum class RuntimeType : std::uint8_t {
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
        Array,
        Struct,
        Pointer
    };

    struct RuntimeString {
        const char* data;
        std::uint64_t length;
    };

    struct RuntimeAggregate {
        void* data;
        std::uint64_t length;
        std::uint64_t stride;
        const void* layout;
    };

    union RawValue {
        std::int8_t i8;
        std::int32_t i32;
        std::int64_t i64;
        std::uint8_t u8;
        std::uint32_t u32;
        std::uint64_t u64;
        float f32;
        double f64;
        bool boolean;
        RuntimeString string;
        RuntimeAggregate aggregate;
        void* pointer;
    };

    struct RuntimeValue {
        RuntimeType type;
        RawValue data;
    };

    extern "C" RuntimeValue* __runtime_alloc_value();

    static_assert(sizeof(RuntimeString) == sizeof(void*) + sizeof(std::uint64_t));
    static_assert(sizeof(RuntimeAggregate) == sizeof(void*) + sizeof(std::uint64_t) * 3);
    static_assert(offsetof(RuntimeValue, data) == alignof(RawValue));
    static_assert(sizeof(RuntimeValue) == offsetof(RuntimeValue, data) + sizeof(RawValue));
}

#endif // SAKURAE_RUNTIME_VALUE_H
