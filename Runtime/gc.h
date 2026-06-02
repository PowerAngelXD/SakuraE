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
#include <string>
#include <atomic>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stack>
#include <unordered_map>
#include <list>

#include "includes/String.hpp"

namespace sakuraE::runtime {
    // GC 标记状态
    enum GCMark: uint32_t {
        Unscanned,
        Uncomplete,
        Marked
    };

    // GC 对象类型
    enum class GCObjectKind: uint8_t {
        Atomic,
        Struct,
        Array
    };
    
    struct ObjectHeader;
    struct GCTypeInfo;

    // 结构体中指针字段的布局描述
    struct GCStructLayout {
        uint32_t ptr_count;
        uint32_t* ptr_offsets = nullptr;
    };

    // 数组元素布局描述
    struct GCArrayLayout {
        uint32_t member_size;
        bool is_ptr;
        GCTypeInfo* member_type = nullptr;
    };

    // 类型信息用于扫描引用
    struct GCTypeInfo {
        const char* name;
        GCObjectKind kind;
        bool contains_refs;
        GCStructLayout* struct_layout = nullptr;
        GCArrayLayout* array_layout = nullptr;
    };

    // 对象头部
    struct ObjectHeader {
        GCTypeInfo* type_info;
        std::atomic<GCMark> obj_status;
        uint64_t obj_size;
        uint64_t elem_count;
    };

    // GC 运行状态
    extern std::atomic<bool> need_gc;
    extern std::atomic<int>  total_active;
    extern std::atomic<int>  safepoints;
    extern std::condition_variable gc_cv;
    extern std::condition_variable resume_cv;

    // 分配与根集合
    extern std::atomic<size_t> allocated_bytes;
    extern size_t limit;
    extern thread_local std::vector<void**> own_stack;
    extern thread_local bool is_registered;
    extern std::vector<std::vector<void**>*> global_stacks;
    extern std::vector<ObjectHeader*> global_heap;
    extern std::mutex gc_mutex;

    [[maybe_unused]] extern inline GCTypeInfo GC_ATOMIC_TYPE;

    // 获取基础类型与数组类型信息
    extern "C" GCTypeInfo* __gc_get_atomic_type();
    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty);

    // 低层扫描接口
    extern "C" ObjectHeader* __gc_get_unlocked(void* payload);
    extern "C" void __gc_wklist_push(void* obj, void* context);
    extern "C" void __gc_scan_struct(void* obj, GCStructLayout* s_layout, void (*visit)(void*, void*), void* context);
    extern "C" void __gc_scan_embedded(void* mem, GCTypeInfo* ty, void (*visit)(void*, void*), void* ctx);
    extern "C" void __gc_scan_array(void* obj, ObjectHeader* header, GCArrayLayout* a_layout, void (*visit)(void*, void*), void* context);
    extern "C" void __gc_scan_object(void* obj, ObjectHeader* header, void (*visit)(void*, void*), void* ctx);
    extern "C" void __gc_scan_unlocked(void* root);

    // 线程与分配接口
    extern "C" void   __gc_create_thread();
    extern "C" void   __gc_destroy_thread();
    extern "C" void   __gc_safe_point();
    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t member_count = 0);
    extern "C" void   __gc_register(void** addr);
    extern "C" void   __gc_pop(uint32_t times);
    extern "C" void   __gc_scan(void* ptr);
    extern "C" void   __gc_collect();
}

#endif // !SAKURAE_RUNTIME_GC_H
