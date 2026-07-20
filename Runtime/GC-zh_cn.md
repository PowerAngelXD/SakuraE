# SakuraE 垃圾回收器

[English Version](GC.md)

## 1. 范围与设计

当前 SakuraE 使用一个简单的单线程、Stop-the-World、非移动式 Mark-Sweep 垃圾回收器GC 只管理通过 `__gc_alloc` 分配的对象，不扫描原生 C++ 栈，因此编译器必须为仍然存活的托管引用显式生成 root

GC 主要负责四项工作：

1. 为 payload 创建 `ObjectHeader` 和对象存储区
2. 通过 `GCTypeInfo` 描述对象中的引用布局
3. 从已注册的 root 槽位开始标记可达对象
4. 清扫未标记对象并更新下一次触发 GC 的分配阈值

GC 不进行内存压缩或对象搬移对象被回收前，其地址保持稳定

## 2. 对象布局

每个托管对象的内存布局如下：

```text
+----------------------+
| ObjectHeader          |
+----------------------+
| payload              |
| payload_size bytes   |
+----------------------+
```

`ObjectHeader` 包含：

- `type_info`：payload 的扫描描述
- `mark`：当前 GC 周期中的标记状态
- `obj_size`：payload 的字节大小
- `elem_count`：独立分配数组的元素数量

`__gc_alloc` 返回的指针指向 payload，而不是 header运行时通过遍历托管堆并检查指针是否落在 payload 范围内，来反查对象 header因此也能识别指向对象内部的 interior pointer不过编译器不会把普通的派生内部地址作为 GC root

完整定义如下：

```cpp
struct ObjectHeader {
    GCTypeInfo* type_info;
    GCMark mark;
    uint64_t obj_size;
    uint64_t elem_count;
};
```

## 3. 类型元数据

### `GCObjectKind`

`GCObjectKind` 决定扫描策略：

- `Atomic`：不包含托管引用
- `Struct`：在指定字节偏移处包含指针字段
- `Array`：包含按照 `GCArrayLayout` 描述的重复元素

### `GCTypeInfo`

`GCTypeInfo` 保存类型名称、对象种类和 `contains_refs` 快速判断标志原子对象可以跳过递归扫描；结构体使用 `struct_layout`，数组使用 `array_layout`

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

`ptr_offsets` 保存托管指针字段相对于嵌入对象起始地址的字节偏移扫描器只读取这些字段，不读取普通标量字段

```cpp
struct GCStructLayout {
    uint32_t ptr_count;
    uint32_t* ptr_offsets;
};
```

### `GCArrayLayout`

`GCArrayLayout` 保存：

- `member_size`：单个元素的字节大小
- `is_ptr`：是否每个元素都是托管指针
- `length`：嵌入式数组的长度
- `member_type`：嵌入式非指针元素的类型元数据

独立分配数组的实际元素数量来自 `ObjectHeader::elem_count`；嵌入式数组的长度来自类型元数据中的 `length`

```cpp
struct GCArrayLayout {
    uint32_t member_size;
    bool is_ptr;
    uint64_t length;
    GCTypeInfo* member_type;
};
```

## 4. Root 管理

GC 使用显式 root 栈root 保存的是指针槽位的地址，而不是指针值的副本收集时，运行时重新读取槽位中的当前值，因此槽位被重新赋值后，GC 仍能得到最新引用

作用域通过 root 栈深度标记实现：

1. `void __gc_enter_scope()` 记录当前 root 深度
2. `void __gc_register(void** addr)` 追加一个槽位地址
3. `void __gc_leave_scope()` 将 root 列表截断到保存的深度
4. `void __gc_pop(uint32_t times)` 从 root 栈尾部移除指定数量的 root

LLVM 后端把 root 槽位放在函数的 entry block 中，使其跨越控制流分支持续有效，并且在后续分配触发 GC 时可以被扫描

`extern "C" void __gc_scan(void* ptr)` 从一个指针开始执行标记，但不会立即清扫Root 和作用域接口必须显式调用，因为当前 GC 不扫描原生 C++ 栈帧

## 5. GC 执行流程

`extern "C" void __gc_collect()` 执行一次 Stop-the-World Mark-Sweep：

```text
新分配对象初始为未标记
遍历所有 root 槽位，从每个 root 开始 DFS 标记
遍历 global_heap：
    已标记对象清除标记，留待下一轮
    未标记对象释放 header 和 payload
重新计算分配阈值
```

`extern "C" void __gc_scan_unlocked(void* root)` 实现 DFS 工作循环：将 root 放入栈中，查找其所属 header，跳过已经标记的对象，标记当前对象，然后通过类型扫描器把发现的子引用压入工作栈

所有对象都保存在 `global_heap` 中地址查找和清扫均为线性操作，设计目标是保持当前单线程运行时的实现简单，而不是支持大规模堆或低暂停时间的生产级 GC

## 6. 扫描函数

### `__gc_scan_struct`

根据 `GCStructLayout::ptr_offsets` 扫描结构体中的指针字段，并通过 visitor 回调提交非空子指针

```cpp
extern "C" void __gc_scan_struct(
    void* obj, GCStructLayout* layout,
    void (*visit)(void*, void*), void* context);
```

### `__gc_scan_embedded`

扫描嵌入在其他对象内部的值原子类型直接跳过；结构体委托给 `__gc_scan_struct`；数组根据元数据长度遍历，并递归处理指针元素或嵌入式元素

```cpp
extern "C" void __gc_scan_embedded(
    void* mem, GCTypeInfo* ty,
    void (*visit)(void*, void*), void* context);
```

### `__gc_scan_array`

扫描独立分配的数组它使用 `header->elem_count`，并验证计算出的元素范围不超过 `header->obj_size`指针数组直接访问每个元素；非指针数组在 `member_type` 含有引用时继续递归扫描

```cpp
extern "C" void __gc_scan_array(
    void* obj, ObjectHeader* header, GCArrayLayout* layout,
    void (*visit)(void*, void*), void* context);
```

### `__gc_scan_object`

根据 `header->type_info->kind` 分派到对应扫描器缺少类型元数据或 `contains_refs == false` 的对象不会递归扫描

```cpp
extern "C" void __gc_scan_object(
    void* obj, ObjectHeader* header,
    void (*visit)(void*, void*), void* context);
```

### `__gc_get_unlocked`

根据 payload 或 interior pointer 查找其所属 header名称中的 `unlocked` 表示当前实现没有锁，也没有线程同步逻辑

```cpp
extern "C" ObjectHeader* __gc_get_unlocked(void* payload);
```

## 7. 分配与元数据接口

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
```

`__gc_get_atomic_type` 返回共享的原子类型元数据两个数组接口描述元素大小和指针/引用属性，带长度版本还描述嵌入式数组的长度`__gc_get_struct_type` 将调用方传入的指针偏移复制到缓存元数据中`__gc_alloc` 将 payload 清零，在前方创建 `ObjectHeader`，并将对象加入 `global_heap`

```cpp
extern "C" void __gc_create_thread();
extern "C" void __gc_destroy_thread();
extern "C" void __gc_safe_point();
```

`__gc_create_thread`、`__gc_destroy_thread` 和 `__gc_safe_point` 是 ABI 兼容桩函数由于当前 GC 是单线程实现，且只在分配路径上触发收集，这些函数有意不执行实际操作

类型缓存使用字符串 key，并将类型名称保存到 `type_name_pool`，保证 `name` 指针在运行时结束前稳定`GCCleaner::~GCCleaner()` 在静态析构阶段释放堆对象和缓存的类型元数据

## 8. 编译器与 LLVM 集成

LLVM 后端通过三种方式接入 GC：

1. 在 IR runtime module 中声明运行时函数
2. 生成对 `__gc_alloc`、作用域函数和 root 注册函数的调用
3. `atrI` JIT 将 C++ 运行时函数注册为 absolute symbols

`isManagedHeapType` 将 string 和 array 识别为托管堆类型参数和这些类型的局部槽位会注册为 root调用参数、数组元素、调用结果和字符串常量等临时值，会在后续可能发生分配前写入 entry block 中的 root 槽位

LLVM 数组类型会被转换为 `GCArrayLayout` 元数据元素字节大小和嵌入式数组长度传给 `__gc_get_array_type_with_length`；独立数组对象的运行时元素数量传给 `__gc_alloc`

### 完整联动案例

考虑下面的 SakuraE 程序：

```sak
func main() -> i32 {
    let words = ["gc", "safe"];
    let result = concat_string(words[0], words[1]);
    __println(result);
    return 0;
}
```

完整数据流如下：

1. 前端生成数组字面量 IR 指令和字符串常量 IR 值
2. `LLVMCodeGenerator::toLLVMConstant(IR::Constant*, LLVMFunction*)` 为每个字符串字面量调用 `create_string`返回指针通过 `LLVMFunction::createRootedTemporary(llvm::Value*, const fzlib::String&)` 写入 entry block 的 alloca，并通过 `LLVMFunction::gcRegisterRoot(llvm::Value*)` 注册
3. `LLVMCodeGenerator::instgen(IR::Instruction*, LLVMFunction*)` 处理 `IR::OpKind::create_array`数组分配前，每个托管元素先从 root 槽加载，保证构造数组期间发生分配时字符串仍然存活
4. `LLVMModule::llvmTy2GCType(llvm::Type*)` 将 LLVM 数组类型转换为 GC 元数据，并调用 `LLVMModule::getArrayGCType(bool, uint32_t, llvm::Value*, uint64_t)`，后者生成对 `__gc_get_array_type_with_length(bool, uint32_t, uint64_t, GCTypeInfo*)` 的调用
5. `LLVMFunction::createHeapAlloc(llvm::Type*, llvm::Value*, llvm::Value*)` 计算 payload 大小并生成 `__gc_alloc(size_t, GCTypeInfo*, uint64_t)`数组指针随后保存到另一个 root 槽
6. 执行 `concat_string(words[0], words[1])` 时，`instgen` 在生成调用前用 `createRootedTemporary` 暂存托管参数运行时 `concat_string(const char*, const char*)` 也会在分配结果前将两个 C 指针注册到临时 runtime scope
7. 调用结果保存在 entry block 的 root 槽中如果后续分配超过 GC 阈值，`__gc_collect()` 会标记 `words`、数组引用到的两个字符串以及 `result`，然后只清扫不可达对象
8. 生成的 return 指令中，`LLVMFunction::gcLeaveAllScopes()` 生成所需的 `__gc_leave_scope()` 调用

概念上的关键 LLVM 调用如下：

```llvm
%gc_ty = call ptr @__gc_get_array_type_with_length(i1 false,
    i32 8, i64 2, ptr %atomic_ty)
%array = call ptr @__gc_alloc(i64 64, ptr %gc_ty, i64 2)
call void @__gc_register(ptr %array_root_slot)
%result = call ptr @concat_string(ptr %word0, ptr %word1)
call void @__gc_register(ptr %result_root_slot)
```

具体 LLVM 临时变量名称和 payload 大小取决于目标数据布局，但所有权顺序固定：生成元数据、保护仍存活的输入、执行分配、写入引用，最后保护新生成的托管对象

## 9. 字符串与 GC 所有权

`create_string` 和 `concat_string` 使用 `__gc_alloc` 并以原子类型元数据分配字符串`free_string` 作为 ABI 兼容接口保留，但实际为空操作，因为字符串的所有权属于 GC`concat_string` 在分配结果前暂时 root 化两个输入指针，防止本次分配触发 GC 时输入对象被回收

## 10. 限制与不变量

- GC 是单线程、Stop-the-World 的实现
- 原生 C++ 栈变量不会被自动扫描
- 可能触发 GC 的分配之前，仍需保留的对象必须已经注册为 root
- GC 不移动对象，存活对象的地址不会改变
- `GCTypeInfo` 必须准确描述所有托管引用
- 错误的布局元数据会被运行时边界检查拒绝或跳过
- 堆查找和清扫是线性的，大量对象时成本会增加

最重要的正确性不变量是：任何需要跨越一次 GC 存活的托管对象，都必须能从已注册 root 槽位出发，或通过有效类型元数据描述的另一存活对象引用到达
