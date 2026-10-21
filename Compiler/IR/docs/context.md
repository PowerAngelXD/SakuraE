# SakuraE IR Context Design

[Chinese Version](context-zh_cn.md)

This document describes the current Context implementation. Forward declarations and completion refer to Context/Lowering APIs, not complete source-language syntax or generator support.

## 1. Overview

SakuraE separates type storage from module-local names:

```text
Program
  owns IRContext
    owns basic and derived IR types
    owns TypeInfoPool
  owns Module objects
    owns NamingContext
      owns IRStructDecl objects
      owns IRStructType objects interned by StructDeclId
    owns Function and IRValue objects
  owns LoweringSession
    owns BackendModule objects
      owns BackendFunction descriptions
```

`IRContext` is shared by all modules in one `Program`. `NamingContext` belongs to one `Module` and establishes the namespace boundary for named structures.

## 2. IRContext

`IRContext`, declared in `../Backend/context/context.hpp`, owns program-level semantic and storage types:

- canonical basic types, including integer, floating-point, void, string, block, and type-info types;
- canonical derived types: pointers, references, arrays, and function types;
- one `TypeInfoPool`.

Each factory caches its result within the context. Repeated requests for the same type shape return the same `IRType*`; different Programs have independent type caches. LLVM contexts are supplied separately by the emitter's caller.

`Program` creates its IRContext before its modules and destroys the Session first, then modules, then the IRContext. Backend descriptions never outlive their source IR or storage types.

### Compatibility factories

Existing static factories such as `IRType::getInt32Ty()` and `TypeInfo::makeBasicTypeID()` remain available as compatibility entry points. They delegate to `IRContext::current()`.

Constructing an `IRContext` makes it the thread-local active context and preserves the previously active context. Therefore, static factories must only be used while the intended context is active. New code that already has an `IRContext&` should prefer its instance factories and `typeInfoPool()` directly.

## 3. NamingContext

`NamingContext` is declared in `Semantic/context/naming_context.hpp` and implemented in `Semantic/context/naming_context.cpp`. Each Semantic `Module` owns its namespace and semantic declaration registry. The header only forward-declares Backend types; Backend context no longer declares or implements the semantic namespace.

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
IRContext -> basic and derived storage types, TypeInfo
LoweringSession -> BackendModule -> BackendFunction
LoweringSession -> non-owning type and source-function indexes
```

`IRStructDecl` stores semantic fields, defaults, and completion state; it does not own `IRStructType`. `NamingContext` owns Backend structure types in a separate container; field type pointers remain non-owning. The storage accessors are a Lowering bridge, rather than Semantic construction inputs; `Module` does not expose a Backend layout lookup. Its Backend cache binds to one `IRContext` and rejects requests using another context. The lazily initialized cache is mutable, while semantic declaration resolution remains read-only.

## 4. TypeInfo and Named Types

`TypeInfoPool` is owned by `IRContext`. It canonicalizes basic, array, pointer, reference, and structure type descriptions for that context.

`StructTypeInfo` stores a `StructDeclId`. `LoweringSession::lowerType()` resolves the semantic declaration, obtains the unique opaque type from the declaration's owning `NamingContext`, registers its identity, and recursively lowers its fields. Arrays, pointers, and references preserve both that identity and their outer wrappers.

The IR generator resolves a type identifier through the current module's `NamingContext`. An unresolved identifier produces an IR-generation error instead of constructing a generic placeholder type.

## 5. LLVM Context Ownership

The caller creates a `llvm::LLVMContext` and passes it with the root BackendModule to LLVMCodeGenerator. IRContext no longer owns or exposes LLVM resources. The CLI transfers its LLVMContext and every reachable emitted module to the JIT.

`LLVMTypeMapper.hpp/.cpp` defines `Codegen::map(IRType*, llvm::LLVMContext&)`. It is the only storage-to-LLVM type mapping boundary. Opaque structures retain identity and are completed in place; remapping revisits nested layouts completed since the previous emission. IR production code has no LLVM API dependency; mapper tests explicitly depend on LLVMCodegen.

## 6. Scope and Current Limitations

Context/Lowering APIs support forward declarations and later completion. Lowering a structure or a composite containing it again reuses the same type and completes fields whose declarations are now available. Session processing state prevents recursive re-entry and is cleared after exceptions to allow retries. Backend `complete()` may succeed only once and checks null field types and duplicate names before committing fields.

Layout validation follows by-value structures and array fields to detect cycles; pointers and references break layout dependencies. Direct or indirect by-value recursion, void fields, and function-type fields are rejected. A completed field list does not imply that all by-value dependencies have a finite LLVM size: forward dependencies may remain opaque and must be declared and lowered again before their sizes are used. Target DataLayout still determines LLVM field offsets, sizes, and alignment.

Named-type lookup is module-local; cross-module type import and type aliases are outside this Context feature. Run `sakurae-ir-context-tests --struct-lowering` for focused validation; running without arguments still includes the original Context tests.

Derived-type caches use non-owning `IRType*` keys and may reference structures owned by `NamingContext`. Program teardown destroys the Session first, then modules and their NamingContexts, and finally the IRContext, after related type use has stopped. Structure types and derived types referring to them must not be used after their owning NamingContext is destroyed.

## 7. Backend Function and Module Lowering

`LoweringSession::lowerModule(const Module&)` lowers the reachable import graph. `lowerFunction(const Function&)` returns its module-owned function description. Repeated calls preserve module/function identity and refresh types, including unused opaque structures completed after the first lowering. The Session only accepts modules from its owning Program.

`BackendFunction` stores the source function non-owningly, its lowered signature and named parameters, linkage, Raw/Boxed/Void ABI choices, separate GC value-root and slot-root decisions, and value/type-operand tables. The original Blocks, Instructions, and Values remain in Semantic IR. `BackendModule` owns local function descriptions and indexes imported functions, runtime declarations, symbols, and structure layouts. Recursive imports are cycle-safe; diamond imports share function identity, while conflicting source names or linkage aliases are rejected.

`BackendModule::verify()` validates the description graph without creating LLVM objects; `toString()` dumps signatures, ABI/root metadata, imports, and layouts. The Session also owns runtime description graphs: BackendFunction indexes constant kinds, value runtime representations, and type operands non-owningly. String and char-pointer descriptions remain distinct even with identical storage. LLVM wrappers retain generated functions/modules, basic blocks, allocas, builders, and active GC scope state. `typeof` generation remains deferred.

`FunctionEmitter` and `ModuleEmitter` are namespace-level types declared in separate matching headers, with all methods implemented in their corresponding cpp files. The generator coordinates them; no nested LLVMFunction/LLVMModule compatibility aliases remain. Field selectors are resolved during Lowering into BackendFieldAccess entries; emitters only use raw layout/index/storage-policy descriptions. `typeof` is deferred and reports an explicit emission error.

