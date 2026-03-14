#include "gc.h"

namespace sakuraE::runtime {
    // status
    std::atomic<bool> need_gc {false};
    std::atomic<int>  total_active {0};
    std::atomic<int>  safepoints {0};
    std::condition_variable gc_cv;
    std::condition_variable resume_cv;

    // alloc
    std::atomic<size_t> allocated_bytes {0};
    size_t limit = 1024 * 1024;
    thread_local std::vector<void**> own_stack;
    thread_local bool is_registered = false;
    std::vector<std::vector<void**>*> global_stacks;
    std::vector<ObjectHeader*> global_heap;
    std::mutex gc_mutex;

    [[maybe_unused]] GCTypeInfo GC_ATOMIC_TYPE = {
        "atomic",
        GCObjectKind::Atomic,
        false,
        nullptr,
        nullptr
    };

    [[maybe_unused]] GCTypeInfo GC_STRING_TYPE = {
        "string",
        GCObjectKind::Atomic,
        false,
        nullptr,
        nullptr
    };

    std::map<fzlib::String, GCTypeInfo*> complexGCTypePool;

    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
        fzlib::String id = std::to_string(is_ptr) + std::to_string(size) + mem_ty->name;
        if (complexGCTypePool.contains(id)) return complexGCTypePool[id];
        else {
            complexGCTypePool[id] = new GCTypeInfo {
                id.c_str(),
                GCObjectKind::Array,
                is_ptr,
                nullptr,
                new GCArrayLayout {
                    size,
                    is_ptr,
                    mem_ty
                }
            };
            return complexGCTypePool[id];
        }
    }

    extern "C" GCTypeInfo* __gc_get_atomic_type() {
        return &GC_ATOMIC_TYPE;
    }

    extern "C" GCTypeInfo* __gc_get_string_type() {
        return &GC_ATOMIC_TYPE;
    }

    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
        
    }

    extern "C" ObjectHeader* __gc_get_unlocked(void* payload) {
        for (auto* header : global_heap) {
            if (static_cast<void*>(header + 1) == payload) {
                return header;
            }
        }
        return nullptr;
    }

    extern "C" void __gc_wklist_push(void* obj, void* context) {
        if (!obj) {
            return;
        }
        auto* work = static_cast<std::stack<void*>*>(context);
        work->push(obj);
    }

    extern "C" void __gc_scan_struct(void* obj, GCStructLayout* s_layout, void (*visit)(void*, void*), void* context) {
        if (!obj || !s_layout) {
            return;
        }
        auto* base = static_cast<char*>(obj);
        for (uint32_t i = 0; i < s_layout->ptr_count; ++i) {
            uint32_t off = s_layout->ptr_offsets[i];
            void* child = *reinterpret_cast<void**>(base + off);
            if (child) {
                visit(child, context);
            }
        }
    }

    extern "C" void __gc_scan_embedded(void* mem, GCTypeInfo* ty, void (*visit)(void*, void*), void* ctx) {
        if (!mem || !ty || !ty->contains_refs) {
            return;
        }
        switch (ty->kind) {
            case GCObjectKind::Atomic:
                return;
            case GCObjectKind::Struct:
                __gc_scan_struct(mem, ty->struct_layout, visit, ctx);
                return;
            case GCObjectKind::Array: {
                ObjectHeader* header = (ObjectHeader*)mem;
                __gc_scan_array(mem, header, header->type_info->array_layout, visit, ctx);
                return;
            }
        }
    }

    extern "C" void __gc_scan_array(void* obj, ObjectHeader* header, GCArrayLayout* a_layout, void (*visit)(void*, void*), void* context) {
        if (!obj || !header || !a_layout) {
            return;
        }
        auto* base = static_cast<char*>(obj);
        for (uint64_t i = 0; i < header->elem_count; ++i) {
            void* elem_addr = base + i * a_layout->member_size;
            if (a_layout->is_ptr) {
                void* child = *reinterpret_cast<void**>(elem_addr);
                if (child) {
                    visit(child, context);
                }
                continue;
            }
            if (a_layout->member_type && a_layout->member_type->contains_refs) {
                __gc_scan_embedded(elem_addr, a_layout->member_type, visit, context);
            }
        }
    }

    extern "C" void __gc_scan_object(void* obj, ObjectHeader* header, void (*visit)(void*, void*), void* ctx) {
        if (!obj || !header) {
            return;
        }
        GCTypeInfo* ty = header->type_info;
        if (!ty || !ty->contains_refs) {
            return;
        }
        switch (ty->kind) {
            case GCObjectKind::Atomic:
                return;
            case GCObjectKind::Struct:
                __gc_scan_struct(obj, ty->struct_layout, visit, ctx);
                return;
            case GCObjectKind::Array:
                __gc_scan_array(obj, header, ty->array_layout, visit, ctx);
                return;
        }
    }

    extern "C" void __gc_scan_unlocked(void* root) {
        if (!root) {
            return;
        }
        std::stack<void*> work_stack;
        work_stack.push(root);

        while (!work_stack.empty()) {
            void* obj = work_stack.top();
            work_stack.pop();
            if (!obj) {
                continue;
            }
            ObjectHeader* header = __gc_get_unlocked(obj);
            if (!header) {
                continue;
            }
            GCMark old = header->obj_status.exchange(Marked, std::memory_order_relaxed);
            if (old == Marked) {
                continue;
            }
            __gc_scan_object(obj, header, __gc_wklist_push, &work_stack);
        }
    }

    extern "C" void   __gc_create_thread() {
        std::lock_guard<std::mutex> lock(gc_mutex);
        
        if (!is_registered) {
            global_stacks.push_back(&own_stack);
            is_registered = true;
            total_active.fetch_add(1, std::memory_order_relaxed);
        }
    }

    extern "C" void   __gc_safe_point() {
        if (need_gc.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(gc_mutex);
            safepoints ++;

            gc_cv.notify_one();

            resume_cv.wait(lock, [] { return !need_gc.load(); });

            safepoints --;
        }
    }

    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t mem_count) {
        if (allocated_bytes + size > limit) {
            bool expected = false;
            if (need_gc.compare_exchange_strong(expected, true)) {
                __gc_collect();
            }
            else {
                __gc_safe_point();
            }
        }

        ObjectHeader* header = (ObjectHeader*)malloc(sizeof(ObjectHeader) + size);
        header->obj_size = size;
        header->obj_status.store(Unscanned);
        header->elem_count = mem_count;
        header->type_info = ty ? ty : &GC_ATOMIC_TYPE;

        std::lock_guard<std::mutex> lock(gc_mutex);
        global_heap.push_back(header);
        allocated_bytes.fetch_add(size, std::memory_order_relaxed);

        return (void*)(header + 1);
    }

    extern "C" void   __gc_register(void** addr) {
        own_stack.push_back(addr);
    }

    extern "C" void   __gc_pop(uint32_t times) {
        for (uint32_t i = 0; i < times; i ++) {
            if (!own_stack.empty()) own_stack.pop_back();
        }
    }

    extern "C" void   __gc_scan(void* ptr) {
        std::lock_guard<std::mutex> lock(gc_mutex);
        __gc_scan_unlocked(ptr);
    }

    extern "C" void   __gc_collect() {
        std::unique_lock<std::mutex> lock(gc_mutex);

        gc_cv.wait(lock, [] {
            return safepoints == (total_active - 1);
        });

        for (auto* stk: global_stacks) {
            for (void** addr: *stk) {
                if (addr && *addr) __gc_scan_unlocked(*addr);
            }
        }

        auto it = global_heap.begin();
        while (it != global_heap.end()) {
            ObjectHeader* header = *it;
            if (header->obj_status == Unscanned) {
                allocated_bytes.fetch_sub(header->obj_size);
                free(header);
                it = global_heap.erase(it);
            }
            else {
                header->obj_status = Unscanned;
                it ++;
            }
        }

        if (allocated_bytes > limit * 0.7) {
            limit *= 2;
        }

        need_gc = false;
        resume_cv.notify_all();
    }


    struct GCCleaner {
        ~GCCleaner() {
            for (auto* header : global_heap) {
                free(header);
            }
            global_heap.clear();
        }
    };
    static GCCleaner cleaner;
}