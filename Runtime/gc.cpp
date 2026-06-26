#include "gc.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <stack>
#include <unordered_map>
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
        // 单线程运行时只维护一套显式根栈。
        // codegen 注册进来的不是“对象指针本身”，而是“保存对象指针的槽位地址”。
        std::vector<void**> global_roots;

        // 每次进入词法作用域时，记录当前根栈深度；
        // 离开作用域时直接回退到这个深度即可。
        std::vector<size_t> scope_markers;

        // 所有 GC 管理的对象都会进入堆列表，直到 sweep 把它删除。
        std::vector<ObjectHeader*> global_heap;

        // 生成代码只知道 payload 指针，GC 通过这张索引表找到对象头。
        std::unordered_map<void*, ObjectHeader*> global_heap_index;

        // 复杂类型描述符（如数组）做缓存，避免重复分配。
        std::map<fzlib::String, GCTypeInfo*> complexGCTypePool;
        std::list<fzlib::String> type_name_pool;

        // 防止在 GC 执行过程中再次递归触发回收。
        bool gc_collecting = false;
    }

    extern "C" GCTypeInfo* __gc_get_array_type(bool is_ptr, uint32_t size, GCTypeInfo* mem_ty) {
        if (!mem_ty) {
            return nullptr;
        }

        // 用元素形态拼出缓存键，复用同一种数组类型的描述符。
        fzlib::String id = std::to_string(is_ptr) + std::to_string(size) + mem_ty->name;
        if (complexGCTypePool.contains(id)) {
            return complexGCTypePool[id];
        }

        type_name_pool.push_back(id);
        const char* name = type_name_pool.back().c_str();

        // 只要元素本身是指针，或者元素内部仍包含引用，
        // 整个数组在标记阶段就必须继续扫描。
        bool contains_refs = is_ptr || mem_ty->contains_refs;

        auto* type_info = new GCTypeInfo {
            name,
            GCObjectKind::Array,
            contains_refs,
            nullptr,
            new GCArrayLayout {
                size,
                is_ptr,
                mem_ty
            }
        };

        complexGCTypePool[id] = type_info;
        return type_info;
    }

    // 原子值内部没有出边引用，因此全局只需要一个共享描述符。
    extern "C" GCTypeInfo* __gc_get_atomic_type() {
        return &GC_ATOMIC_TYPE;
    }

    // 运行时对外暴露的是 payload 指针；
    // 真正的 GC 元数据在对象头里，通过索引表做一次反查。
    extern "C" ObjectHeader* __gc_get_unlocked(void* payload) {
        auto it = global_heap_index.find(payload);
        if (it == global_heap_index.end()) {
            return nullptr;
        }
        return it->second;
    }

    // 标记阶段采用显式工作栈，避免递归扫描导致调用栈过深。
    extern "C" void __gc_wklist_push(void* obj, void* context) {
        if (!obj) {
            return;
        }

        auto* work = static_cast<std::stack<void*>*>(context);
        work->push(obj);
    }

    // 结构体扫描依赖预先计算好的“指针字段偏移表”。
    // 对每个偏移位置读取一个子指针，再交给 visit 继续处理。
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

    // 内嵌值本身不一定是独立堆对象，但它内部的字段依然可能引用堆对象。
    // 因此这里根据类型描述继续向下扫描。
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
                if (!header) {
                    return;
                }
                __gc_scan_array(mem, header, header->type_info->array_layout, visit, ctx);
                return;
            }
        }
    }

    // 数组元素要么直接就是指针，要么是“内部还含引用”的内嵌值。
    // 这两种情况需要分别处理。
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

    // 根据对象头上的运行时类型信息，分发到对应的扫描路径。
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

    // 从一个根对象出发做 DFS 标记，把所有可达对象都染成 Marked。
    extern "C" void __gc_scan_unlocked(void* root) {
        if (!root) {
            return;
        }

        std::stack<void*> work_stack;
        work_stack.push(root);

        while (!work_stack.empty()) {
            // 取出当前待处理对象。
            void* obj = work_stack.top();
            work_stack.pop();

            if (!obj) {
                continue;
            }

            // 先从 payload 反查对象头；查不到说明它不是 GC 管理对象。
            ObjectHeader* header = __gc_get_unlocked(obj);
            if (!header || header->obj_status == Marked) {
                continue;
            }

            // 先标记自己，再把它内部可达的对象压入工作栈。
            header->obj_status = Marked;
            __gc_scan_object(obj, header, __gc_wklist_push, &work_stack);
        }
    }

    extern "C" void __gc_create_thread() {
        // 当前实现是单线程版本，此接口仅用于兼容旧 ABI。
    }

    extern "C" void __gc_destroy_thread() {
        // 当前实现是单线程版本，此接口仅用于兼容旧 ABI。
    }

    // 记录进入当前作用域前的根栈深度。
    extern "C" void __gc_enter_scope() {
        scope_markers.push_back(global_roots.size());
    }

    extern "C" void __gc_leave_scope() {
        if (scope_markers.empty()) {
            return;
        }

        // 直接回退到进入作用域前的深度，丢弃本作用域新增的根。
        size_t marker = scope_markers.back();
        scope_markers.pop_back();
        global_roots.resize(marker);
    }

    extern "C" void __gc_safe_point() {
        // 单线程版本在分配路径上同步回收，这里不需要额外逻辑。
    }

    // 分配时申请“对象头 + payload”的整块内存，并把 payload 清零。
    // 这样扫描器不会把未初始化字节误判成垃圾指针。
    extern "C" void* __gc_alloc(size_t size, GCTypeInfo* ty, uint64_t mem_count) {
        const size_t total_size = sizeof(ObjectHeader) + size;

        // 超过阈值时先尝试做一轮同步回收，再继续分配。
        if (!gc_collecting && allocated_bytes + total_size > limit) {
            __gc_collect();
        }

        auto* header = static_cast<ObjectHeader*>(std::malloc(total_size));
        if (!header) {
            std::fprintf(stderr, "[Runtime Error] Out of memory in __gc_alloc\n");
            std::exit(1);
        }

        header->obj_size = size;
        header->obj_status = Unscanned;
        header->elem_count = mem_count;
        header->type_info = ty ? ty : &GC_ATOMIC_TYPE;

        // header 后面紧跟的那段内存就是返回给生成代码的 payload。
        void* payload = static_cast<void*>(header + 1);
        std::memset(payload, 0, size);

        // 把新对象登记进堆集合与反查索引，供后续 mark / sweep 使用。
        global_heap.push_back(header);
        global_heap_index.emplace(payload, header);
        allocated_bytes += total_size;

        // 如果分配后仍然长期顶到上限，就放宽后续 GC 阈值。
        if (allocated_bytes > limit) {
            limit = allocated_bytes * 2;
        }

        return payload;
    }

    // codegen 注册的是“局部槽位地址”，而不是槽位中的指针值。
    // 这样变量被重新赋值后，GC 仍能读到该槽位里的最新对象指针。
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

    // 单线程 mark-sweep：
    // 1. 从显式根集合出发，标记所有可达对象
    // 2. 线性扫描堆列表，把不可达对象逐步清理掉
    //
    // 当前版本对不可达对象采用“两轮淘汰”：
    // - 第一次没被标到：Unscanned -> Uncomplete
    // - 第二次还没被标到：Uncomplete -> 真正释放
    //
    // 这是一种过渡策略：当前仍有少量表达式临时值没有被 codegen
    // 显式注册进根栈，多保留一个回收周期可以降低误回收风险。
    extern "C" void __gc_collect() {
        if (gc_collecting) {
            return;
        }

        gc_collecting = true;

        // 先从所有显式根开始做一轮完整标记。
        for (void** addr : global_roots) {
            if (addr && *addr) {
                __gc_scan_unlocked(*addr);
            }
        }

        auto it = global_heap.begin();
        while (it != global_heap.end()) {
            ObjectHeader* header = *it;

            if (header->obj_status == Marked) {
                // 本轮仍然可达，恢复成下一轮 GC 的初始状态。
                header->obj_status = Unscanned;
                ++it;
                continue;
            }

            if (header->obj_status == Uncomplete) {
                // 连续两轮都不可达，才真正释放对象。
                allocated_bytes -= header->obj_size + sizeof(ObjectHeader);
                global_heap_index.erase(static_cast<void*>(header + 1));
                std::free(header);
                it = global_heap.erase(it);
                continue;
            }

            // 第一次发现不可达，先标成“待回收”状态，给临时值一个缓冲周期。
            header->obj_status = Uncomplete;
            ++it;
        }

        if (allocated_bytes > limit * 7 / 10) {
            limit *= 2;
        }

        gc_collecting = false;
    }

    // 进程退出时清理剩余堆对象与类型描述缓存。
    struct GCCleaner {
        ~GCCleaner() {
            for (auto* header : global_heap) {
                std::free(header);
            }
            global_heap.clear();
            global_heap_index.clear();
            global_roots.clear();
            scope_markers.clear();

            for (auto& [_, ty] : complexGCTypePool) {
                if (!ty) {
                    continue;
                }

                delete ty->array_layout;
                delete ty->struct_layout;
                delete ty;
            }

            complexGCTypePool.clear();
            type_name_pool.clear();
        }
    };

    static GCCleaner cleaner;
}
