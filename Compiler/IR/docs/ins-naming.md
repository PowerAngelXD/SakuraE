# Instruction Naming

### This document establishes the naming conventions for SakuraE IR instructions to prevent naming confusion.

1. The prefix of every instruction name must be the instruction's unique identifier, with hyphens ("-") used to replace underscores ("_") in multi-word names.
> e.g.
> The instruction cond_br should be named cond-br according to this convention.
> Example: `generator.cpp: line 7`: 
> ```c++
> return curFunc()
>            ->curBlock()
>            ->createInstruction(OpKind::constant, literal->getType(), {literal}, "constant");
> ```

<br>
<br>

1. For instructions with directional or referential meaning, the target objects should be separated by a dot (.).
> e.g.
> `generator.cpp: line 457`: 
> ```c++
> curFunc()
>    ->block(thenExitBlockIndex)
>    ->createInstruction(OpKind::br,
>                        IRType::getVoidTy(),
>                        {mergeBlock},
>                        "br.if.merge");
> ```
> In the case of the cond-br instruction, the jump target mergeBlock should be appended to the instruction alias, separated by a dot (.).