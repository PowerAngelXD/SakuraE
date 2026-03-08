/*
    SakuraE Runtime Library
    gc.h
    2026-2-13

    By FZSGBall
*/

#ifndef SAKURAE_RUNTIME_GC_H
#define SAKURAE_RUNTIME_GC_H

#include <condition_variable>
#include <cstdint>
#include <sys/types.h>
#include <vector>
#include <mutex>
#include <stdint.h>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stack>

namespace sakuraE::runtime {
    enum GCMark: uint32_t {
        Unscanned,
        Uncomplete,
        Marked
    };

    enum class GCObjectKind: uint8_t {
        Atomic,
        Struct,
        Array,
        Object
    };
    
    struct ObjectHeader;
    struct GCTypeInfo;

    struct GCStructLayout {
        uint32_t ptr_count;
        const uint32_t* ptr_offsets;
    };

    struct GCArrayLayout {
        uint32_t member_size;
        bool is_ptr;
        const GCTypeInfo* member_type;
    };

    struct GCTypeInfo {
        const char* name;
        GCObjectKind kind;
        bool contains_refs;
        const GCStructLayout* struct_layout;
        const GCArrayLayout* array_layout;
    };

    struct ObjectHeader {
        const GCTypeInfo* type_info;
        std::atomic<GCMark> obj_status;
        uint64_t obj_size;
        uint64_t elem_count;
    };

    // status
    static std::atomic<bool> need_gc {false};
    static std::atomic<int>  total_active {0};
    static std::atomic<int>  safepoints {0};
    static std::condition_variable gc_cv;
    static std::condition_variable resume_cv;

    // alloc
    static std::atomic<size_t> allocated_bytes {0};
    inline size_t limit = 1024 * 1024;
    static thread_local std::vector<void**> own_stack;
    static std::vector<std::vector<void**>*> global_stacks;
    static std::vector<ObjectHeader*> global_heap;
    static std::mutex gc_mutex;

    extern "C" void   __gc_create_thread();
    extern "C" void   __gc_safe_point();
    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t member_count = 0);
    extern "C" void   __gc_register(void** addr);
    extern "C" void   __gc_pop(uint32_t times);
    extern "C" void   __gc_scan(void* ptr);
    extern "C" void   __gc_collect();
}

#endif // !SAKURAE_RUNTIME_GC_H
