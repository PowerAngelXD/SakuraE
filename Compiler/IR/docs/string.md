# IR String Type

[简体中文](string-zh_cn.md)

### This document primarily outlines the string types in SakuraE IR.

In SakuraE IR, `string` is now a dedicated IR type instead of being aliased to raw `char*`.

This distinction is important for GC and language semantics:
- `string` means a GC-managed string object.
- `char*` means a raw native pointer.

Therefore, when the String type is represented as a TypeInfo, the resulting IR type must be `string`, not `char*`.

Relatively, you can see how tid2IRType works for more details:
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
