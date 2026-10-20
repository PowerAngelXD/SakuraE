# IR 字符串表示

[English Version](string.md)

`TypeInfo::String` 和 `Pointer<Char>` 是不同的源语言语义类型：字符串表示 GC 托管的字符串对象，字符指针表示原生地址。

当前两者有意复用相同 storage 类型：`LoweringSession::lowerType()` 返回所属 IRContext 的 pointer-to-char IRType。不能根据 IRType、其文本名称或 LLVM 指针类型反推字符串语义。

Session 拥有的 RuntimeTypeDescription 在生成 LLVM 前保留这一区别：

| 描述 | Runtime tag | 托管 root | Runtime 元数据 |
| --- | --- | --- | --- |
| String | String | 是 | string |
| 字符指针 | Pointer | 否 | ptr<i8> |

LLVMCodegen 根据 Backend 描述决定装箱、比较、索引和 GC 行为。字符串常量调用 create_string，wrapper 中的 RuntimeString 保存 GC 托管字符数据及长度；一等字符指针则把地址存入 RuntimeValue::data.pointer。

两个 storage 类型都通过 `Codegen::map()` 映射为 opaque LLVM 指针。`IRType::isString()` 与旧 tid2IRType 转换均已移除。常量及 load/store 边界见 [值体系](value-zh_cn.md)。
