# SakuraE Runtime Library

[简体中文](README-zh_cn.md)

The runtime provides GC-managed payload allocation, boxed values, strings, I/O, runtime errors, and immutable runtime type metadata for generated code.

## Interfaces

- [`gc.h`](gc.h): payload allocation, scanning, roots, scopes, and collection. See [GC design](GC.md).
- [`value.h`](value.h): `RuntimeValue`, its `RuntimeType` tag, and `RawValue` payload. Generated language calls pass `RuntimeValue*`; managed arrays currently use pointer payloads.
- [`rttype.h`](rttype.h): `RuntimeTypeInfo`, the runtime type metadata object. `__runtime_type_info_basic`, `_pointer`, `_reference`, `_array`, and `_struct` intern immutable metadata. Factory C symbols are unchanged.
- [`raw_string.h`](raw_string.h): `create_string(const char*)`, `concat_string(RuntimeValue*, RuntimeValue*)`, and the compatibility no-op `free_string(RuntimeValue*)`. Character payloads are GC-managed.
- [`print.h`](print.h): `print(RuntimeValue*)` and `println(RuntimeValue*)`.
- [`alloc.h`](alloc.h): raw `__alloc` / `__free` allocation APIs.
- [`errors.h`](errors.h): runtime error state and bounds checks.

`IR::TypeInfo` is the source-language semantic type; `IR::IRType` describes storage; `runtime::RuntimeTypeInfo` is runtime metadata. `RuntimeType::TypeInfo` is only a boxed-value tag, not a fourth type system. Runtime factories do not receive compiler type objects.

The CLI registers the C ABI symbols with the JIT. Runtime objects are linked into the compiler executable; LLVM generation emits calls to those symbols. `typeof` code generation is currently deferred.
