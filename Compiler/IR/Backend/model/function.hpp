#ifndef SAKURAE_BACKEND_FUNCTION_HPP
#define SAKURAE_BACKEND_FUNCTION_HPP

#include "includes/String.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sakuraE::IR {
    class Function;
    class IRValue;
    class Instruction;
    class IRType;
    class IRFunctionType;
    class IRStructType;
    class BackendModule;

    // Backend IR 的函数级模型：IRFunction 经 LoweringSession lowering 后得到 BackendFunction，
    // 它不复制指令，而是以语义 IR 值指针为键，附加 storage 类型、运行时类型元数据、ABI 与 GC root 等后端元数据，
    // 供 FunctionEmitter/LLVMCodeGenerator 只读消费。
    // 函数的链接方式：Definition 为本模块定义并生成函数体；External 为外部声明（__runtime 模块中的运行时函数）
    enum class Linkage { Definition, External };
    // 值在函数边界（参数/返回值）上的 ABI 形态：
    // Void 表示无值；Raw 表示直接按 storage 类型传递原始值；Boxed 表示传递指向运行时装箱值的不透明指针
    enum class ABIValueKind { Void, Raw, Boxed };

    // 运行时类型描述的种类，与语义类型一一对应；Descriptor 表示类型描述符(typeinfo) 值本身
    enum class RuntimeTypeKind {
        Void, I8, I32, I64, U32, U64, F32, F64, Bool, String,
        Pointer, Reference, Array, Struct, Function, Descriptor
    };
    // 装箱值携带的运行时类型标签，数值需与 Runtime/value.h 中 RuntimeType 的枚举值保持一致
    // （3 对应运行时的 U8，此处未使用）。引用复用 Pointer 标签；None 表示该类型不能装箱
    enum class RuntimeValueTag : std::uint8_t {
        I8 = 0, I32 = 1, I64 = 2, U32 = 4, U64 = 5, F32 = 6,
        F64 = 7, Bool = 8, String = 9, Array = 10, Struct = 11,
        Pointer = 12, Descriptor = 13, None = 255
    };
    // 值在后端的表示策略：
    // Boxed —— 运行时装箱值（标量、字符串、指针、类型描述符），跨边界需装箱/拆箱；
    // ManagedArray —— GC 托管的数组对象；Reference —— 指向存储槽位的引用；
    // Handle —— 结构体句柄（指向原始结构体布局的指针，可为 null）；NonValue —— void/函数等不作为值存在的类型
    enum class ValueRepresentation { Boxed, ManagedArray, Reference, Handle, NonValue };
    // 常量的后端种类，由常量的语义类型决定；Null 表示空句柄常量（如空数组/空结构体句柄）
    enum class ConstantKind { Null, I8, I32, I64, U32, U64, F32, F64, Bool, String, Array, Descriptor };

    // 运行时类型元数据的后端描述，由 LoweringSession 按语义类型(TypeInfo) 缓存并拥有，同一语义类型只对应一个描述；
    // 所有图边（element）以及 BackendFunction 对它的引用都是非拥有的。
    struct RuntimeTypeDescription {
        // 描述种类
        RuntimeTypeKind kind = RuntimeTypeKind::Void;
        // 装箱时使用的运行时标签；None 表示不可装箱
        RuntimeValueTag tag = RuntimeValueTag::None;
        // 值的后端表示策略
        ValueRepresentation representation = ValueRepresentation::NonValue;
        // 对应的 storage 类型（lowerType 的结果，非拥有）。字符串与 char 指针 storage 相同但描述不同
        IRType* storage = nullptr;
        // 指针/引用的被指类型或数组的元素类型描述；其他种类为 nullptr
        const RuntimeTypeDescription* element = nullptr;
        // 数组长度；非数组为 0
        std::uint64_t count = 0;
        // 结构体的名义身份 "模块:局部ID:结构体名"；当前运行时结构体工厂按名义识别、没有字段图 API
        fzlib::String identity;

        // 是否为 GC 托管对象（字符串或数组）：托管值需要注册为 GC root
        bool managed() const {
            return kind == RuntimeTypeKind::String || kind == RuntimeTypeKind::Array;
        }
    };

    // 字段 storage 策略。字段存放在原始结构体布局中，绝不放进 RuntimeValue 包装槽位：
    // Scalar 为内联标量；Pointer 为指针（含字符串 char*）；Reference 为引用；
    // EmbeddedStruct / EmbeddedArray 为按值内嵌的结构体/数组
    enum class FieldStorageKind { Scalar, Pointer, Reference, EmbeddedStruct, EmbeddedArray };

    // lowering 时把 gmem 指令的字符串字段选择器解析成的字段访问描述；emitter 只读取布局、下标和 storage 策略
    struct BackendFieldAccess {
        // 基对象的结构体布局（非拥有），必须已完整
        IRStructType* layout = nullptr;
        // 字段在布局中的下标，即 LLVM struct GEP 的成员序号
        std::size_t index = 0;
        // 字段值的运行时类型描述（非拥有）
        const RuntimeTypeDescription* type = nullptr;
        // 字段在原始布局中的存储策略
        FieldStorageKind storage = FieldStorageKind::Scalar;
    };

    // 函数签名的 ABI 形态：由 LoweringSession 根据函数原始名与 storage 类型决定，
    // 运行时/GC 内建函数使用 Raw，其余非 void 值默认 Boxed
    struct ABIInfo {
        // 返回值 ABI
        ABIValueKind result = ABIValueKind::Void;
        // 每个形参的 ABI，顺序与 parameters 一致
        std::vector<ABIValueKind> parameters;
    };

    // 函数的 GC root 元数据
    struct GCInfo {
        // 每个形参是否为托管值，需在函数入口把参数槽位注册为 GC root
        std::vector<bool> parameterRoots;
        // 需要作为 GC root 追踪的托管值：非指令值，或 constant/create_array/call/load 产生的托管值
        std::unordered_set<const IRValue*> rootedValues;
        // 需要把存储槽位注册为 GC root 的 create_alloca 指令（其值类型为托管类型）
        std::unordered_set<const IRValue*> rootedSlots;
    };

    // 单个函数的 Backend IR 描述。由 BackendModule 通过 unique_ptr 拥有；
    // 所有指针字段与表中的键/值都是非拥有引用（指向语义 IR、IRContext 驻留类型或 Session 拥有的描述）
    class BackendFunction {
    public:
        // 对应的语义 IR 函数
        const Function* source = nullptr;
        // 所属 BackendModule
        BackendModule* parent = nullptr;
        // 源码函数名（语义 IR 中的名字）
        fzlib::String sourceName;
        // 链接符号名：External 且有原始名时使用原始名，否则与 sourceName 相同
        fzlib::String linkageName;
        // 链接方式
        Linkage linkage = Linkage::Definition;
        // 函数签名 storage 类型，与语义 IR 函数值本身的 storage 类型一致
        IRFunctionType* signature = nullptr;
        // 形参 (名字, storage 类型)，顺序与源函数形参一致
        std::vector<std::pair<fzlib::String, IRType*>> parameters;
        // 返回值 storage 类型，与 signature 的返回类型相同
        IRType* returnType = nullptr;
        // 参数/返回值 ABI 与 GC root 元数据
        ABIInfo abi;
        GCInfo gcInfo;
        // 以下表以语义 IR 值为键，覆盖函数体可达的全部值（函数本身、块、指令、常量及其操作数）：
        // 值 -> storage 类型
        std::unordered_map<const IRValue*, IRType*> valueTypes;
        // 值 -> 运行时类型描述
        std::unordered_map<const IRValue*, const RuntimeTypeDescription*> runtimeTypes;
        // 类型操作数：sizeof 指令与类型描述符常量 -> 其操作的类型描述
        std::unordered_map<const IRValue*, const RuntimeTypeDescription*> typeOperands;
        // 常量 -> 后端常量种类
        std::unordered_map<const IRValue*, ConstantKind> constants;
        // gmem 指令 -> 解析后的字段访问
        std::unordered_map<const Instruction*, BackendFieldAccess> fieldAccesses;
        // 每个形参的运行时类型描述，顺序与 parameters 一致
        std::vector<const RuntimeTypeDescription*> parameterRuntimeTypes;
        // 返回值的运行时类型描述
        const RuntimeTypeDescription* returnRuntimeType = nullptr;

        // 查询值的 storage 类型；值未 lowering 时抛异常
        IRType* storageType(const IRValue* value) const;
        // 查询值的运行时类型描述；值未 lowering 时抛异常
        const RuntimeTypeDescription& runtimeType(const IRValue* value) const;
        // 查询 gmem 指令的字段访问；缺失时抛异常
        const BackendFieldAccess& fieldAccess(const Instruction* instruction) const;
        // 该值是否需要作为 GC root 追踪
        bool tracksRoot(const IRValue* value) const;
        // 该 alloca 槽位是否需要注册为 GC root
        bool rootsSlot(const IRValue* value) const;
    };
}

#endif
