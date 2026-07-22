/*
    SakuraE Runtime Library
    errors.h
*/

#ifndef SAKURAE_RUNTIME_ERRORS_H
#define SAKURAE_RUNTIME_ERRORS_H

#include <cstdint>

namespace sakuraE::runtime {
    struct RuntimeErrorInfo {
        std::int64_t index = 0;
        std::uint64_t length = 0;
    };

    extern "C" void __runtime_check_array_bounds(
        std::int64_t index,
        std::uint64_t length);

    extern "C" [[noreturn]] void __runtime_array_bounds_error(
        std::int64_t index,
        std::uint64_t length);

    extern "C" bool __runtime_take_error(RuntimeErrorInfo* error);
    extern "C" void __runtime_reset_error();
}

#endif // SAKURAE_RUNTIME_ERRORS_H
