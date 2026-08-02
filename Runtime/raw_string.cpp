/*
    SakuraE Runtime Library
    raw_string.cpp
    2026-2-7

    By FZSGBall
*/
#include "raw_string.h"
#include "gc.h"
#include "alloc.h"
#include <limits>

using namespace sakuraE::runtime;

extern "C" RuntimeValue* create_string(const char* literal) {
    auto* value = __runtime_alloc_value();
    value->type = RuntimeType::String;
    if (!literal) return value;

    size_t len = strlen(literal);
    if (len == std::numeric_limits<size_t>::max()) {
        std::fprintf(stderr, "[Runtime Error] String size overflow in create_string\n");
        std::exit(1);
    }
    // 语言字符串使用 GC 原子对象承载，调用方不需要手动释放。
    char* str = (char*)__gc_alloc(len + 1, __gc_get_atomic_type());

    strcpy(str, literal);
    value->data.string = {str, static_cast<std::uint64_t>(len)};
    // Keep a newly created string alive until the surrounding runtime scope
    // ends; the caller may not have installed its wrapper root yet.
    __gc_register(reinterpret_cast<void**>(const_cast<char**>(&value->data.string.data)));
    return value;
}

extern "C" void free_string(RuntimeValue* str) {
    // Strings are GC-managed; retain this ABI-compatible no-op for existing callers.
    (void)str;
}

extern "C" RuntimeValue* concat_string(RuntimeValue* s1, RuntimeValue* s2) {
    auto* value = __runtime_alloc_value();
    value->type = RuntimeType::String;
    const char* raw_s1 = s1 && s1->type == RuntimeType::String ? s1->data.string.data : nullptr;
    const char* raw_s2 = s2 && s2->type == RuntimeType::String ? s2->data.string.data : nullptr;
    if (!raw_s1) raw_s1 = "";
    if (!raw_s2) raw_s2 = "";

    // `concat_string` 在真正拼接前可能先触发新的 GC 分配。
    // 因此先把两个入参临时压入根栈，避免它们在本次调用中途被误回收。
    __gc_enter_scope();
    void* root1 = const_cast<char*>(raw_s1);
    void* root2 = const_cast<char*>(raw_s2);
    __gc_register(&root1);
    __gc_register(&root2);

    const char* safe_s1 = static_cast<const char*>(root1);
    const char* safe_s2 = static_cast<const char*>(root2);

    size_t len1 = strlen(safe_s1);
    size_t len2 = strlen(safe_s2);
    if (len1 > std::numeric_limits<size_t>::max() - len2 - 1) {
        std::fprintf(stderr, "[Runtime Error] String size overflow in concat_string\n");
        std::exit(1);
    }

    char* result = (char*)__gc_alloc(len1 + len2 + 1, __gc_get_atomic_type());
    if (!result) exit(1);

    strcpy(result, safe_s1);
    strcat(result, safe_s2);
    // 拼接完成后，释放本次调用临时压入的根。
    __gc_leave_scope();
    value->data.string = {result, static_cast<std::uint64_t>(len1 + len2)};
    __gc_register(reinterpret_cast<void**>(const_cast<char**>(&value->data.string.data)));
    return value;
}
