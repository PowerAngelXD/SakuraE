#include "gc.h"

#include <list>
#include <unordered_map>

namespace sakuraE::runtime {
    // GC 状态控制
    std::atomic<bool> need_gc {false};
    std::atomic<int>  total_active {0};
    std::atomic<int>  safepoints {0};
    std::condition_variable gc_cv;
    std::condition_variable resume_cv;

    // 内存分配与根集合
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

    // 复杂类型缓存与名字池
    std::map<fzlib::String, GCTypeInfo*> complexGCTypePool;
    std::list<fzlib::String> type_name_pool;
    std::mutex type_pool_mutex;

    // payload 到对象头的索引
    std::unordered_map<void*, ObjectHeader*> global_heap_index;

    // 获取数组类型信息并缓存
    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
        if (!mem_ty) return nullptr;
        fzlib::String id = std::to_string(is_ptr) + std::to_string(size) + mem_ty->name;

        std::lock_guard<std::mutex> lock(type_pool_mutex);
        if (complexGCTypePool.contains(id)) return complexGCTypePool[id];

        type_name_pool.push_back(id);
        const char* name = type_name_pool.back().c_str();

        complexGCTypePool[id] = new GCTypeInfo {
            name,
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

    // 获取原子类型信息
    extern "C" GCTypeInfo* __gc_get_atomic_type() {
        return &GC_ATOMIC_TYPE;
    }

    // 根据 payload 快速查找对象头
    extern "C" ObjectHeader* __gc_get_unlocked(void* payload) {
        auto it = global_heap_index.find(payload);
        if (it == global_heap_index.end()) return nullptr;
        return it->second;
    }

    // 工作栈入栈回调
    extern "C" void __gc_wklist_push(void* obj, void* context) {
        if (!obj) {
            return;
        }
        auto* work = static_cast<std::stack<void*>*>(context);
        work->push(obj);
    }

    // 扫描结构体中的指针字段
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

    // 扫描内嵌对象的数据区域
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
                ObjectHeader* header = __gc_get_unlocked(mem);
                if (!header) return;
                __gc_scan_array(mem, header, header->type_info->array_layout, visit, ctx);
                return;
            }
        }
    }

    // 扫描数组元素
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

    // 扫描对象本体
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

    // 从根出发做 DFS 标记
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

    // 注册当前线程的根集合
    extern "C" void   __gc_create_thread() {
        std::lock_guard<std::mutex> lock(gc_mutex);
        
        if (!is_registered) {
            global_stacks.push_back(&own_stack);
            is_registered = true;
            total_active.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 注销当前线程
    extern "C" void   __gc_destroy_thread() {
        std::lock_guard<std::mutex> lock(gc_mutex);

        if (!is_registered) return;

        auto it = std::find(global_stacks.begin(), global_stacks.end(), &own_stack);
        if (it != global_stacks.end()) {
            global_stacks.erase(it);
        }
        own_stack.clear();
        is_registered = false;
        total_active.fetch_sub(1, std::memory_order_relaxed);
    }

    // 到达安全点等待 GC
    extern "C" void   __gc_safe_point() {
        if (need_gc.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(gc_mutex);
            safepoints ++;

            gc_cv.notify_one();

            resume_cv.wait(lock, [] { return !need_gc.load(); });

            safepoints --;
        }
    }

    // 分配对象并写入对象头
    extern "C" void*  __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t mem_count) {
        const size_t total_size = size + sizeof(ObjectHeader);
        if (allocated_bytes.load(std::memory_order_relaxed) + total_size > limit) {
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

        void* payload = (void*)(header + 1);

        std::lock_guard<std::mutex> lock(gc_mutex);
        global_heap.push_back(header);
        global_heap_index.emplace(payload, header);
        allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);

        return payload;
    }

    // 注册根变量地址
    extern "C" void   __gc_register(void** addr) {
        own_stack.push_back(addr);
    }

    // 弹出指定数量的根
    extern "C" void   __gc_pop(uint32_t times) {
        for (uint32_t i = 0; i < times; i ++) {
            if (!own_stack.empty()) own_stack.pop_back();
        }
    }

    // 扫描指定根
    extern "C" void   __gc_scan(void* ptr) {
        std::lock_guard<std::mutex> lock(gc_mutex);
        __gc_scan_unlocked(ptr);
    }

    // 停世界标记清除
    extern "C" void   __gc_collect() {
        std::unique_lock<std::mutex> lock(gc_mutex);

        if (total_active.load(std::memory_order_relaxed) > 1) {
            gc_cv.wait(lock, [] {
                return safepoints == (total_active - 1);
            });
        }

        for (auto* stk: global_stacks) {
            for (void** addr: *stk) {
                if (addr && *addr) __gc_scan_unlocked(*addr);
            }
        }

        auto it = global_heap.begin();
        while (it != global_heap.end()) {
            ObjectHeader* header = *it;
            if (header->obj_status == Unscanned) {
                const size_t total_size = header->obj_size + sizeof(ObjectHeader);
                allocated_bytes.fetch_sub(total_size, std::memory_order_relaxed);
                global_heap_index.erase(static_cast<void*>(header + 1));
                free(header);
                it = global_heap.erase(it);
            }
            else {
                header->obj_status = Unscanned;
                it ++;
            }
        }

        if (allocated_bytes.load(std::memory_order_relaxed) > limit * 0.7) {
            limit *= 2;
        }

        need_gc = false;
        resume_cv.notify_all();
    }


    // 进程结束时释放资源
    struct GCCleaner {
        ~GCCleaner() {
            for (auto* header : global_heap) {
                free(header);
            }
            global_heap.clear();
            global_heap_index.clear();
            complexGCTypePool.clear();
            type_name_pool.clear();
        }
    };
    static GCCleaner cleaner;
}