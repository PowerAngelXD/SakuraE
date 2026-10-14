# SakuraE IR Context Design

[Chinese Version](context-zh_cn.md)

This document describes the current Context implementation. Forward declarations and completion refer to Context/Lowering APIs, not complete source-language syntax or generator support.

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
      owns IRStructType objects interned by StructDeclId
    owns Function and IRValue objects
```

`IRContext` is shared by all modules in one `Program`. `NamingContext` belongs to one `Module` and establishes the namespace boundary for named structures.

## 2. IRContext

`IRContext`, declared in `../Backend/context/context.hpp`, owns program-level types and LLVM resources:

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
- `resolveStruct(id)` for read-only semantic declaration resolution during lowering;
- `resolveNamingContext(id)` to locate the declaration's owning namespace;
- `getStructTy(context, id)` to obtain or create an opaque type owned by this namespace;
- `lookupStructType(name)` to query an existing Backend type without triggering lowering;
- `declareOpaqueStruct(name, info)` and `implStruct(...)` to register and complete semantic declarations;
- `defineStruct(name, fields, info)` to create and register a structure.

The map rejects a second definition with the same name in one module by raising `SakuraError`. Identical names in different modules are kept in different `NamingContext` instances and are therefore distinct named types.

The current ownership relationship is intentionally direct:

```text
NamingContext -> IRStructDecl (semantic declaration)
NamingContext -> IRStructType (Backend type)
IRContext -> basic and derived types, TypeInfo, LLVM Context
LoweringSession -> non-owning declaration-ID/Backend-type mappings
```

`IRStructDecl` stores semantic fields, defaults, and completion state; it does not own `IRStructType`. `NamingContext` owns Backend structure types in a separate container; field type pointers remain non-owning. Its Backend cache binds to one `IRContext` and rejects requests using another context. The lazily initialized cache is mutable, while semantic declaration resolution remains read-only.

## 4. TypeInfo and Named Types

`TypeInfoPool` is owned by `IRContext`. It canonicalizes basic, array, pointer, reference, and structure type descriptions for that context.

`StructTypeInfo` stores a `StructDeclId`. `LoweringSession::lowerType()` resolves the semantic declaration, obtains the unique opaque type from the declaration's owning `NamingContext`, registers its identity, and recursively lowers its fields. Arrays, pointers, and references preserve both that identity and their outer wrappers.

The IR generator resolves a type identifier through the current module's `NamingContext`. An unresolved identifier produces an IR-generation error instead of constructing a generic placeholder type.

## 5. LLVM Context Ownership

LLVM types are created with the `llvm::LLVMContext` owned by `IRContext`. LLVM code generation receives that same context from `Program`; it does not create an unrelated LLVM context for the same IR program.

`IRContext::releaseLLVMContext()` exists for paths that must transfer ownership of the LLVM context, such as a JIT handoff. After ownership has been released, callers must not request the context again from that `IRContext`.

## 6. Scope and Current Limitations

Context/Lowering APIs support forward declarations and later completion. Lowering a structure or a composite containing it again reuses the same type and completes fields whose declarations are now available. Session processing state prevents recursive re-entry and is cleared after exceptions to allow retries. Backend `complete()` may succeed only once and checks null field types and duplicate names before committing fields.

Layout validation follows by-value structures and array fields to detect cycles; pointers and references break layout dependencies. Direct or indirect by-value recursion, void fields, and function-type fields are rejected. A completed field list does not imply that all by-value dependencies have a finite LLVM size: forward dependencies may remain opaque and must be declared and lowered again before their sizes are used. Target DataLayout still determines LLVM field offsets, sizes, and alignment.

Named-type lookup is module-local; cross-module type import and type aliases are outside this Context feature. Run `sakurae-ir-context-tests --struct-lowering` for focused validation; running without arguments still includes the original Context tests.

Derived-type caches use non-owning `IRType*` keys and may reference structures owned by `NamingContext`. Program teardown destroys the Session first, then modules and their NamingContexts, and finally the IRContext, after related type use has stopped. Structure types and derived types referring to them must not be used after their owning NamingContext is destroyed.
