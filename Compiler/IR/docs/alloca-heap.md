# Alloca and Heap Management in sakIR

> [!warning]
> This document was written by the author after a hellish first refactoring.
> 
> It is intended to thoroughly solve problems in this area.

### Overview

In sakIR, a value is divided into two types: `RawType` and `ComplexType`

When a variable is declared and defined, it is assigned one of these two types.

For these two cases, sakIR handles them differently:

**For `RawType`**, sakIR directly calls `createAlloca` to create a declaration for it. The type of the `IRValue*` returned by this method is the type of the value itself.

**For `ComplexType`**, sakIR also calls `createAlloca` to create a declaration, but these Alloca will invariably be of `Pointer` type. Because in the deeper architecture, they are all allocated memory by `__gc_alloc`, in other words, they are all allocated on the heap (the new version uses a new GC system).

Among them, we need to clarify the return values of Instructions under some OpKinds:

- `alloca` returns an address on the stack.
- `indexing` returns the address of a value with array characteristics (heap address, not the stack address after alloca).
- `param` returns an address on the stack.

### Final Clarification

- RawType 
    sakIR directly creates an alloca with the type of the value itself.
- ComplexType
    sakIR creates an alloca of type `ptr<T>` (T is the type of the value itself).
