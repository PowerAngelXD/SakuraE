#include "errors.h"

#include <cstdlib>
#include <pthread.h>

namespace sakuraE::runtime {
    namespace {
        RuntimeErrorInfo runtime_error;
        bool runtime_error_active = false;
    }

    extern "C" void __runtime_check_array_bounds(
        std::int64_t index,
        std::uint64_t length) {
        if (index < 0 || static_cast<std::uint64_t>(index) >= length) {
            __runtime_array_bounds_error(index, length);
        }
    }

    extern "C" [[noreturn]] void __runtime_array_bounds_error(
        std::int64_t index,
        std::uint64_t length) {
        runtime_error = {index, length};
        runtime_error_active = true;
        // JIT code has no C++ exception/unwind ABI. End only its worker
        // thread and let the host convert this state into SakuraError.
        pthread_exit(nullptr);
        std::abort();
    }

    extern "C" bool __runtime_take_error(RuntimeErrorInfo* error) {
        if (!runtime_error_active) {
            return false;
        }

        if (error) {
            *error = runtime_error;
        }
        runtime_error_active = false;
        return true;
    }

    extern "C" void __runtime_reset_error() {
        runtime_error = {};
        runtime_error_active = false;
    }
}
