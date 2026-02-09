/*
    SakuraE Runtime Library
    print.cpp
    2026-2-7

    By FZSGBall
*/

#include "print.h"

extern "C" void __print(char* str) {
    if(str) printf("%s", str);
}

extern "C" void __println(char* str) {
    if (str) printf("%s\n", str);
}