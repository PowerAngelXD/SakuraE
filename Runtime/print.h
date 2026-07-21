/*
    SakuraE Runtime Library
    print.h
    2026-2-7

    By FZSGBall
*/

#ifndef SAKURAE_RUNTIME_PRINT_H
#define SAKURAE_RUNTIME_PRINT_H

#include <cstdint>
#include <cstdlib>
#include <stdio.h>
#include "value.h"


extern "C" void print(const sakuraE::runtime::RuntimeValue* value);
extern "C" void println(const sakuraE::runtime::RuntimeValue* value);


#endif
