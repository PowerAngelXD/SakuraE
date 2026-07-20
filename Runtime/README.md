# SakuraE Runtime Library

[简体中文](README-zh_cn.md)

The runtime library for the SakuraE compiler, providing memory management, string manipulation, and basic I/O support.

## File List and Functionality

### 1. Memory Management
*   **[Garbage Collector Design](GC.md)**: Design, APIs, object layout, root management, and LLVM integration.
*   **[`alloc.h`](Runtime/alloc.h)**: Header for the memory allocator, defining core runtime allocation interfaces.
*   **[`alloc.cpp`](Runtime/alloc.cpp)**: 
    *   `__alloc(size_t size)`: Wraps `malloc` to provide heap allocation with zero-initialization and error handling for out-of-memory conditions.
    *   `__free(void* ptr)`: Wraps `free` for releasing heap memory.

### 2. String Manipulation
*   **[`raw_string.cpp`](Runtime/raw_string.cpp)**:
    *   `create_string(const char* literal)`: Copies a C-style string literal into heap memory, supporting string mutability.
    *   `free_string(char* str)`: Compatibility no-op; strings are reclaimed by the GC.
    *   `concat_string(const char* s1, const char* s2)`: Concatenates two strings and returns a new heap-allocated string.

### 3. Basic I/O
*   **[`print.cpp`](Runtime/print.cpp)**:
    *   `__print(char* str)`: Prints a string to standard output.
    *   `__println(char* str)`: Prints a string followed by a newline.

## Compilation and Linking
These files are typically compiled into object files or a static library and linked with the user program after LLVM code generation to provide essential runtime support.
