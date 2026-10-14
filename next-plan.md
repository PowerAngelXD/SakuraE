# SakuraE 类型系统与 Backend IR 重构计划

## 目标

当前重构只建立清晰的三层边界：

```text
AST / Semantic Analysis
        |
        v
现有 IR Model
  TypeInfo + IRFunction + IRModule + Block + Instruction + Value
        |
        | Lowering
        v
Backend IR
  IRType + BackendFunction + BackendModule
        |
        | LLVM Codegen
        v
llvm::Type* / llvm::Value* / llvm::Function* / llvm::Module*
```

Lowering 只负责三类转换：

```text
TypeInfo   -> IRType
IRFunction -> BackendFunction
IRModule   -> BackendModule
```

Lowering 不把每个 Value、Instruction 或 Block 复制成另一套对象。BackendFunction 和
BackendModule 是 LLVM Codegen 的稳定输入，但不保存 LLVM API 对象；它们保存 lowering
后的函数签名、ABI、布局、linkage、GC/runtime 描述，以及对现有函数体的非拥有引用。
LLVM emitter 再根据这些描述遍历原有 Block/Instruction，建立 LLVM 对象和 value 映射。
所属 Program 持有贯穿 lowering 与 codegen 的 `LoweringSession`。Session 拥有生成的
Backend IR，并维护语义对象到 Backend 对象的映射；Program 的 `IRContext` 拥有基础及派生
类型，各 Module 的 `NamingContext` 拥有命名结构体类型。Session 引用这些类型，不重复拥有。

`LLVMFunction`/`LLVMModule` 如果在过渡期继续使用，应理解为 Backend IR 对象，而不是
`llvm::Function`/`llvm::Module` 的封装。为减少歧义，推荐最终改名为
`BackendFunction`/`BackendModule`，目录使用 `Compiler/IR/Backend`。

## 核心原则

1. 现有 `Compiler/IR/Semantic/model` 继续作为前端生成的 IR model，不新增完整的
   `SemanticValue`、`SemanticInstruction`、`StorageValue` 或 `StorageInstruction`。
2. 语义判断只使用 `TypeInfo`：可赋值性、nullable、指针/引用、重载解析、函数参数匹配、
   结构体字段和控制流检查不得依赖 `IRTypeID`。
3. `IRType` 是 backend/storage 类型；`IRType -> llvm::Type*` 只能发生在 LLVM Codegen。
4. `TypeInfo -> IRType` 只能发生在 Lowering。`TypeInfo::toIRType()` 仅作为过渡兼容接口，
   不得新增调用点，最终删除。
5. Backend IR 不保存 LLVM API 对象；LLVM Codegen 负责 `llvm::Module`、`llvm::Function`、
   `llvm::BasicBlock`、`llvm::IRBuilder`、LLVM value 映射和 intrinsic 生成。
6. 一个 `Program` 对应一个 `IRContext`，由 `Program` 持有，语义类型与 Backend/storage 类型
   均在该 context 中管理。同一份 Semantic IR 只向所属 Program 的 `IRContext` lowering，
   不要求跨 `IRContext` 重复 lowering；显式传递 context 的 lowering 入口必须使用该 context
   创建 `IRType`，不能误用全局 `IRContext::current()`。
7. 每个 Program 复用一个长期存活的 `LoweringSession`；它拥有 Backend IR，不直接拥有
   `IRType`。销毁或重置时，先释放 Backend IR，再释放它引用的 Semantic IR 和 `IRContext`。

## 当前模型的定位

当前 `Compiler/IR/Semantic/model` 下的 `IRValue`、`Instruction`、`Block`、`Function`、
`Module`、`Scope` 继续复用，不建立平行的 Semantic/Storage model。

过渡期间，`IRValue` 可以同时保存：

```cpp
TypeInfo* semanticType;
IRType* storageType; // 可选缓存，不参与语义判断
```

更推荐由 BackendFunction 保存非拥有的 value type side table：

```cpp
std::unordered_map<const IRValue*, IRType*> valueTypes;
```

这样原有 IR model 不必在构造时固化 storage 类型，也不需要复制 Value、Instruction 或
Block。LLVM Codegen 通过 BackendFunction 查询 value 的 storage type，不直接读取
编译器 `TypeInfo*`。

如果过渡期仍在 `IRValue` 上保留 `storageType`，必须明确命名为 `getStorageType()`，并
保留 `getSemanticType()`；不得继续使用含义不明确的 `getType()`。

## TypeInfo 与 TypeLowering

`TypeInfo` 必须向 Lowering 提供复合类型的只读信息。不要让 Lowering 访问私有成员，也不
要公开可修改的内部状态。可提供：

```cpp
using ComplexTypeInfo = std::variant<
    std::monostate,
    ArrayTypeInfo,
    PointerTypeInfo,
    RefTypeInfo,
    StructTypeInfo,
    FunctionTypeInfo
>;

const ComplexTypeInfo& getComplexTypeInfo() const;
```

也可以提供 `getArrayInfo()`、`getPointerInfo()`、`getRefInfo()`、`getStructInfo()` 等
专用只读访问器。选择一种稳定接口即可，不能让 lowering 依赖 `TypeInfo` 的实现细节。

类型 lowering 默认使用所属 Program 的持久 `LoweringSession`。如需提供便捷入口，
也应从该 Session 获取结果，不能在每次调用时创建临时 Session：

```cpp
IRType* lowerType(const TypeInfo* type, LoweringSession& session);
```

转换规则包括基础类型、数组、指针、引用、结构体、函数类型和 nullable 表示。nullable
类型是否与其 base type 使用相同 storage type，由 lowering 策略决定；语义上的 `T` 与
`T?` 仍然是不同的 `TypeInfo`。结构体解析由 Session 持有的 Program 级 resolver 提供，
不能从 Backend 类型反向查找声明。

## StructTypeInfo 与 IRStructType

`StructTypeInfo` 不应直接保存 `IRStructType*`，`IRStructType::FieldInfo` 也不应保存
`TypeInfo*`；否则语义结构体身份、语义字段和 Backend storage 布局会绑定在一起。
当前实现已按声明 ID 表示 `StructTypeInfo`，Backend 字段只保存 `IRType*`；后续继续保持
这一边界，不能以 `IRStructType*` 作为语义类型 canonical identity。

语义层只保存结构体声明身份：

```cpp
struct StructDeclId {
    ModuleId module;
    uint64_t localId;
};

class StructTypeInfo {
    StructDeclId declaration;

public:
    StructDeclId getDeclId() const;
};
```

`StructDeclId` 不是结构体定义，而是查找语义结构体声明的稳定键。它必须在所属 Program
范围内唯一，并且能够区分不同模块中同名的结构体。可以像上例一样组合 `ModuleId` 和模块
内递增 ID；如果只保存单个 `uint64_t`，则必须保证该 ID 只在其所属 Program 中
解释和比较，不能跨 Program 裸比较数值。即使组合模块 ID 与局部 ID，也必须防止不同
Program 中恰好相同的组合 ID 被误认为属于同一份语义声明。

现有 `IRStructDecl` 可以继续复用，但应改造成 Semantic declaration：

```cpp
struct SemanticField {
    String name;
    TypeInfo* type;
    PositionInfo info;
};

class StructDecl {
    StructDeclId id;
    String name;
    std::vector<SemanticField> fields;
    std::map<String, Constant*> defaults;
    bool complete;
};
```

Semantic declaration 负责名称、声明身份、字段语义类型、默认值和 opaque/complete 状态，
不拥有 `IRStructType`。结构体名称查找和声明管理属于 Semantic module/program，不属于
Backend context。

### TypeInfoContext 与结构体声明注册表

现有 `TypeInfoContext` 已经承担 `TypeInfo` 的驻留和 canonical identity 管理。结构体相关
状态应分成两张以 `StructDeclId` 为键、职责不同的表：

```cpp
class SemanticTypeContext {
    std::unordered_map<StructDeclId, std::unique_ptr<TypeInfo>> structTypes;
    std::unordered_map<StructDeclId, std::unique_ptr<StructDecl>> structDecls;

public:
    TypeInfo* makeStructType(StructDeclId id);
    const StructDecl* resolveStruct(StructDeclId id) const;
};
```

`structTypes` 只负责 `StructDeclId -> StructTypeInfo` 的驻留和唯一化：同一 context 中同一个
ID 必须始终返回同一个 `TypeInfo*`。`structDecls` 保存真正的语义声明，包括名称、字段、
默认值和完成状态，用于定义检查与 lowering 解析。不能再以 `IRStructType*` 作为
`structTypes` 的 key。

`TypeInfoContext` 继续由所属 Program 的 `IRContext` 持有；无需为跨 context lowering
迁移其所有权。如果两张表都放入 `TypeInfoContext`，可以按职责改名为
`SemanticTypeContext`，但不改变由 Program 的 `IRContext` 持有这一关系。也可以保持当前
分工：`TypeInfoContext` 只保留 `structTypes`，`structDecls` 放在由 Semantic module/program
持有的 `StructDeclRegistry` 中。两种实现都必须向 lowering 提供只读的 `StructResolver`。

语义上下文或声明注册表负责检查：

- `StructDeclId` 是否属于当前 Program 的语义声明；
- ID 和模块内名称是否重复注册；
- opaque 声明是否只被完成一次；
- `StructTypeInfo` 引用的 ID 是否存在；
- 字段引用的结构体 ID 是否可以解析。

Backend 的 `IRStructType` 只表达 storage 布局：

```cpp
struct BackendField {
    String name;
    IRType* type;
    PositionInfo info;
};
```

`IRStructType::FieldInfo` 可以保留为 Backend 字段结构，但必须删除其中的 `semanticType`。
最终关系为：

```text
Semantic:
SemanticTypeContext
  -> StructDeclId -> StructTypeInfo { declaration id }
  -> StructDeclId -> StructDecl { fields: TypeInfo* }

Lowering:
read StructDeclId + StructDecl
  -> StructDeclId -> IRStructType { fields: IRType* }
```

Lowering 不迁移、修改或替换 semantic context 中的表。它只读地解析 `StructDecl`，并在
所属 Program 的 Session 中建立独立、持续有效的结果映射。结构体统一通过 `lowerType()`
按需 lowering：首次遇到声明时，在声明所属 Module 的 `NamingContext` 中创建 opaque
`IRStructType`，先记录 `StructDeclId -> IRStructType*`，再递归转换字段类型并完成布局。
递归转换过程中再次遇到正在填充的结构体时，复用已登记的类型对象。

`NamingContext` 分别拥有语义声明表和 Backend 结构体类型表，不让 `IRStructDecl` 拥有
`IRStructType`。Program 级 resolver 同时定位声明及其所属 NamingContext；后者的类型缓存
只服务于该 Program 的 IRContext，禁止跨 context 复用。结构体字段类型均为非拥有引用；
销毁 NamingContext 前必须停止使用相关 Session 和类型指针。

如果语义声明尚未完成，先保留 opaque 类型；声明完成后的后续 lowering 必须在同一个
类型对象上补齐布局，不能仅因缓存命中就直接返回。实现应区分尚未填充、正在填充和已完成
的状态，避免递归重复填充。结构体身份预登记用于解析引用，并不意味着直接内嵌自身的
无限布局合法；合法递归的 storage 表示仍须由类型规则和 lowering 策略确定。

不要求先批量预建全部结构体，也不需要单独的 `prepareStructs()` 入口。模块 lowering
若需要包含所有声明的结构体（包括未被函数引用的类型），可以枚举声明并逐个调用
`lowerType()`，复用同一套转换逻辑。

如果 `StructTypeInfo` 只保存 `StructDeclId`，lowering 必须通过 Session 持有的
Program 级 resolver 找到字段声明。入口可以是：

```cpp
IRType* lowerType(const TypeInfo* type, LoweringSession& session);
```

完整的函数或模块 lowering 由 `LoweringSession` 引用源 Semantic program 来
提供 resolver。过渡期也可以让 `StructTypeInfo` 保存非 Backend 的 `const StructDecl*`，
但最终目标仍是稳定的 `StructDeclId`。

## Backend IR

Backend IR 不是另一套完整的前端 IR，而是 lowering 产物和 LLVM Codegen 输入。

### BackendFunction

`BackendFunction`（过渡期可称 `LLVMFunction`）至少保存：

```cpp
class BackendFunction {
    const IR::Function* source;
    IRFunctionType* signature;
    std::vector<IRType*> parameterTypes;
    IRType* returnType;
    String sourceName;
    String linkageName;
    Linkage linkage;
    std::unordered_map<const IRValue*, IRType*> valueTypes;
    ABIInfo abi;
    GCInfo gcInfo;
};
```

它不拥有 `IR::Function`，也不保存 `llvm::Function`。函数体仍由 `source` 中的 Block 和
Instruction 表达；BackendFunction 只提供它们所需的 storage/ABI 描述。

### BackendModule

`BackendModule`（过渡期可称 `LLVMModule`）至少保存：

```cpp
class BackendModule {
    const IR::Module* source;
    std::vector<std::unique_ptr<BackendFunction>> functions;
    std::unordered_map<StructDeclId, IRStructType*> structTypes;
    RuntimeDeclarations runtime;
    ModuleLinkageInfo linkage;
};
```

它负责聚合 lowered 函数、结构体布局、runtime 声明、GC 类型描述、using/import 产生的
符号和模块级冲突检查，但不保存 `llvm::Module`。

### LLVM Codegen

LLVM Codegen 只消费 Backend IR：

```cpp
class LLVMModuleEmitter {
    BackendModule* source;
    llvm::Module* module;
};

class LLVMFunctionEmitter {
    BackendFunction* source;
    llvm::Function* function;
    std::unordered_map<const IR::Block*, llvm::BasicBlock*> blocks;
    std::unordered_map<const IR::IRValue*, llvm::Value*> values;
};
```

LLVM emitter 可以遍历 `BackendFunction::source` 的原始函数体，但不得重新执行语义类型
推导或调用 `TypeInfo::toIRType()`。

## LoweringSession

`LoweringSession` 是所属 Program 持有的持久桥梁：引用源 Semantic IR 与该 Program 的
`IRContext`，拥有最终的 `BackendModule`（其内部拥有 `BackendFunction`），并保留转换映射。
Session 不直接拥有 `IRType`；基础及派生类型由 `IRContext` 持有，命名结构体 storage 类型由
所属 Module 的 `NamingContext` 持有，
Session 中的类型指针和映射都是非拥有引用：

```cpp
class LoweringSession {
    const StructResolver& structResolver;
    IRContext& programContext;
    std::unordered_map<const TypeInfo*, IRType*> typeMap;
    std::unordered_map<StructDeclId, IRStructType*> structMap;
    std::unordered_map<const IR::Function*, BackendFunction*> functionMap;
    std::unordered_map<const IR::Module*, std::unique_ptr<BackendModule>> backendModules;

public:
    LoweringSession(const StructResolver& source, IRContext& programContext);

    IRType* lowerType(const TypeInfo* type);
    BackendFunction* lowerFunction(const IR::Function& function);
    BackendModule* lowerModule(const IR::Module& module);
};
```

`backendModules` 是结果的唯一拥有者；`functionMap` 和 `structMap` 只索引已有对象，
`BackendModule::functions` 拥有函数描述，`IRContext` 拥有基础及派生类型，`NamingContext`
拥有命名结构体类型。同一源模块重复 lowering
应返回同一结果，不得创建另一套 Backend 对象或结构体类型。Program 必须先构造 context
与语义模块，再建立 Session；销毁或重置时先销毁 Session，再销毁语义模块与 context，避免
BackendModule 的 `source` 和类型指针悬空。多模块结构体解析应覆盖所属 Program 内的声明。

当前不需要 `valueMap` 或 `blockMap`，因为不创建平行的 Value、Instruction 或 Block。
`BackendFunction` 可以在 lowering 函数体时建立 `valueTypes` side table。若未来确实要
引入独立后端指令对象，再单独扩展 session，不提前设计完整对象映射。

Program 内的类型转换统一通过其持久 Session，避免临时 Session 丢失结构体身份映射。
若保留无状态的基础类型转换便捷入口，其结果仍由 Program 的 context 持有，且不能
用于依赖结构体声明或 Session 缓存的类型图。

## 分阶段迁移

### 阶段 0：冻结边界

- 统计 `getType()`、`toIRType()`、`toLLVMType()` 和 `IRTypeID` 判断。
- 禁止新增 `TypeInfo::toIRType()` 调用。
- 明确 `getSemanticType()` 与 `getStorageType()` 的调用边界。
- 增加过渡期断言：有语义类型的值，其 storage type 必须等于所属 Program 的 context 中的
  lowering 结果。

验收：新增代码不扩大 Semantic、Backend 和 LLVM 之间的跨层依赖。

### 阶段 1：完成 TypeInfo 只读接口

当前验收结果：通过（Context/Generator 层）。数组、指针、引用、结构体 ID 和
FunctionTypeInfo 的访问与驻留已验证；基础工厂拒绝复合 ID，复合工厂拒绝空组件和跨
IRContext 组件，nullable 只允许结构体和数组。Program::validateSemanticType 在语义阶段
递归检查声明 ID、字段引用与 context 归属，不创建 Backend 类型。TypeInfoContext 的
makeStructTypeID 仍只负责驻留，不单独解析声明；必须由 Program 语义验证完成注册表检查。
重复模块 ID、重复结构体名称和重复语义声明补全均被拒绝。

- 为数组、指针、引用、结构体和函数签名提供稳定的只读结构访问。
- 引入 `FunctionTypeInfo`，把函数参数、返回类型、nullable 和可赋值性判断集中到语义类型。
- 引入稳定的 `StructDeclId`，使 `StructTypeInfo` 只保存语义声明身份，而不是
  `IRStructType*`。
- 将 `TypeInfoContext` 中以 `IRStructType*` 为 key 的驻留表改为
  `StructDeclId -> StructTypeInfo`，保证同一 semantic context 内结构体类型的 canonical
  identity。
- 建立 `StructDeclId -> StructDecl` 语义声明注册表；若由 `TypeInfoContext` 同时持有，则将其
  逐步改名为 `SemanticTypeContext`。
- 将 `IRStructDecl` 改为只保存语义字段、默认值和声明完成状态；结构体查找归 Semantic
  module/program 管理。

验收：Lowering 不访问 `TypeInfo` 私有成员；语义代码不需要读取 `IRType` 才能完成类型判断；
重复 ID、重复定义、不属于当前 Program 的 ID 和无法解析的字段类型能在语义阶段被发现。

### 阶段 2：实现独立 TypeLowering

- 将 `TypeInfo::toIRType()` 的逻辑迁移到所属 Program 的持久
  `LoweringSession::lowerType(const TypeInfo*)`。
- 使用 `LoweringSession` 的 `typeMap` 缓存递归类型转换；结果由所属 Program 的 `IRContext`
  或声明所属 Module 的 `NamingContext` 持有，字段类型不得混入其他 Program 的 context。
- 一个 Program 复用同一个 Session；不在每次类型转换时临时创建 Session。
- 结构体通过 `lowerType()` 按需转换；使用 `structMap` 在递归转换字段前登记 opaque 类型，
  并支持语义声明完成后在同一类型对象上补齐布局，无需独立的 `prepareStructs()`。
- 处理基础类型、数组、指针、引用、结构体、函数类型和 nullable 表示。
- `LoweringSession` 只读访问 semantic context/`StructResolver`，建立自己的
  `StructDeclId -> IRStructType*` 结果映射，不修改语义声明注册表。
- 为独立 `lowerType()` 提供 `StructResolver` 或语义源上下文，不能依赖 Backend 类型指针
  反向查找声明。
- `TypeInfo::toIRType()` 保留为 deprecated 兼容接口，最终删除。

验收：所有 `TypeInfo -> IRType` 转换集中在 Lowering；同一 Program 中重复转换复用类型
及结构体身份，已缓存的 opaque 类型可以在语义声明完成后补齐布局；Session 销毁不释放
`IRContext` 或 `NamingContext` 拥有的类型。

### 阶段 3：Semantic IR 优先

当前实现状态：语义判断的 storage 依赖已移除。Generator 不再读取 getType()、
getStorageType()、IRTypeID 或从 IRType 反推 TypeInfo。索引、解引用、字段访问、初始化、
return、数值提升、GC 内部地址检查和函数重载均依据 TypeInfo；调用依据 FunctionTypeInfo，
CallableValue 仅接受语义签名。运行时函数显式声明语义签名，print/println 的装箱泛型参数
是显式的语义调用规则，不通过 storage 签名推断。

sizeof 仍保持既有支持范围，通过 inferExprType 检查类型而不求值；typeof 与类型描述值
使用 TypeID::TypeDescriptor，lowering 后对应 IRTypeInfoType。常量的语义类型从内容 variant
读取，不从 storage 类型推断。旧 Function/Instruction 构造参数及常量 storage 缓存仍保留，
仅在语义检查后通过 Session lowering 获取；删除这些构造参数属于阶段 4，常量完整迁移
属于阶段 6，不应将本阶段完成理解为整个 Semantic 目录不再引用 Backend 类型。

验收覆盖：Context 驻留与 nullable、语义声明引用检查、无 storage 的语义函数调用、
数值/指针重载、字符串及数组索引、引用与解引用、字段访问、非法索引/return/参数、
sizeof、typeof、GC 内部地址，以及既有 match/repeat 和 runtime 字符串调用样例。

- 将 `inferExprType()` 改为返回 `TypeInfo*`。
- 将数值提升、`sizeof`、`typeof`、数组索引、指针/引用、结构体访问、return、变量初始化
  和函数参数匹配改为只读取 `TypeInfo`。
- 语义函数调用通过 `FunctionTypeInfo` 检查参数个数、参数匹配和返回类型。
- `CallableValue` 只作为过渡适配层，禁止新增依赖；迁移完成后删除其 storage 类型依赖。

验收：语义推导和可调用性判断不使用 `IRTypeID`、`getStorageType()` 或 `toIRType()`。

### 阶段 4：整理现有 IR model

- 保留当前 `Function`、`Module`、`Block`、`Instruction` 和 `Value`，不新增平行 Semantic
  model 或 Storage model。
- 让 `Function` 保存语义函数签名；storage 签名改由 lowering 生成并放入 BackendFunction。
- 让 Instruction/Value 的语义字段使用 `TypeInfo*`；过渡期 storage type 只能作为明确命名的
  缓存，或迁移到 BackendFunction 的 side table。
- 让结构体声明和字段语义类型由 Semantic model 管理；`IRStructType` 只保留 Backend 字段
  storage 类型，不再保存 `semanticType`。
- 将 `NamingContext`/结构体声明注册表从 Backend context 的职责中移出，并由
  `SemanticTypeContext` 或 Semantic module/program 持有。

验收：现有 IR model 可以独立完成语义验证；Backend 类型信息不再成为函数和指令构造的必需输入。

### 阶段 5：实现 BackendFunction/BackendModule lowering

- 新增 Backend IR 目录和 `BackendFunction`、`BackendModule`。
- 实现 `IRFunction -> BackendFunction`：函数签名、参数、返回类型、linkage、ABI、GC/runtime
  描述以及 value type side table。
- 实现 `IRModule -> BackendModule`：函数聚合、结构体布局、runtime 声明、using/import 和
  模块级符号检查。
- 由持久 Session 拥有 `BackendModule`；`BackendModule` 拥有 `BackendFunction`，两者对
  Semantic IR 及 `IRType` 均只保存非拥有引用。
- 根据 `StructDeclId` 建立和索引 lowered `IRStructType`；类型由声明所属 Module 的
  `NamingContext` 拥有，结构体字段布局使用所属 Program 的基础及派生类型。
- 结构体 lowering 复用 `lowerType()` 的按需转换流程；需要聚合全部结构体声明时，枚举
  声明并逐个调用该入口，无需单独的批量准备阶段。semantic context 和 BackendModule
  各自保留独立的 ID 映射。
- Backend 对象引用现有函数体，不复制 Value、Instruction 或 Block。

验收：LLVM Codegen 可以只接收由 Session 持有的 BackendModule；Backend IR 不包含
LLVM API 类型，Session 与 Program 的销毁顺序不会留下悬空引用。

### 阶段 6：重构 Constant 和 runtime type info

- Semantic 常量以 `TypeInfo*` 表达其语义类型；null、字符串、数组和 GC 值的 storage 表示
  由 lowering/Backend 描述决定。
- 区分编译器 `TypeInfo`、运行时 `Runtime::TypeInfo` 和 `IRTypeInfoType`。
- LLVM Codegen 不直接读取编译器 `TypeInfo*`。

验收：常量语义和 storage 表示之间只有 Lowering/Backend 边界。

### 阶段 7：LLVM 类型映射外移

- 新增 `Compiler/LLVMCodegen/LLVMTypeMapper.hpp/.cpp`：

```cpp
llvm::Type* map(IRType* type, llvm::LLVMContext& context);
```

- 将 ABI 参数、返回值、结构体布局和 runtime 类型映射集中到 LLVM Codegen。
- 最终删除 `IRType::toLLVMType()`，使 `Compiler/IR` 不再包含 LLVM 头文件。

验收：`IRType -> llvm::Type*` 只发生在 LLVM Codegen。

### 阶段 8：删除兼容层

删除或完成迁移：

```text
TypeInfo::toIRType()
IRValue::getType()
IRType::toLLVMType()
CallableValue 的 storage 类型依赖
IRStructType::FieldInfo::semanticType
```

最终保留的 Lowering 边界为：

```text
TypeInfo   -> IRType
IRFunction -> BackendFunction
IRModule   -> BackendModule
```

## 测试与验收

1. 同一 `IRContext` 中 TypeInfo/IRType 的 canonical identity。
2. 不同 Program 各自的 `IRContext` 中的类型不会错误相等或被交叉使用。
3. `T` 与 `T?` 的语义类型不同，但可以按 lowering 策略得到相同 storage type。
4. 函数重载和参数匹配只依赖 `TypeInfo`/`FunctionTypeInfo`。
5. 结构体声明身份、字段语义类型和 lowered storage 布局一致；递归/前向结构体可以先建立
   Semantic declaration，再通过 `lowerType()` 按需完成 Backend lowering；首次转换得到
   opaque 类型时，语义声明完成后的后续转换复用原类型对象并补齐布局。
6. 同一 Program 中同一个 Semantic struct 重复 lowering 时复用该 Program 的
   `IRStructType`；不同 Program 的结构体不共享 Backend 类型指针。
7. 同一 Program 中同一个 `StructDeclId` 得到同一个 `StructTypeInfo`；重复定义、
   不属于该 Program 的 ID 和无法解析的 ID 会被拒绝。
8. Lowering 后语义声明表保持不变，`LoweringSession`/`BackendModule` 中建立独立的
   `StructDeclId -> IRStructType*` 映射；同一 Program 中重复 lowering 复用 Session 拥有的
   BackendModule；基础及派生类型由 Program 的 `IRContext` 唯一拥有，命名结构体类型由
   声明所属 Module 的 `NamingContext` 唯一拥有。
9. BackendFunction 的函数签名、参数和 value type side table 与源 IR 一致。
10. BackendModule 可以独立 dump 和验证模块级函数、结构体和 runtime 描述。
11. LLVM Codegen 不接触编译器 `TypeInfo`，只消费 Backend IR 和 `IRType`。
12. LLVM emitter 能为 BackendFunction 中的原始 Block/Instruction 建立完整 LLVM 映射。

## 最终工作流

```text
AST
  -> Semantic Analysis
  -> 现有 IR Model(TypeInfo)
  -> Semantic Verification
  -> Lowering
       TypeInfo   -> IRType
       IRFunction -> BackendFunction
       IRModule   -> BackendModule
  -> Backend IR Verification
  -> LLVMTypeMapper / LLVMFunctionEmitter / LLVMModuleEmitter
  -> LLVM IR
```

Backend IR 是稳定的后端描述层，不是另一套完整前端 IR。Value、Instruction 和 Block
继续由现有 IR model 表达；Lowering 只生成类型、函数和模块级后端信息；LLVM emitter
负责最终 LLVM 对象和指令生成。
