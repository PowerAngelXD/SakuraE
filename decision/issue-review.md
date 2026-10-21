# IR / LLVMCodegen 问题重新评估

评估基线：提交 `7426e3d` 加当前工作区未提交修改（Emitter 改名、RuntimeTypeKind 改名、GC/索引等修复）。
编号沿用上一轮注释工作中整理的 39 项问题清单。本文只记录状态与解决思路，未修改任何代码。

## 1. 已解决

| # | 问题 | 解决方式 |
|---|---|---|
| 1（部分） | for/repeat 循环体在 `enterLoop` 之前生成，break/continue 报 "used out of loop" | `bd40ab0`：循环体生成移到 `enterLoop` 之后 |
| 2 | 三个及以上操作数的 `&&`/`\|\|` 短路不正确 | `7426e3d`：每个右操作数一对 `rhs.i`/`join.i` 块，条件值只在本块使用 |
| 3 | indexing 下标不扩展到 i64，不检查边界 | 工作区：`indexToI64` 统一按 RuntimeTypeKind 选择符号扩展，`emitBoundsCheck` 覆盖数组、引用数组 |
| 4 | 字符串 indexing 无边界检查 | 工作区：装箱字符串用 `boxedStringLength`，原始字段字符串用 `rawStringLength`（strlen）。`char*` 没有长度信息，维持不检查 |
| 5 | `__gc_register_value_slot` 只登记一次当时的 payload | 工作区：`RootEntry{addr, RootKind::ValueSlot}`，扫描时读取槽位当前值 |
| 6 | `main` 末尾隐式 `ret` 不调用 `gcLeaveAllScopes` | 工作区：`FunctionEmitter::codegen` 补发前先调用 |
| 7 | `getIntNTy(32)` 与 `getInt32Ty()` 返回不同实例 | 工作区：`getIntNTy`/`getUIntNTy` 对标准位宽转发到定宽实例 |
| 8 | 经指针自引用的结构体 `toString` 无限递归 | 工作区：`fieldTypeName` 对字段中的结构体只打印名字 |
| 9 | 模块无函数时 `curFunc()` 访问 `fnList[-1]` | 工作区：越界时报 IR 生成错误 |
| 10 | `~IRContext` 非 LIFO 销毁破坏 `activeContext` 链 | 工作区：析构时从链中间摘除自身 |
| 11 | `createAlloca` 在 `++getFirstInsertionPt()` 插入 | 工作区：改为 `getFirstInsertionPt()` |
| 15 | 后缀 `x++`/`x--` 返回新值 | `bd40ab0`：返回 `oldValue` |
| 29 | indexing 每次打印 `addr IR type` 调试输出 | 工作区：已删除 |

## 2. 仍未解决：行为错误

### 1（剩余）for/repeat 的 continue 跳过步进
- 现状：`visitForStmtNode`、`visitRepeatStmtNode` 都调用 `enterLoop(condBlock, mergeBlock)`，continue 直接跳回 cond。for 会跳过 `i++`；repeat 的计数器只在 step 中自增，循环体内执行 continue 会导致死循环。
- 思路：continue 目标改为 `stepBlock`，即 `enterLoop(stepBlock, mergeBlock)`。step 块已经存在并且跳回 cond；repeat 的 step 复用 cond 中 load 的计数器值，cond 支配 step，不受影响。while 没有 step，保持跳回 prep。补一条 `repeat(3) { continue; }` 的 JIT 用例验证循环能结束。

### 12 浮点字面量后缀含义与 C 相反
- 现状：带 `f` 的字面量解析为 Float64（stod），不带后缀的解析为 Float32（stof）。
- 思路：带 `f` → `std::stof` 得到 Float32；不带后缀 → `std::stod` 得到 Float64。这会改变现有源码中浮点字面量的默认类型，需要同步检查 `test/*.sak` 和 generator 测试里的浮点断言。

### 13 `U` 后缀超过 32 位时静默截断
- 思路：先用 `stoull` 解析，超过 `UINT32_MAX` 时报 IR 生成错误，与 `L`/无后缀分支的溢出行为一致。

### 14 浮点解析异常没有转换成 SakuraError
- 思路：`FLOAT_N` 分支和 `INT_N` 一样用 try/catch 包住，`std::invalid_argument`/`std::out_of_range` 转成带 `tok.info` 的 SakuraError。

### 16 `main` 返回类型检查依赖修饰后的名字
- 现状：`fnName == "main"` 比较的是 mangle 后的名字，带参数的 `main` 不会被检查。
- 思路：用源名（mangle 前的 identifier）比较；同时决定是否允许带参 main，若不允许直接报错。

### 17 return 检查只看"出现过 return"
- 现状：`hasReturn` 在任意位置遇到 return 就置位，`if (c) { return 1; }` 这类缺少返回路径的函数能通过，后端得到无终结指令的块。
- 思路：生成结束后遍历函数的基本块，对非 void 函数检查每个可达块是否以终结指令结束；void 函数对未终结的块补 `ret`。可以替换掉 `hasReturn`/`getReturnChecker`。

### 18 比较运算不检查操作数类型
- 思路：`visitLogicExprNode` 生成比较指令前检查两侧语义类型：数值类型要求相同（项目没有隐式数值转换），`==`/`!=` 额外允许 bool、char、指针、nullable 与 null。不合法时报 IR 生成错误，避免问题拖到 LLVM 阶段。

### 19 repeat 隐藏计数器 `static int repeat_counter` 从不自增
- 现状：所有计数器都叫 `$repeat_counter.1`，嵌套 repeat 依赖作用域遮蔽才不冲突。
- 思路：创建后执行 `repeat_counter++`，或参照 `binaryID` 的写法。

### 20 `Scope::declare` 同层重名静默忽略，`leave()` 不检查层数
- 思路：`declare` 用 `emplace` 的返回值判断是否已存在，已存在时报重复声明错误；`leave()` 在 `cursor == 0` 时抛 `std::logic_error`（属于编译器内部错误）。

### 21 `getArrayDimension` 对无符号 0 不报错
- 思路：`UInt32`/`UInt64` 分支同样要求 `> 0`，否则报 "Array dimension must be greater than zero."。

### 22 `IRType::isEqual` 对 Ref、FloatN、Block 一律返回 false
- 思路：Ref 与 Pointer 共用元素类型递归比较；FloatN 比较位宽；Block 比较身份即可（第一行的指针比较已覆盖），其余种类保持 false。

### 23 `IRIntegerType::isUnsigned` 从不写入，`toString` 不区分符号
- 思路：删除 `isUnsigned` 字段，符号信息只由 `irTypeID` 表达；`toString` 对 `UInteger*TyID` 打印 `uN`。打印结果会进入 IR dump，需要同步更新相关测试的期望文本。

### 24 两个 `Block::createReturn` 对已终结块的处理不一致
- 思路：无参版本与带值版本一致，已终结时返回 nullptr；或者两者都改为抛错。先确认调用方（`visitReturnStmtNode` 之后可能还有不可达语句）期望哪种行为。

### 25 `IRStructDecl::complete` 用 `defaults` 整体覆盖已有默认值
- 思路：改为合并：只写入 `defaults` 中出现的字段，保留之前 `setDefaultValue` 的结果；或者在 `complete` 之前禁止调用 `setDefaultValue`。

### 26 `IRArray` 构造函数公开；`Constant::intern` 命中缓存时丢弃位置信息
- 思路：`IRArray` 构造函数改为 private，只允许 `createArray` 创建。位置信息不应参与常量驻留的键，报错需要位置时由调用方传入当前 token 的 info，不从常量对象里读取。

### 27 `Constant::toString` 没有 double 分支
- 思路：在 `std::visit` 中补 `double` 分支，与 `float` 使用相同的格式。

## 3. 仍未解决：诊断、命名、文档、测试

| # | 问题 | 思路 |
|---|---|---|
| 28 | `toFormatString` 会处理字符串字面量里的 `{` `}` `;` `@` | 扫描时跟踪是否在引号内，引号内原样输出 |
| 30 | `createLoad` 在纯读取场景报赋值的错误文本 | 改为 "An L-value is required to load a value." |
| 31 | `createAlloca` 遇到 void 报 "Unsupported semantic default initialization" | 在 `createAlloca` 入口先拒绝 void，报 "Cannot declare a variable of type void." |
| 32 | `TypeInfo::getFunctionType()` 实际返回返回类型 | 当前没有调用者，属于死代码，可直接删除 |
| 33 | `Function::isLookEmpty` 拼写错误 | 改名为 `isLoopEmpty`，调用方只有 break/continue 两处 |
| 34 | `session.hpp` 使用 `std::map` 但没有 `#include <map>`；include guard 为 `SAKURAE_LOWERING_CONTEXT_HPP` | 补 include，guard 改为 `SAKURAE_LOWERING_SESSION_HPP` |
| 35 | call 指令依赖 `"call." + 名字` 约定，`session.cpp` 与 `LLVMCodegenerator.cpp` 都硬编码前缀长度 5 | 让 call 指令把被调函数作为操作数或单独字段保存；短期至少把前缀抽成共享常量 |
| 36 | `GC-zh_cn.md`/`GC.md` 第 5 步说 `createHeapAlloc` 的结果总有 root 槽保护 | 改为"仅当 lowering 把该值记入 `rootedValues` 时才登记 root" |
| 37 | `invalid-null-test` 的源码 `return 0 }` 缺分号，可能因语法错误而通过 | 补分号，并断言错误信息与 null 推断相关 |
| 38 | codegen 测试的通过信息没有提到 `testFieldValidationAndDeferredTypeof` | 在输出文本中加上 typeof/字段校验 |
| 39 | 多数 case 自己调用 `bind`，`instgen` 末尾再 bind 一次 | 删除各 case 内的 bind，统一由末尾处理；`alloca`、`param`、`gaddr` 等 bind 的值与 `instResult` 不同的 case 需要先改为写入 `instResult` |

## 4. 本次评估新发现

工作区的改名与修复还没提交，以下问题都来自这批未提交修改：

- Emitter 改名不完整：
  - `ModuleEmitter.hpp` 的 include guard 仍是 `SAKURAE_MODULEEMMITER_HPP`；`FunctionEmitter.hpp` 的 guard 是大小写混用的 `SAKURAE_FUNCTIONEmitter_HPP`。
  - 注释和文档中仍有 `ModuleEmmiter`：`LLVMCodegenerator.hpp:52,54,339`、`LLVMCodegenerator.cpp:677`、`FunctionEmitter.hpp:32`、`Compiler/IR/docs/{context,context-zh_cn,value,value-zh_cn}.md`、`Runtime/GC{,-zh_cn}.md` 第 4 步、`decision/next-plan.md`。
  - 思路：统一改为 `SAKURAE_FUNCTION_EMITTER_HPP`/`SAKURAE_MODULE_EMITTER_HPP`，全局替换 `ModuleEmmiter` → `ModuleEmitter`。
- 修复后遗留的过期注释：
  - `generator.cpp:492-493` 仍说后缀运算返回新值。
  - `generator.cpp:1293-1294` 仍说 repeat 的循环体看不到本循环。
  - `generator.cpp:177` 仍引用已改名的 `isInteriorGCManagedLValue`（现为 `isGCLValue`）。
- `&&` 与 `||` 同优先级、从左到右结合（`a || b && c` 等价于 `(a || b) && c`），与 C 系语言不同。这属于语言设计决定。如果要与 C 一致，需要在 Parser 中把 `&&` 提到更高的优先级层级，`visitBinaryExprNode` 不需要改。

## 5. 死代码（按用户决定暂不处理）

`LoweringSession::structMap`（只写不读）、`rankList`、`StringTyID`、空文件 `impl.hpp`、`IRType::getStorageType()`。第 32 项 `getFunctionType()` 也没有调用者，可以和这批一起处理。

## 6. 建议处理顺序

1. 第 4 节的改名收尾和过期注释：与当前未提交的改名放在同一批提交里，避免改名只做一半就提交。
2. 第 1 项剩余部分（continue 死循环）以及 12、16、17：会产生错误代码或让错误程序通过。
3. 13、14、18–22：前端诊断和类型规则。
4. 23–27 以及第 3 节：一致性和可维护性。
