/*
    SakuraE Runtime Library
    raw_string.cpp
    2026-2-7

    By FZSGBall
*/
#include "raw_string.h"
#include "gc.h"
#include "alloc.h"

using namespace sakuraE::runtime;

extern "C" char* create_string(const char* literal) {
    if (!literal) return nullptr;

    size_t len = strlen(literal);
    char* str = (char*)__gc_alloc(len + 1, __gc_get_atomic_type());

    strcpy(str, literal);
    return str;
}

extern "C" void free_string(char* str) {
    (void)str;
}

extern "C" char* concat_string(const char* s1, const char* s2) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";

    // `concat_string` 在真正拼接前可能先触发新的 GC 分配。
    // 因此先把两个入参临时压入根栈，避免它们在本次调用中途被误回收。
    void* root1 = const_cast<char*>(s1);
    void* root2 = const_cast<char*>(s2);

    __gc_enter_scope();
    __gc_register(&root1);
    __gc_register(&root2);

    // 之后统一从被根住的局部变量中读取安全入参。
    const char* safe_s1 = static_cast<const char*>(root1);
    const char* safe_s2 = static_cast<const char*>(root2);

    size_t len1 = strlen(safe_s1);
    size_t len2 = strlen(safe_s2);

    char* result = (char*)__gc_alloc(len1 + len2 + 1, __gc_get_atomic_type());
    if (!result) exit(1);

    strcpy(result, safe_s1);
    strcat(result, safe_s2);
    // 拼接完成后，释放本次调用临时压入的根。
    __gc_leave_scope();
    return result;
}
