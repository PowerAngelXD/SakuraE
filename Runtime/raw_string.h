/*
    SakuraE Runtime Library
    raw_string.h
    2026-2-7

    By FZSGBall
*/

#ifndef SAKURAE_RUNTIME_RAW_STRING_H
#define SAKURAE_RUNTIME_RAW_STRING_H

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "alloc.h"
#include "value.h"

extern "C" sakuraE::runtime::RuntimeValue* create_string(const char* literal);

extern "C" void free_string(sakuraE::runtime::RuntimeValue* str);

extern "C" sakuraE::runtime::RuntimeValue* concat_string(
    sakuraE::runtime::RuntimeValue* s1,
    sakuraE::runtime::RuntimeValue* s2);

#endif
