# SakuraE IR Context 设计

[English Version](context.md)

本文档描述 SakuraE IR 当前已经实现的 Context 机制。内容以现有代码的所有权边界为准，不将结构体前置声明、跨模块命名类型等未实现能力视为既有功能。

## 1. 概览

SakuraE 将类型存储与模块内名称分离：

```text
Program
  拥有 IRContext
    拥有 llvm::LLVMContext
    拥有基础及派生 IR 类型
    拥有 TypeInfoPool
  拥有 Module 对象
    拥有 NamingContext
      拥有 IRStructDecl 对象
        拥有 IRStructType 对象
    拥有 Function 和 IRValue 对象
```

同一个 `Program` 的全部模块共享一个 `IRContext`。每个 `Module` 持有独立的 `NamingContext`，后者构成命名结构体的模块级命名空间边界。

## 2. IRContext

`IRContext` 声明于 `Compiler/IR/context.hpp`，负责拥有不依赖源语言名称的资源：

- 一个 `llvm::LLVMContext`；
- 规范化的基础类型，包括整数、浮点、void、string、block 和 type-info 类型；
- 规范化的派生类型，包括指针、引用、数组和函数类型；
- 一个 `TypeInfoPool`。

各工厂方法在当前 Context 内缓存结果。同一 Context 中对同一类型形状的重复请求会返回同一个 `IRType*`；不同 `IRContext` 的类型缓存和 LLVM Context 相互隔离。

`Program` 先创建 `IRContext`，再创建各模块。其析构函数会先删除模块，之后才销毁 `IRContext` 成员，因此模块拥有的声明和 IR 值不会比 Program 级的类型与 LLVM 资源更早失效。

### 兼容静态工厂

既有的 `IRType::getInt32Ty()`、`TypeInfo::makeBasicTypeID()` 等静态工厂仍作为兼容入口保留，但它们会转发到 `IRContext::current()`。

构造 `IRContext` 时，它会成为线程局部的活动 Context，并保留此前的活动 Context。因此，静态工厂只能在目标 Context 处于活动状态时调用。已经持有 `IRContext&` 的新代码应优先使用实例工厂和 `typeInfoPool()`。

## 3. NamingContext

`NamingContext` 声明于 `context.hpp`，实现在 `context.cpp`。实现文件包含 `model/struct.hpp`，使公开的 Context 头文件只需前置声明 `IRStructDecl`。

一个 `NamingContext` 保存模块标识符，以及从结构体名称到 `IRStructDecl` 的映射，提供：

- `lookupStructDecl(name)`：查询结构体声明；
- `lookupStructType(name)`：查询对应的 `IRStructType`；
- `defineStruct(name, fields, info)`：创建并注册结构体。

同一模块中重复定义同名结构体会抛出 `SakuraError`。不同模块各自拥有 `NamingContext`，即使结构体名称相同，也会得到不同的命名类型。

当前代码中的所有权关系是：

```text
NamingContext -> IRStructDecl -> IRStructType
```

`IRStructDecl` 保存源码声明相关数据，并拥有具体的 `IRStructType`。`IRStructType` 保存字段名、字段类型及字段索引；它不是 `IRValue`。

## 4. TypeInfo 与命名类型

`TypeInfoPool` 由 `IRContext` 拥有，在该 Context 内规范化基础、数组、指针、引用和结构体类型描述。

`TypeID::Struct` 由保存已解析 `IRStructType*` 的 `StructTypeInfo` 表示，取代了旧的无信息自定义类型占位符。因此，结构体 `TypeInfo` 转换回 IR 时会返回精确的命名类型；数组、指针和引用 `TypeInfo` 会递归保留该类型身份。

IR 生成器遇到类型标识符时，会通过当前模块的 `NamingContext` 解析。未定义的名称会产生 IR 生成错误，不会再构造通用占位类型。

## 5. LLVM Context 所有权

LLVM 类型使用 `IRContext` 拥有的 `llvm::LLVMContext` 创建。LLVM Codegen 从 `Program` 获取同一个 Context，不会为同一份 IR 程序另行创建不相关的 LLVM Context。

`IRContext::releaseLLVMContext()` 用于 JIT 交接等需要转移 LLVM Context 所有权的路径。所有权转移后，调用方不得再从该 `IRContext` 请求 LLVM Context。

## 6. 范围与当前限制

当前实现只支持完整定义，不包含“先声明不透明结构体、后补充字段”的独立阶段，因此尚未支持递归结构体定义。命名类型解析限定在模块内部，跨模块类型导入和类型别名不属于当前 Context 功能。

派生类型缓存以非拥有的 `IRType*` 作为键。在正常的 `Program` 生命周期中，该关系有效，因为 Program 结束时会销毁所有模块。类型或 IR 值不能在其所属 `Program` 销毁后继续保留或使用。
