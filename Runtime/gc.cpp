#include "gc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <stack>
#include <vector>

#include "includes/String.hpp"

namespace sakuraE::runtime {
    size_t allocated_bytes = 0;
    size_t limit = 1024 * 1024;

    GCTypeInfo GC_ATOMIC_TYPE = {
        "atomic",
        GCObjectKind::Atomic,
        false,
        nullptr,
        nullptr
    };

    namespace {
        constexpr size_t MIN_LIMIT = 1024 * 1024;

        // 单线程实现只维护一套显式 root stack。
        // root 中保存的是“槽位地址”，GC 每次扫描时再读取槽位里的最新指针值。
        std::vector<void**> global_roots;

        // 每次进入一个词法作用域时记录 root stack 的深度，
        // 离开作用域后直接回退即可。
        std::vector<size_t> scope_markers;

        // 所有 GC 托管对象都挂在这条 heap list 上，sweep 阶段会线性遍历它。
        std::vector<ObjectHeader*> global_heap;

        // 为 array / struct 这类复合类型缓存 GCTypeInfo，避免重复分配描述符。
        std::map<fzlib::String, GCTypeInfo*> complex_gc_type_pool;
        std::list<fzlib::String> type_name_pool;

        // 防止 collect 过程中再次递归进入 collect。
        bool gc_collecting = false;

        inline char* payload_begin(ObjectHeader* header) {
            return reinterpret_cast<char*>(header + 1);
        }

        inline char* payload_end(ObjectHeader* header) {
            return payload_begin(header) + header->obj_size;
        }

        inline bool contains_payload_address(ObjectHeader* header, void* ptr) {
            if (!header || !ptr) {
                return false;
            }

            auto* addr = static_cast<char*>(ptr);
            return addr >= payload_begin(header) && addr < payload_end(header);
        }

        // 这是一个简单但完整的单线程 GC，因此直接线性扫描 heap list 来定位对象。
        // 这样除了 payload 起始地址，还能识别“指向对象内部”的 interior pointer。
        ObjectHeader* find_header_by_address(void* ptr) {
            if (!ptr) {
                return nullptr;
            }

            for (auto* header : global_heap) {
                if (contains_payload_address(header, ptr)) {
                    return header;
                }
            }

            return nullptr;
        }

        fzlib::String build_array_type_key(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
            return "array|" + std::to_string(is_ptr ? 1 : 0) + "|" + std::to_string(size) + "|" + mem_ty->name;
        }

        fzlib::String build_struct_type_key(const char* name, uint32_t ptr_count, const uint32_t* ptr_offsets) {
            fzlib::String key = "struct|";
            key += (name ? name : "<anonymous>");
            key += "|";
            key += std::to_string(ptr_count);

            for (uint32_t i = 0; i < ptr_count; ++i) {
                key += "|";
                key += std::to_string(ptr_offsets[i]);
            }

            return key;
        }

        inline void refresh_limit_after_collect() {
            limit = std::max(MIN_LIMIT, allocated_bytes == 0 ? MIN_LIMIT : allocated_bytes * 2);
        }
    }

    extern "C" GCTypeInfo* __gc_get_atomic_type() {
        return &GC_ATOMIC_TYPE;
    }

    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
        if (!mem_ty) {
            return nullptr;
        }

        fzlib::String key = build_array_type_key(is_ptr, size, mem_ty);
        if (complex_gc_type_pool.contains(key)) {
            return complex_gc_type_pool[key];
        }

        type_name_pool.push_back(key);
        const char* cached_name = type_name_pool.back().c_str();

        auto* type_info = new GCTypeInfo {
            cached_name,
            GCObjectKind::Array,
            is_ptr || mem_ty->contains_refs,
            nullptr,
            new GCArrayLayout {
                size,
                is_ptr,
                mem_ty
            }
        };

        complex_gc_type_pool[key] = type_info;
        return type_info;
    }

    // 这是给未来 struct object 预留的接口。
    // 当前语言主路径还未真正生成 struct 的 GC metadata，但 runtime 已经支持缓存和扫描规则描述。
    extern "C" GCTypeInfo* __gc_get_struct_type(const char* name, uint32_t ptr_count, const uint32_t* ptr_offsets) {
        if (ptr_count > 0 && !ptr_offsets) {
            return nullptr;
        }

        fzlib::String key = build_struct_type_key(name, ptr_count, ptr_offsets);
        if (complex_gc_type_pool.contains(key)) {
            return complex_gc_type_pool[key];
        }

        type_name_pool.push_back(key);
        const char* cached_name = type_name_pool.back().c_str();

        auto* layout = new GCStructLayout {};
        layout->ptr_count = ptr_count;

        if (ptr_count > 0) {
            layout->ptr_offsets = new uint32_t[ptr_count];
            std::memcpy(layout->ptr_offsets, ptr_offsets, sizeof(uint32_t) * ptr_count);
        }

        auto* type_info = new GCTypeInfo {
            cached_name,
            GCObjectKind::Struct,
            ptr_count > 0,
            layout,
            nullptr
        };

        complex_gc_type_pool[key] = type_info;
        return type_info;
    }

    extern "C" ObjectHeader* __gc_get_unlocked(void* payload) {
        return find_header_by_address(payload);
    }

    extern "C" void __gc_wklist_push(void* obj, void* context) {
        if (!obj || !context) {
            return;
        }

        auto* work_stack = static_cast<std::stack<void*>*>(context);
        work_stack->push(obj);
    }

    extern "C" void __gc_scan_struct(void* obj, GCStructLayout* s_layout, void (*visit)(void*, void*), void* context) {
        if (!obj || !s_layout || !visit) {
            return;
        }

        auto* base = static_cast<char*>(obj);
        for (uint32_t i = 0; i < s_layout->ptr_count; ++i) {
            uint32_t offset = s_layout->ptr_offsets[i];
            void* child = *reinterpret_cast<void**>(base + offset);
            if (child) {
                visit(child, context);
            }
        }
    }

    extern "C" void __gc_scan_embedded(void* mem, GCTypeInfo* ty, void (*visit)(void*, void*), void* ctx) {
        if (!mem || !ty || !visit || !ty->contains_refs) {
            return;
        }

        switch (ty->kind) {
            case GCObjectKind::Atomic:
                return;
            case GCObjectKind::Struct:
                __gc_scan_struct(mem, ty->struct_layout, visit, ctx);
                return;
            case GCObjectKind::Array:
                // 预留给未来“内嵌 array field”的扫描路径。
                // 当前简单 GC 只完整支持独立 heap object 形式的 array。
                return;
        }
    }

    extern "C" void __gc_scan_array(void* obj, ObjectHeader* header, GCArrayLayout* a_layout, void (*visit)(void*, void*), void* context) {
        if (!obj || !header || !a_layout || !visit) {
            return;
        }

        auto* base = static_cast<char*>(obj);
        for (uint64_t i = 0; i < header->elem_count; ++i) {
            void* element_addr = base + i * a_layout->member_size;

            if (a_layout->is_ptr) {
                void* child = *reinterpret_cast<void**>(element_addr);
                if (child) {
                    visit(child, context);
                }
                continue;
            }

            if (a_layout->member_type && a_layout->member_type->contains_refs) {
                __gc_scan_embedded(element_addr, a_layout->member_type, visit, context);
            }
        }
    }

    extern "C" void __gc_scan_object(void* obj, ObjectHeader* header, void (*visit)(void*, void*), void* ctx) {
        if (!obj || !header || !visit) {
            return;
        }

        GCTypeInfo* type_info = header->type_info;
        if (!type_info || !type_info->contains_refs) {
            return;
        }

        switch (type_info->kind) {
            case GCObjectKind::Atomic:
                return;
            case GCObjectKind::Struct:
                __gc_scan_struct(obj, type_info->struct_layout, visit, ctx);
                return;
            case GCObjectKind::Array:
                __gc_scan_array(obj, header, type_info->array_layout, visit, ctx);
                return;
        }
    }

    // 从一个显式 root 出发做 DFS mark。
    // 只要 root 指向了某个对象内部，GC 也会把整个宿主对象标活。
    extern "C" void __gc_scan_unlocked(void* root) {
        if (!root) {
            return;
        }

        std::stack<void*> work_stack;
        work_stack.push(root);

        while (!work_stack.empty()) {
            void* current = work_stack.top();
            work_stack.pop();

            ObjectHeader* header = find_header_by_address(current);
            if (!header || header->mark == Marked) {
                continue;
            }

            header->mark = Marked;
            __gc_scan_object(payload_begin(header), header, __gc_wklist_push, &work_stack);
        }
    }

    extern "C" void __gc_create_thread() {
        // 当前版本是单线程 stop-the-world GC，接口仅保留 ABI 兼容。
    }

    extern "C" void __gc_destroy_thread() {
        // 当前版本是单线程 stop-the-world GC，接口仅保留 ABI 兼容。
    }

    extern "C" void __gc_safe_point() {
        // 当前简单实现只在分配路径上触发 collect，不额外引入 safe point 逻辑。
    }

    extern "C" void __gc_enter_scope() {
        scope_markers.push_back(global_roots.size());
    }

    extern "C" void __gc_leave_scope() {
        if (scope_markers.empty()) {
            return;
        }

        size_t marker = scope_markers.back();
        scope_markers.pop_back();
        global_roots.resize(marker);
    }

    extern "C" void* __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t member_count) {
        const size_t total_size = sizeof(ObjectHeader) + size;

        if (!gc_collecting && allocated_bytes + total_size > limit) {
            __gc_collect();
        }

        auto* header = static_cast<ObjectHeader*>(std::malloc(total_size));
        if (!header) {
            std::fprintf(stderr, "[Runtime Error] Out of memory in __gc_alloc\n");
            std::exit(1);
        }

        header->type_info = ty ? ty : &GC_ATOMIC_TYPE;
        header->mark = Unmarked;
        header->obj_size = size;
        header->elem_count = member_count;

        void* payload = static_cast<void*>(header + 1);
        std::memset(payload, 0, size);

        global_heap.push_back(header);
        allocated_bytes += total_size;

        if (allocated_bytes > limit) {
            limit = std::max(limit * 2, allocated_bytes * 2);
        }

        return payload;
    }

    extern "C" void __gc_register(void** addr) {
        if (!addr) {
            return;
        }

        global_roots.push_back(addr);
    }

    extern "C" void __gc_pop(uint32_t times) {
        while (times > 0 && !global_roots.empty()) {
            global_roots.pop_back();
            --times;
        }
    }

    extern "C" void __gc_scan(void* ptr) {
        __gc_scan_unlocked(ptr);
    }

    // 单线程 stop-the-world mark-sweep：
    // 1. 从显式 root stack 递归标记可达对象
    // 2. 线性扫描 heap list，立即回收本轮未标记对象
    extern "C" void __gc_collect() {
        if (gc_collecting) {
            return;
        }

        gc_collecting = true;

        for (void** addr : global_roots) {
            if (addr && *addr) {
                __gc_scan_unlocked(*addr);
            }
        }

        auto it = global_heap.begin();
        while (it != global_heap.end()) {
            ObjectHeader* header = *it;

            if (header->mark == Marked) {
                header->mark = Unmarked;
                ++it;
                continue;
            }

            allocated_bytes -= sizeof(ObjectHeader) + header->obj_size;
            std::free(header);
            it = global_heap.erase(it);
        }

        refresh_limit_after_collect();
        gc_collecting = false;
    }

    struct GCCleaner {
        ~GCCleaner() {
            for (auto* header : global_heap) {
                std::free(header);
            }
            global_heap.clear();
            global_roots.clear();
            scope_markers.clear();

            for (auto& [_, type_info] : complex_gc_type_pool) {
                if (!type_info) {
                    continue;
                }

                if (type_info->array_layout) {
                    delete type_info->array_layout;
                }

                if (type_info->struct_layout) {
                    delete[] type_info->struct_layout->ptr_offsets;
                    delete type_info->struct_layout;
                }

                delete type_info;
            }

            complex_gc_type_pool.clear();
            type_name_pool.clear();
        }
    };

    static GCCleaner cleaner;
}
