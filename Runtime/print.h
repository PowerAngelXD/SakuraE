#ifndef SAKURAE_RUNTIME_PRINT_H
#define SAKURAE_RUNTIME_PRINT_H

#include <cstdlib>
#include <stdio.h>

extern "C" void __print(char* str);

extern "C" void __println(char* str);

#endif