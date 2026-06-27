# IR 字符串类型

[English Version](string.md)

### 本文档主要概述 SakuraE IR 中的字符串类型。

现在的 SakuraE IR 中，`string` 是一个独立的 IR 类型，不再与原生 `char*` 复用同一个语义类型。

这样做是为了彻底区分两类值：
- `string`：GC 托管的 string object。
- `char*`：原生 native pointer。

因此，当 `String` 类型通过 `TypeInfo` 转成 IRType 时，结果应该是 `string`，而不是 `char*`。

相对地，你可以查看 `tid2IRType` 的工作原理以了解更多细节：
```c++
inline IRType* tid2IRType(TypeID tid) {
        switch (tid)
        {
        case TypeID::Int32:
            return IRType::getInt32Ty();
        case TypeID::Float:
            return IRType::getFloatTy();
        case TypeID::Char:
            return IRType::getCharTy();
        case TypeID::Bool:
            return IRType::getBoolTy();
        case TypeID::String:
            return IRType::getStringTy();
        default:
            throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown type id to convert to IRType",
                                {0, 0, "InsideError"});
        }
    }
```
