/*
    SakuraE Runtime Library
    alloc.cpp
    2026-2-7

    By FZSGBall
*/

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include "alloc.h"

extern "C" void* __alloc(size_t size) {
    if (size <= 0) return nullptr;

    void* ptr = malloc(size);

    if (!ptr) {
        fprintf(stderr, "[Runtime Error] Out of memory in function: create_string");
        exit(1);
    }

    memset(ptr, 0, size);

    return ptr;
}

extern "C" void __free(void* ptr) {
    if (ptr) free(ptr);
}