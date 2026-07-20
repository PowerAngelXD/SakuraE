/*
    SakuraE Runtime Library
    print.cpp
    2026-2-7

    By FZSGBall
*/

#include "print.h"

using namespace sakuraE::runtime;

extern "C" void __print(const RuntimeValue* value) {
    if (!value) return;
    switch (value->type) {
        // I8 is the language char type, so print its character value rather
        // than exposing the underlying ASCII/code-point number.
        case RuntimeType::I8: printf("%c", static_cast<unsigned char>(value->data.i8)); break;
        case RuntimeType::I32: printf("%d", value->data.i32); break;
        case RuntimeType::I64: printf("%lld", static_cast<long long>(value->data.i64)); break;
        case RuntimeType::U8: printf("%u", static_cast<unsigned>(value->data.u8)); break;
        case RuntimeType::U32: printf("%u", value->data.u32); break;
        case RuntimeType::U64: printf("%llu", static_cast<unsigned long long>(value->data.u64)); break;
        case RuntimeType::F32: printf("%g", static_cast<double>(value->data.f32)); break;
        case RuntimeType::F64: printf("%g", value->data.f64); break;
        case RuntimeType::Bool: printf("%s", value->data.boolean ? "true" : "false"); break;
        case RuntimeType::String:
            if (value->data.string.data) printf("%s", value->data.string.data);
            break;
        default: printf("<object>"); break;
    }
}

extern "C" void __println(const RuntimeValue* value) {
    __print(value);
    putchar('\n');
}
