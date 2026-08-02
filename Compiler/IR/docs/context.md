# SakuraE IR Context Design

[Chinese Version](context-zh_cn.md)

This document describes the Context implementation currently used by SakuraE IR. It records the ownership boundaries that exist in the code today; it is not a specification for unimplemented features such as forward-declared structures or cross-module named types.

## 1. Overview

SakuraE separates type storage from module-local names:

```text
Program
  owns IRContext
    owns llvm::LLVMContext
    owns basic and derived IR types
    owns TypeInfoPool
  owns Module objects
    owns NamingContext
      owns IRStructDecl objects
        owns IRStructType objects
    owns Function and IRValue objects
```

`IRContext` is shared by all modules in one `Program`. `NamingContext` belongs to one `Module` and establishes the namespace boundary for named structures.

## 2. IRContext

`IRContext`, declared in `Compiler/IR/context.hpp`, owns resources that are independent of source-level names:

- one `llvm::LLVMContext`;
- canonical basic types, including integer, floating-point, void, string, block, and type-info types;
- canonical derived types: pointers, references, arrays, and function types;
- one `TypeInfoPool`.

Each factory caches its result within the context. Repeated requests for the same type shape in one context return the same `IRType*`. Different `IRContext` instances have separate type caches and separate LLVM contexts.

`Program` creates its `IRContext` before its modules. Its destructor deletes modules before the `IRContext` member is destroyed, so module-owned declarations and IR values do not outlive the program-level type and LLVM resources.

### Compatibility factories

Existing static factories such as `IRType::getInt32Ty()` and `TypeInfo::makeBasicTypeID()` remain available as compatibility entry points. They delegate to `IRContext::current()`.

Constructing an `IRContext` makes it the thread-local active context and preserves the previously active context. Therefore, static factories must only be used while the intended context is active. New code that already has an `IRContext&` should prefer its instance factories and `typeInfoPool()` directly.

## 3. NamingContext

`NamingContext` is declared in `context.hpp` and implemented in `context.cpp`. Its implementation includes `model/struct.hpp` so that the public Context header needs only a forward declaration of `IRStructDecl`.

A `NamingContext` stores a module identifier and a map from a structure name to an `IRStructDecl`. It provides:

- `lookupStructDecl(name)` to obtain the declaration;
- `lookupStructType(name)` to obtain its `IRStructType`;
- `defineStruct(name, fields, info)` to create and register a structure.

The map rejects a second definition with the same name in one module by raising `SakuraError`. Identical names in different modules are kept in different `NamingContext` instances and are therefore distinct named types.

The current ownership relationship is intentionally direct:

```text
NamingContext -> IRStructDecl -> IRStructType
```

`IRStructDecl` stores source declaration data and owns the concrete `IRStructType`. `IRStructType` holds field names, field types, and field indices. It is not an `IRValue`.

## 4. TypeInfo and Named Types

`TypeInfoPool` is owned by `IRContext`. It canonicalizes basic, array, pointer, reference, and structure type descriptions for that context.

The `TypeID::Struct` case is represented by `StructTypeInfo`, which stores the resolved `IRStructType*`. This replaces the old information-free custom-type placeholder. Consequently, converting a structure `TypeInfo` back to IR returns the exact named type, while array, pointer, and reference `TypeInfo` objects recursively preserve that identity.

The IR generator resolves a type identifier through the current module's `NamingContext`. An unresolved identifier produces an IR-generation error instead of constructing a generic placeholder type.

## 5. LLVM Context Ownership

LLVM types are created with the `llvm::LLVMContext` owned by `IRContext`. LLVM code generation receives that same context from `Program`; it does not create an unrelated LLVM context for the same IR program.

`IRContext::releaseLLVMContext()` exists for paths that must transfer ownership of the LLVM context, such as a JIT handoff. After ownership has been released, callers must not request the context again from that `IRContext`.

## 6. Scope and Current Limitations

The current implementation provides complete definitions only. It does not yet provide a separate opaque-declaration and later-definition phase for recursive structures. Named-type lookup is module-local; cross-module type import and type aliases are not part of this Context implementation.

The derived-type caches keep non-owning `IRType*` keys. Their lifetime is valid for normal `Program` use, where all modules are destroyed as the program ends. Types and values must not be retained after their owning `Program` has been destroyed.
