# Instruction Naming

[简体中文](ins-naming-zh_cn.md)

### This document establishes the naming conventions for SakuraE IR instructions to prevent naming confusion.

1. The prefix of every instruction name must be the instruction's unique identifier, with underscores ("_") used to replace hyphens ("-") in multi-word names.
> e.g.
> The instruction cond-br should be named cond_br according to this convention.
> Example: `block.hpp: line 85`: 
> ```c++
> IRValue* createCondBr(IRValue* cond, IRValue* thenBlock, IRValue* elseBlock) {
>       if (!instructions.back()->isTerminal())
>            return createInstruction(OpKind::cond_br,
>                                      getSemanticType(),
>                                      {cond, thenBlock, elseBlock},
>                                      "cond_br.(" + thenBlock->getName() + ").(" + elseBlock->getName() + ")");
>           
>       return nullptr;
>}
> ```

<br>
<br>

2. For instructions with directional or referential meaning, the target objects should be separated by a dot (.).
> e.g.
> `generator.cpp: line 457`: 
> ```c++
> curFunc()
>    ->block(thenExitBlockIndex)
>    ->createInstruction(OpKind::br,
>                        curFunc()->curBlock()->getSemanticType(),
>                        {mergeBlock},
>                        "br.if.merge");
> ```
> In the case of the cond-br instruction, the jump target mergeBlock should be appended to the instruction alias, separated by a dot (.).
