# SakuraE Garbage Collector

[Chinese Version](GC-zh_cn.md)

## 1. Scope and Design

SakuraE currently uses a simple single-threaded, stop-the-world, non-moving mark-sweep garbage collector. It manages objects allocated through `__gc_alloc` and does not scan the native C++ stack. The compiler therefore emits explicit roots for live managed references.

The collector has four main responsibilities:

1. Allocate a payload together with an `ObjectHeader`.
2. Describe how each object contains references through `GCTypeInfo`.
3. Mark objects reachable from registered root slots.
4. Sweep unmarked objects and refresh the allocation threshold.

The collector does not compact memory. Object addresses remain stable until the object is reclaimed.

## 2. Object Layout

Every managed allocation has this layout:

```text
+----------------------+
| ObjectHeader          |
+----------------------+
| payload              |
| payload_size bytes   |
+----------------------+
```

`ObjectHeader` contains:

- `type_info`: the scanning description for the payload.
- `mark`: the mark state for the current collection cycle.
- `obj_size`: payload size in bytes.
- `elem_count`: element count for independently allocated arrays.

The complete runtime definition is:

```cpp
struct ObjectHeader {
    GCTypeInfo* type_info;
    GCMark mark;
    uint64_t obj_size;
    uint64_t elem_count;
};
```

The public pointer returned by `__gc_alloc` points to the payload. Runtime code locates the header by scanning the managed heap and checking whether a pointer lies inside a payload range. This also recognizes interior pointers, although the compiler deliberately does not treat derived interior addresses as ordinary roots.

## 3. Type Metadata

### `GCObjectKind`

`GCObjectKind` selects the scanning algorithm:

- `Atomic`: contains no managed references.
- `Struct`: contains pointers at declared byte offsets.
- `Array`: contains repeated elements described by `GCArrayLayout`.

### `GCTypeInfo`

`GCTypeInfo` stores the object name, object kind, and the `contains_refs` fast-path flag. Atomic objects can skip recursive scanning. Structs use `struct_layout`; arrays use `array_layout`.

```cpp
struct GCTypeInfo {
    const char* name;
    GCObjectKind kind;
    bool contains_refs;
    GCStructLayout* struct_layout;
    GCArrayLayout* array_layout;
};
```

### `GCStructLayout`

`ptr_offsets` contains the byte offsets of managed pointer fields relative to the embedded object's beginning. The scanner reads only these fields and ignores scalar data.

```cpp
struct GCStructLayout {
    uint32_t ptr_count;
    uint32_t* ptr_offsets;
};
```

### `GCArrayLayout`

`GCArrayLayout` stores:

- `member_size`: size of one element in bytes.
- `is_ptr`: whether every element is a managed pointer.
- `length`: length for an embedded array.
- `member_type`: metadata for an embedded non-pointer element.

For an independently allocated array, the actual element count comes from `ObjectHeader::elem_count`. For an embedded array, the type metadata supplies `length`.

```cpp
struct GCArrayLayout {
    uint32_t member_size;
    bool is_ptr;
    uint64_t length;
    GCTypeInfo* member_type;
};
```

## 4. Root Management

The collector uses an explicit root stack. A root is the address of a pointer slot, not a copied pointer value. During collection the runtime reads the current value from each slot, so assignments to the slot remain visible to the collector.

Scopes are represented by markers containing the current root-stack depth:

1. `void __gc_enter_scope()` records the current depth.
2. `void __gc_register(void** addr)` appends a slot address.
3. `void __gc_leave_scope()` truncates the root list to the saved depth.
4. `void __gc_pop(uint32_t times)` removes a specified number of roots from the end.

The generated LLVM code places root slots in the function entry block. This keeps their storage valid across control-flow branches and makes them available when a later allocation triggers collection.

`extern "C" void __gc_scan(void* ptr)` performs marking from one pointer without immediately sweeping. The scope and root APIs are explicit because the collector does not scan native C++ stack frames.

## 5. Collection Algorithm

`extern "C" void __gc_collect()` performs a stop-the-world mark-sweep cycle:

```text
mark = false for newly allocated objects
for each registered root slot:
    mark reachable objects using DFS
for each object in global_heap:
    if marked:
        clear mark for the next cycle
    else:
        free header and payload
recalculate the allocation limit
```

`extern "C" void __gc_scan_unlocked(void* root)` implements the DFS work loop. It pushes the root onto a stack, resolves the owning header, skips already marked objects, marks the current object, and pushes references discovered by the type-specific scanner.

The collector uses `global_heap`, a vector of all allocated headers. Address lookup and sweeping are linear. This is intentionally simple and suitable for the current single-threaded runtime, but it is not designed for large heaps or low-pause production workloads.

## 6. Scanning Functions

### `__gc_scan_struct`

```cpp
extern "C" void __gc_scan_struct(
    void* obj, GCStructLayout* layout,
    void (*visit)(void*, void*), void* context);
```

Scans pointer fields listed by `GCStructLayout::ptr_offsets` and passes non-null child pointers to the visitor callback.

### `__gc_scan_embedded`

```cpp
extern "C" void __gc_scan_embedded(
    void* mem, GCTypeInfo* ty,
    void (*visit)(void*, void*), void* context);
```

Scans a value embedded inside another object. Atomic values are ignored. Struct values delegate to `__gc_scan_struct`; array values iterate through their metadata-defined length and recursively scan pointer or embedded elements.

### `__gc_scan_array`

```cpp
extern "C" void __gc_scan_array(
    void* obj, ObjectHeader* header, GCArrayLayout* layout,
    void (*visit)(void*, void*), void* context);
```

Scans an independently allocated array. It uses `header->elem_count` and validates that the calculated element range fits inside `header->obj_size`. Pointer arrays visit each element directly. Non-pointer arrays recurse into `member_type` when that type contains references.

### `__gc_scan_object`

```cpp
extern "C" void __gc_scan_object(
    void* obj, ObjectHeader* header,
    void (*visit)(void*, void*), void* context);
```

Dispatches to the scanner selected by `header->type_info->kind`. Objects with missing metadata or `contains_refs == false` are not recursively scanned.

### `__gc_get_unlocked`

```cpp
extern "C" ObjectHeader* __gc_get_unlocked(void* payload);
```

Finds the header owning a payload or interior address. The function is named `unlocked` because the current collector has no locking or thread synchronization. `extern "C" void __gc_wklist_push(void* obj, void* context)` is the visitor used by the DFS mark loop. `GCCleaner::~GCCleaner()` releases remaining heap objects and cached metadata at process shutdown.

## 7. Allocation and Metadata APIs

```cpp
extern "C" GCTypeInfo* __gc_get_atomic_type();
extern "C" GCTypeInfo* __gc_get_array_type(
    bool is_ptr, uint32_t size, GCTypeInfo* mem_ty);
extern "C" GCTypeInfo* __gc_get_array_type_with_length(
    bool is_ptr, uint32_t size, uint64_t length, GCTypeInfo* mem_ty);
extern "C" GCTypeInfo* __gc_get_struct_type(
    const char* name, uint32_t ptr_count, const uint32_t* ptr_offsets);
extern "C" void* __gc_alloc(
    size_t size, GCTypeInfo* ty, uint64_t member_count = 0);
extern "C" void __gc_create_thread();
extern "C" void __gc_destroy_thread();
extern "C" void __gc_safe_point();
```

The metadata functions create or retrieve cached type descriptions. `__gc_alloc` zero-initializes a payload, prepends an `ObjectHeader`, and adds the object to `global_heap`. The three thread/safe-point functions are ABI compatibility stubs and intentionally do nothing in the current single-threaded collector.

The type cache uses a string key and stores type names in `type_name_pool` so the `name` pointers remain stable until runtime shutdown. `GCCleaner` releases heap objects and cached type metadata during static destruction.

## 8. Compiler and LLVM Integration

The LLVM backend integrates with the runtime in three ways:

1. It declares runtime functions in the IR runtime module.
2. It emits calls to `__gc_alloc`, scope functions, and root registration functions.
3. The `atrI` JIT registers the C++ runtime functions as absolute symbols.

Managed strings and arrays are recognized by `isManagedHeapType`. Parameters and local allocation slots of these types are registered as roots. Temporary results used as call arguments, array elements, call results, and string constants are spilled into entry-block allocas before later allocations can occur.

Array LLVM types are converted into `GCArrayLayout` metadata. The element byte size and embedded array length are passed to `__gc_get_array_type_with_length`; independently allocated array objects pass their runtime element count to `__gc_alloc`.

### Complete integration example

Consider this SakuraE program:

```sak
func main() -> i32 {
    let words = ["gc", "safe"];
    let result = concat_string(words[0], words[1]);
    println(result);
    return 0;
}
```

The complete data flow is:

1. The frontend creates an array literal IR instruction and string constant IR values.
2. `LLVMCodeGenerator::toLLVMConstant(IR::Constant*, LLVMFunction*)` calls `create_string` for each string literal. Each returned pointer is spilled by `LLVMFunction::createRootedTemporary(llvm::Value*, const fzlib::String&)` into an entry-block alloca and registered through `LLVMFunction::gcRegisterRoot(llvm::Value*)`.
3. `LLVMCodeGenerator::instgen(IR::Instruction*, LLVMFunction*)` handles `IR::OpKind::create_array`. Before allocating the array, each managed element is loaded from its root slot, so the element strings survive any allocation performed during array construction.
4. `LLVMModule::llvmTy2GCType(llvm::Type*)` converts the LLVM array type to metadata. It calls `LLVMModule::getArrayGCType(bool, uint32_t, llvm::Value*, uint64_t)`, which emits a call to `__gc_get_array_type_with_length(bool, uint32_t, uint64_t, GCTypeInfo*)`.
5. `LLVMFunction::createHeapAlloc(llvm::Type*, llvm::Value*, llvm::Value*)` computes the payload size and emits `__gc_alloc(size_t, GCTypeInfo*, uint64_t)`. The resulting array pointer is stored in another root slot.
6. For `concat_string(words[0], words[1])`, `instgen` spills managed call arguments with `createRootedTemporary` before emitting the call. The runtime `concat_string(const char*, const char*)` also roots both C pointers in a temporary runtime scope before allocating the result.
7. The call result is protected in an entry-block root slot. If a later allocation crosses the GC threshold, `__gc_collect()` marks `words`, both strings reachable from the array, and `result`, then sweeps only unreachable objects.
8. At the generated return instruction, `LLVMFunction::gcLeaveAllScopes()` emits the required `__gc_leave_scope()` calls.

Conceptually, the relevant LLVM calls are:

```llvm
%gc_ty = call ptr @__gc_get_array_type_with_length(i1 false,
    i32 8, i64 2, ptr %atomic_ty)
%array = call ptr @__gc_alloc(i64 64, ptr %gc_ty, i64 2)
call void @__gc_register(ptr %array_root_slot)
%result = call ptr @concat_string(ptr %word0, ptr %word1)
call void @__gc_register(ptr %result_root_slot)
```

The exact LLVM temporary names and payload size depend on the target data layout, but the ownership sequence is fixed: create metadata, root live inputs, allocate, store references, and root the resulting managed object.

## 9. Strings and GC Ownership

`create_string` and `concat_string` allocate strings through `__gc_alloc` with atomic metadata. `free_string` is retained as an ABI-compatible no-op because ownership belongs to the collector. `concat_string` temporarily roots both input pointers while allocating the result, since that allocation may trigger collection.

## 10. Limitations and Invariants

- The collector is single-threaded and stop-the-world.
- Native C++ stack variables are not automatically scanned.
- Roots must be registered before an allocation that can trigger collection.
- The collector is non-moving; live object addresses do not change.
- `GCTypeInfo` must accurately describe every managed reference.
- Invalid layout metadata is rejected or skipped by runtime bounds checks.
- Heap lookup and sweep are linear and may become expensive for large object counts.

The most important correctness invariant is: every managed object that must survive a collection must be reachable from a registered root slot or from another marked object through valid type metadata.
