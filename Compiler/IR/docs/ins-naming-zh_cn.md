# 指令命名

[English Version](ins-naming.md)

### 本文档规定了 SakuraE IR 指令的命名规范，以防止命名混淆。

1. 每个指令名称的前缀必须是指令的唯一标识符，多单词名称中使用下划线 ("_") 代替连字符 ("-")。
> e.g.
> 指令"cond-br"应该被命名为"cond_br"
> 例: `block.hpp: line 85`: 
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

2. 对于具有方向性或引用含义的指令，目标对象应使用点号 (.) 分隔。
> 例如：
> `generator.cpp: 第 457 行`: 
> ```c++
> curFunc()
>    ->block(thenExitBlockIndex)
>    ->createInstruction(OpKind::br,
>                        curFunc()->curBlock()->getSemanticType(),
>                        {mergeBlock},
>                        "br.if.merge");
> ```
> 在 `cond-br` 指令的情况下，跳转目标 `mergeBlock` 应附加到指令别名后，并用点号 (.) 分隔。
