/*
    SakuraE Runtime Library
    input.cpp
*/

#include "input.h"

#include "gc.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace sakuraE::runtime {
    extern "C" RuntimeValue* input() {
        auto* value = __runtime_alloc_value();
        value->type = RuntimeType::String;

        std::string line;
        if (!std::getline(std::cin, line)) {
            value->data.string = {nullptr, 0};
            return value;
        }

        if (line.size() == std::numeric_limits<std::size_t>::max()) {
            std::fprintf(stderr, "[Runtime Error] Input line is too large\n");
            std::exit(1);
        }

        auto* data = static_cast<char*>(
            __gc_alloc(line.size() + 1, __gc_get_atomic_type()));
        std::memcpy(data, line.data(), line.size());
        data[line.size()] = '\0';

        value->data.string = {
            data,
            static_cast<std::uint64_t>(line.size())
        };
        __gc_register(reinterpret_cast<void**>(
            const_cast<char**>(&value->data.string.data)));
        return value;
    }

    extern "C" RuntimeValue* inputc() {
        auto* value = __runtime_alloc_value();
        value->type = RuntimeType::I8;
        value->data.i8 = std::getc(stdin);
        return value;
    }
}
