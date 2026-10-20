# IR String Representation

[简体中文](string-zh_cn.md)

`TypeInfo::String` and `Pointer<Char>` are different source-language semantic types. A string is a GC-managed string object; a char pointer is a native address.

Their current storage types are intentionally identical: `LoweringSession::lowerType()` returns the owning IRContext's pointer-to-char IRType for both. Do not recover string semantics from IRType, its textual name, or the LLVM pointer type.

Session-owned RuntimeTypeDescription preserves the distinction before LLVM emission:

| Description | Runtime tag | Managed root | Runtime metadata |
| --- | --- | --- | --- |
| String | String | Yes | string |
| Pointer to character | Pointer | No | ptr<i8> |

LLVMCodegen reads the Backend description for boxing, comparisons, indexing, and GC decisions. String literals call `create_string`; the wrapper stores a RuntimeString containing GC-managed character data and length. A first-class char pointer stores its address in RuntimeValue::data.pointer instead.

Both storage types map to an opaque LLVM pointer through `Codegen::map()`. `IRType::isString()` and the old tid2IRType conversion are not available. See [Value system](value.md) for constant and load/store boundaries.
