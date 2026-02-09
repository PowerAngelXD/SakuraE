/*
    SakuraE Runtime Library
    raw_string.cpp
    2026-2-7

    By FZSGBall
*/
#include "raw_string.h"
#include "alloc.h"

extern "C" char* create_string(const char* literal) {
    if (!literal) return nullptr;

    size_t len = strlen(literal);
    char* str = (char*)__alloc(len + 1);

    strcpy(str, literal);
    return str;
}

extern "C" void free_string(char* str) {
    if (str) __free(str);
}

extern "C" char* concat_string(const char* s1, const char* s2) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
        
    char* result = (char*)__alloc(len1 + len2 + 1);
    if (!result) exit(1);

    strcpy(result, s1);
    strcat(result, s2);
    return result;
}