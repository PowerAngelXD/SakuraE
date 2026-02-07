#ifndef SAKURAE_RUNTIME_ALLOC_H
#define SAKURAE_RUNTIME_ALLOC_H

#include <cstddef>

extern "C" void* __alloc(size_t size);
extern "C" void __free(void* ptr);

#endif