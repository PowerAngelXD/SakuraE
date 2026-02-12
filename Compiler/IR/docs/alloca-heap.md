# Management of Alloca and Heap in sakIR
> [!warning] 
> This document was written by the author after a "hellish" first refactoring, 
> as a reflection to thoroughly resolve issues in this area.

### Overview
In sakIR, values are categorized into two types: `RawType` and `ComplexType`.

When a variable is declared and defined, it is assigned one of these types. sakIR handles these two scenarios differently:

**For `RawType`:**

sakIR directly calls `createAlloca` to create a declaration. The `IRValue*` returned by this method has a type identical to the value's intrinsic type.

**For `ComplexType`:**

sakIR also calls `createAlloca` to create a declaration, but these allocas are invariably of `Pointer` type. This is because, in the underlying architecture, they are managed via `__alloc` for memory allocation—in other words, they are all allocated on the heap.

It is necessary to clarify the return values of Instruction under specific OpKinds:

- `alloca`: Returns an address on the stack.
- `indexing`: Returns the address of a value with array characteristics (this is a heap address, not the stack address from the initial alloca).
- `param`: Returns an address on the stack.

### Final Clarification
- RawType sakIR creates an alloca using the value's intrinsic type directly.
- ComplexType sakIR creates an alloca of type `ptr<T>` (where T is the intrinsic type of the value)