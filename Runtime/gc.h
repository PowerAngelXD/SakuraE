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

#include "value.h"

namespace sakuraE::runtime {
    // 对象在一次 mark-sweep 周期中的可达性标记。
    enum GCMark: uint32_t {
        Unmarked,
        Marked
    };

    // GC 根据对象种类选择对应的引用扫描策略。
    enum class GCObjectKind: uint8_t {
        Atomic,
        Struct,
        Array
    };

    struct ObjectHeader;
    struct GCTypeInfo;

    struct GCStructLayout {
        // 结构体中每个托管指针字段相对于 payload 起始地址的字节偏移。
        uint32_t ptr_count;
        uint32_t* ptr_offsets = nullptr;
    };

    // 数组对象的扫描规则：元素大小、是否为指针、以及元素本身的类型信息。
    struct GCArrayLayout {
        // member_size 是单个元素的字节大小，length 用于扫描嵌入式数组。
        uint32_t member_size;
        bool is_ptr;
        uint64_t length;
        GCTypeInfo* member_type = nullptr;
        bool boxed_value = false;
    };

    // 每个堆对象都会挂一个 GCTypeInfo，标记阶段据此决定如何继续遍历。
    struct GCTypeInfo {
        // contains_refs 为 false 时可以跳过整个对象的递归扫描。
        const char* name;
        GCObjectKind kind;
        bool contains_refs;
        GCStructLayout* struct_layout = nullptr;
        GCArrayLayout* array_layout = nullptr;
    };

    // ObjectHeader 紧挨在对象 payload 前面，生成代码只拿到 payload 指针。
    struct ObjectHeader {
        // header 紧邻 payload，elem_count 仅对堆数组对象表示元素数量。
        GCTypeInfo* type_info;
        GCMark mark;
        uint64_t obj_size;
        uint64_t elem_count;
    };

    extern size_t allocated_bytes;
    extern size_t limit;
    extern GCTypeInfo GC_ATOMIC_TYPE;

    // 获取静态的无引用原子类型描述符。
    extern "C" GCTypeInfo* __gc_get_atomic_type();
    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty);
    extern "C" GCTypeInfo* __gc_get_array_type_with_length(bool is_ptr, uint32_t size, uint64_t length, GCTypeInfo* mem_ty);
    extern "C" GCTypeInfo* __gc_get_runtime_value_array_type(uint32_t size, uint64_t length);
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

    // root scope 采用栈式嵌套管理，离开作用域时回退到进入前的 root 深度。
    extern "C" void   __gc_enter_scope();
    extern "C" void   __gc_leave_scope();
    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t member_count = 0);
    extern "C" void   __gc_register(void** addr);
    // Register a boxed value slot; the collector follows managed references
    // described by RuntimeValue instead of treating scalar storage as a pointer.
    extern "C" void   __gc_register_value(RuntimeValue* value);
    extern "C" void   __gc_register_value_slot(RuntimeValue** slot);
    extern "C" void   __gc_pop(uint32_t times);
    extern "C" void   __gc_scan(void* ptr);
    extern "C" void   __gc_scan_value(const RuntimeValue* value);
    extern "C" void   __gc_collect();
    extern "C" void   __gc_reset();
}

#endif // SakuraE 运行时 GC 头文件保护
