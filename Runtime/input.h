/*
    SakuraE Runtime Library
    input.h
*/

#ifndef SAKURAE_RUNTIME_INPUT_H
#define SAKURAE_RUNTIME_INPUT_H

#include "value.h"

namespace sakuraE::runtime {
    extern "C" RuntimeValue* input();
    extern "C" RuntimeValue* inputc();
}

#endif // SAKURAE_RUNTIME_INPUT_H
