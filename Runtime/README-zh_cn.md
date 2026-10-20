# SakuraE 运行时库

[English Version](README.md)

Runtime 为生成代码提供 GC payload 分配、装箱值、字符串、I/O、运行时错误和不可变类型元数据。

## 接口

- [`gc.h`](gc.h)：payload 分配、扫描、root、作用域和收集；详见 [GC 设计](GC-zh_cn.md)。
- [`value.h`](value.h)：`RuntimeValue`、`RuntimeType` 标签及 `RawValue`。生成的语言调用传递 `RuntimeValue*`；托管数组当前使用指针 payload。
- [`rttype.h`](rttype.h)：运行时类型元数据对象 `RuntimeTypeInfo`。`__runtime_type_info_basic`、`_pointer`、`_reference`、`_array`、`_struct` 工厂驻留不可变元数据，C 符号名称保持不变。
- [`raw_string.h`](raw_string.h)：`create_string(const char*)`、`concat_string(RuntimeValue*, RuntimeValue*)`，以及兼容的空操作 `free_string(RuntimeValue*)`；字符 payload 由 GC 管理。
- [`print.h`](print.h)：`print(RuntimeValue*)` 和 `println(RuntimeValue*)`。
- [`alloc.h`](alloc.h)：原始内存分配接口 `__alloc` / `__free`。
- [`errors.h`](errors.h)：运行时错误状态和数组边界检查。

`IR::TypeInfo` 表达源语言语义类型，`IR::IRType` 表达 storage 类型，`runtime::RuntimeTypeInfo` 表达运行时类型元数据。`RuntimeType::TypeInfo` 只是装箱值标签，不是第四套类型体系；Runtime 工厂不接收编译器类型对象。

CLI 向 JIT 注册 C ABI 符号；Runtime 实现链接到编译器可执行文件，LLVM 生成阶段发出对这些符号的调用。`typeof` 代码生成当前暂缓实现。
