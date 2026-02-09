#ifndef SAKURAE_RUNTIME_RAW_STRING_H
#define SAKURAE_RUNTIME_RAW_STRING_H

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "alloc.h"

extern "C" char* create_string(const char* literal);

extern "C" void free_string(char* str);

extern "C" char* concat_string(const char* s1, const char* s2);

#endif