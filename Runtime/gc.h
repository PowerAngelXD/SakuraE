/*
    SakuraE Runtime Library
    gc.h
    2026-2-13

    By FZSGBall
*/

#ifndef SAKURAE_RUNTIME_GC_H
#define SAKURAE_RUNTIME_GC_H

#include <cstddef>
#include <cstdint>

namespace sakuraE::runtime {
    enum GCMark: uint32_t {
        Unmarked,
        Marked
    };

    enum class GCObjectKind: uint8_t {
        Atomic,
        Struct,
        Array
    };

    struct ObjectHeader;
    struct GCTypeInfo;

    struct GCStructLayout {
        uint32_t ptr_count;
        uint32_t* ptr_offsets = nullptr;
    };

    // 数组对象的扫描规则：元素大小、是否为指针、以及元素本身的类型信息。
    struct GCArrayLayout {
        uint32_t member_size;
        bool is_ptr;
        GCTypeInfo* member_type = nullptr;
    };

    // 每个堆对象都会挂一个 GCTypeInfo，标记阶段据此决定如何继续遍历。
    struct GCTypeInfo {
        const char* name;
        GCObjectKind kind;
        bool contains_refs;
        GCStructLayout* struct_layout = nullptr;
        GCArrayLayout* array_layout = nullptr;
    };

    // ObjectHeader 紧挨在对象 payload 前面，生成代码只拿到 payload 指针。
    struct ObjectHeader {
        GCTypeInfo* type_info;
        GCMark mark;
        uint64_t obj_size;
        uint64_t elem_count;
    };

    extern size_t allocated_bytes;
    extern size_t limit;
    extern GCTypeInfo GC_ATOMIC_TYPE;

    extern "C" GCTypeInfo* __gc_get_atomic_type();
    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty);
    extern "C" GCTypeInfo* __gc_get_struct_type(const char* name, uint32_t ptr_count, const uint32_t* ptr_offsets);

    extern "C" ObjectHeader* __gc_get_unlocked(void* payload);
    extern "C" void __gc_wklist_push(void* obj, void* context);
    extern "C" void __gc_scan_struct(void* obj, GCStructLayout* s_layout, void (*visit)(void*, void*), void* context);
    extern "C" void __gc_scan_embedded(void* mem, GCTypeInfo* ty, void (*visit)(void*, void*), void* ctx);
    extern "C" void __gc_scan_array(void* obj, ObjectHeader* header, GCArrayLayout* a_layout, void (*visit)(void*, void*), void* context);
    extern "C" void __gc_scan_object(void* obj, ObjectHeader* header, void (*visit)(void*, void*), void* ctx);
    extern "C" void __gc_scan_unlocked(void* root);

    // 这些接口仅为了兼容现有 JIT / codegen 的调用约定而保留。
    // 在当前单线程实现里，它们本身不再承担实际工作。
    extern "C" void   __gc_create_thread();
    extern "C" void   __gc_destroy_thread();
    extern "C" void   __gc_safe_point();

    extern "C" void   __gc_enter_scope();
    extern "C" void   __gc_leave_scope();
    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t member_count = 0);
    extern "C" void   __gc_register(void** addr);
    extern "C" void   __gc_pop(uint32_t times);
    extern "C" void   __gc_scan(void* ptr);
    extern "C" void   __gc_collect();
}

#endif // SakuraE 运行时 GC 头文件保护
