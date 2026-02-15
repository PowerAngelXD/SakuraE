# sakIR中Alloca和Heap的管理

> [!warning]
> 这篇文档是作者本人在经过地狱般的第一次重构后痛定思痛写出来的文档
> 
> 意在将这方面的问题彻底解决

### 概述

在sakIR中，一个值将分为两种类型：`RawType` 和 `ComplexType`

而一个变量在被声明定义的时候，会被赋上这两种之一的值

对于这两种情况，sakIR处理的方式也会不相同：

**对于 `RawType`**，sakIR会直接调用 `createAlloca` 为其创建一个声明，这个方法的返回的 `IRValue*` 的类型就是这个值的本身的类型

**对于 `ComplexType`** sakIR也会调用 `createAlloca` 创建一个声明，但是这些 Alloca 会无一例外地均为 `Pointer` 类型，因为在更深层的架构中，它们都是由 `__gc_alloc` 进行内存分配的，换而言之，他们都分配在堆上（新版本中，使用了新的gc系统）

其中，我们需要说明部分OpKind下Instruction的返回值：

- `alloca` 返回栈上的地址
- `indexing` 返回具有数组特征值的地址（堆地址，而不是alloca后栈上的地址）
- `param` 返回栈上的地址

### 最后澄清

- RawType 
    sakIR会直接创建一个以其值本身类型的alloca
- ComplexType
    sakIR会创建一个类型为 `ptr<T>` 的alloca（T为这个值本身的类型）