# SakuraE IR Context 设计

[English Version](context.md)

本文档描述 SakuraE IR 当前已经实现的 Context 机制。结构体前置声明与补全能力指 Context/Lowering 层接口，不代表源语言语法和生成器已完整支持。

## 1. 概览

SakuraE 将类型存储与模块内名称分离：

```text
Program
  拥有 IRContext
    拥有基础及派生 IR 类型
    拥有 TypeInfoPool
  拥有 Module 对象
    拥有 NamingContext
      拥有 IRStructDecl 对象
      拥有按 StructDeclId 驻留的 IRStructType
    拥有 Function 和 IRValue 对象
  拥有 LoweringSession
    拥有 BackendModule 对象
      拥有 BackendFunction 描述
```

同一个 `Program` 的全部模块共享一个 `IRContext`。每个 `Module` 持有独立的 `NamingContext`，后者构成命名结构体的模块级命名空间边界。

## 2. IRContext

`IRContext` 声明于 `../Backend/context/context.hpp`，负责拥有 Program 级语义类型与 storage 类型：

- 规范化的基础类型，包括整数、浮点、void、string、block 和 type-info 类型；
- 规范化的派生类型，包括指针、引用、数组和函数类型；
- 一个 `TypeInfoPool`。

各工厂方法在当前 Context 内缓存结果。同一类型形状重复请求返回同一个 `IRType*`；不同 Program 的类型缓存独立。LLVM Context 由 emitter 的调用方单独提供。

`Program` 先创建 IRContext，再创建模块；销毁时先销毁 Session，再销毁模块，最后销毁 IRContext。Backend 描述不能比所引用的源 IR 与 storage 类型存活更久。

### 兼容静态工厂

既有的 `IRType::getInt32Ty()`、`TypeInfo::makeBasicTypeID()` 等静态工厂仍作为兼容入口保留，但它们会转发到 `IRContext::current()`。

构造 `IRContext` 时，它会成为线程局部的活动 Context，并保留此前的活动 Context。因此，静态工厂只能在目标 Context 处于活动状态时调用。已经持有 `IRContext&` 的新代码应优先使用实例工厂和 `typeInfoPool()`。

## 3. NamingContext

`NamingContext` 声明于 `Semantic/context/naming_context.hpp`，实现在 `Semantic/context/naming_context.cpp`，由 Semantic `Module` 拥有命名空间及语义声明注册表。公开头文件仅前置声明 Backend 类型；Backend context 不再声明或实现语义命名空间。

一个 `NamingContext` 保存模块标识符，以及从结构体名称到 `IRStructDecl` 的映射，提供：

- `lookupStructDecl(name)`：查询结构体声明；
- `resolveStruct(id)`：供 lowering 只读解析语义声明；
- `resolveNamingContext(id)`：定位声明所属的命名空间；
- `getStructTy(context, id)`：获取或创建本命名空间拥有的 opaque 类型；
- `lookupStructType(name)`：查询已创建的 Backend 类型，不触发 lowering；
- `declareOpaqueStruct(name, info)` 和 `implStruct(...)`：注册前置声明并补全语义字段；
- `defineStruct(name, fields, info)`：创建并注册结构体。

同一模块中重复定义同名结构体会抛出 `SakuraError`。不同模块各自拥有 `NamingContext`，即使结构体名称相同，也会得到不同的命名类型。

当前代码中的所有权关系是：

```text
NamingContext -> IRStructDecl（语义声明）
NamingContext -> IRStructType（Backend 类型）
IRContext -> 基础及派生 storage 类型、TypeInfo
LoweringSession -> BackendModule -> BackendFunction
LoweringSession -> 非拥有的类型与源函数索引
```

`IRStructDecl` 保存语义字段、默认值和声明完成状态，不拥有 `IRStructType`。`NamingContext` 用独立容器拥有 Backend 结构体类型；字段类型指针为非拥有引用。storage 访问器仅作为 Lowering 桥接接口，不是语义构造输入；`Module` 不再暴露 Backend 布局查询。同一命名空间的 Backend 类型缓存只能绑定一个 `IRContext`，传入其他 context 会被拒绝。类型缓存是延迟初始化的 mutable 状态，语义声明解析保持只读。

## 4. TypeInfo 与命名类型

`TypeInfoPool` 由 `IRContext` 拥有，在该 Context 内规范化基础、数组、指针、引用和结构体类型描述。

`TypeID::Struct` 由保存 `StructDeclId` 的 `StructTypeInfo` 表示。`LoweringSession::lowerType()` 解析语义声明，在声明所属的 `NamingContext` 获取唯一的 opaque 类型，先登记身份再递归转换字段。数组、指针和引用保留该类型身份及外层包装。

IR 生成器遇到类型标识符时，会通过当前模块的 `NamingContext` 解析。未定义的名称会产生 IR 生成错误，不会再构造通用占位类型。

## 5. LLVM Context 所有权

调用方创建 `llvm::LLVMContext`，连同根 BackendModule 传给 LLVMCodeGenerator。IRContext 不再拥有或暴露 LLVM 资源。CLI 将自己的 LLVMContext 和全部可达生成模块交给 JIT。

`LLVMTypeMapper.hpp/.cpp` 提供 `Codegen::map(IRType*, llvm::LLVMContext&)`，是 storage 到 LLVM 类型的唯一转换边界。opaque 结构体保持身份并原地补全；重复映射会访问此前未完成的内嵌布局。IR 生产代码不依赖 LLVM API；映射测试显式依赖 LLVMCodegen。

## 6. 范围与当前限制

Context/Lowering 接口支持先声明、后补全：再次 lowering 结构体或包含它的复合类型时，会复用原类型并补全已完成声明的字段。正在处理的类型通过 Session 状态避免重复递归；异常后清除处理状态，允许重试。Backend `complete()` 只允许成功一次，并在提交字段前检查空类型和重复字段名。

布局检查沿按值结构体和数组字段检测递归环，指针和引用打断布局依赖。直接或间接按值递归、void 和函数类型字段会被拒绝。字段列表完成不代表所有按值依赖均已具有有限 LLVM 大小：前向依赖仍可能 opaque，使用其实际大小前须完成相关声明并再次 lowering。LLVM 字段偏移、大小和对齐仍由目标 DataLayout 决定。

命名类型解析限定在模块内部，跨模块类型导入和类型别名不属于当前 Context 功能。针对性验证可运行 `sakurae-ir-context-tests --struct-lowering`；无参数运行仍包含原有 Context 测试。

派生类型缓存以非拥有的 `IRType*` 作为键，可能引用 `NamingContext` 拥有的结构体。Program 销毁时先销毁 Session，再销毁模块和 NamingContext，最后销毁 IRContext；停止使用相关类型后才进行销毁。不得在所属 NamingContext 销毁后继续使用结构体或引用它的派生类型。

## 7. Backend 函数与模块 Lowering

`LoweringSession::lowerModule(const Module&)` 转换可达导入图；`lowerFunction(const Function&)` 返回所属模块拥有的函数描述。重复调用复用模块/函数身份并刷新类型，包括首次 lowering 后才完成的未使用 opaque 结构体。Session 只接受所属 Program 的模块。

`BackendFunction` 非拥有地引用源函数，保存 lowered 签名及具名参数、linkage、Raw/Boxed/Void ABI 策略、分开的 GC 值 root 与槽位 root 描述，以及 value/type-operand 类型表。原有 Block、Instruction、Value 仍属于 Semantic IR。`BackendModule` 拥有本模块函数描述，索引导入函数、runtime 声明、符号和结构体布局。递归导入支持环，菱形导入复用函数身份，不同函数的 source 名称或 linkage 别名冲突会被拒绝。

`BackendModule::verify()` 不创建 LLVM 对象即可验证描述图；`toString()` 输出签名、ABI/root 元数据、导入和布局。Session 还拥有 runtime 描述图，BackendFunction 非拥有地索引常量类别、值表示和类型操作数。字符串与字符指针即使 storage 相同，也保留不同描述。LLVM wrapper 继续保存生成的函数/模块、基本块、alloca、builder 和活动 GC 作用域状态；`typeof` 代码生成暂缓实现。

FunctionEmitter 与 ModuleEmitter 为命名空间级类型，分别在同名头文件声明、cpp 文件实现全部方法；generator 负责协调，不保留旧嵌套类兼容别名。Lowering 将字段选择器解析为 BackendFieldAccess，emitter 只读取原始布局、索引和 storage 策略。typeof 暂缓并明确报告 emission 错误。

