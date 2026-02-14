#include "gc.h"

namespace sakuraE::runtime {
    extern "C" void   __gc_create_thread() {
        std::lock_guard<std::mutex> lock(gc_mutex);
        global_stacks.push_back(&own_stack);
        total_active ++;
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

    extern "C" void*  __gc_alloc(size_t size, ObjectType ty) {
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
        header->obj_type = ty;

        std::lock_guard<std::mutex> lock(gc_mutex);
        global_heap.push_back(header);
        allocated_bytes.fetch_add(size);

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
        if (!ptr) return;

        std::stack<void*> work_stack;
        work_stack.push(ptr);

        while (!work_stack.empty()) {
            void* _ptr = work_stack.top();
            work_stack.pop();

            ObjectHeader* header = (ObjectHeader*)_ptr - 1;
        
            GCMark expected = Unscanned;
            if (header->obj_status.compare_exchange_strong(expected, Uncomplete)) {
                void** data = (void**)_ptr;
                size_t element_size = header->obj_size / sizeof(void*);

                for (size_t i = 0; i < element_size; i ++) {
                    work_stack.push(data[i]);
                }

                header->obj_status.store(Marked);
            }
        }
    }

    extern "C" void   __gc_collect() {
        std::unique_lock<std::mutex> lock(gc_mutex);

        gc_cv.wait(lock, [] {
            return safepoints == (total_active - 1);
        });

        for (auto* stk: global_stacks) {
            for (void** addr: *stk) {
                if (addr && *addr) __gc_scan(*addr);
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