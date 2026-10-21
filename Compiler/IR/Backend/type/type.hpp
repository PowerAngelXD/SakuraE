#ifndef SAKURAE_TYPE_HPP
#define SAKURAE_TYPE_HPP

#include <Compiler/Error/error.hpp>
#include <map>

#include <optional>
#include <vector>

#include "includes/String.hpp"

// Backend storage 类型（IRType）体系。
// IRType 描述值在后端/LLVM 中的存储布局，是 lowering 的产物：
// 语义类型(TypeInfo) 经 LoweringSession::lowerType 转换为 IRType，再由 LLVMCodegen 映射为 llvm::Type。
// 除 IRStructType 外，所有 IRType 都由 IRContext 驻留（intern）：相同结构只创建一个实例，
// 因此在同一 IRContext 内可以直接用指针相等判断 storage 类型是否相同。
namespace sakuraE::IR {
    class IRContext;
    class TypeInfo;
    class Constant;
    // IRType 的种类标识，用于 isXxx 判定与 static_cast 前的分派。
    // 定宽整数/浮点各有独立 ID；IntegerNTyID / UIntegerNTyID / FloatNTyID 表示任意位宽，需配合 getBitWidth 区分。
    // 注意：Bool(i1) 与 Char(i8) 由有符号 IRIntegerType 按位宽映射得到，不单独建类。
    enum IRTypeID {
        VoidTyID,
        Integer32TyID,
        Integer64TyID,
        IntegerNTyID,
        UInteger32TyID,
        UInteger64TyID,
        UIntegerNTyID,
        Float32TyID,
        Float64TyID,
        FloatNTyID,
        CharTyID,
        BoolTyID,
        // 保留的 ID：没有任何 IRType 使用它，字符串的 storage 类型是 char*（见 getStringTy）
        StringTyID,
        // 类型描述符(typeinfo) 的 storage 类型，对应运行时类型元数据句柄
        TypeInfoTyID,
        // 复合类型
        RefTyID,
        PointerTyID,
        ArrayTyID,
        StructTyID,

        // 函数签名与基本块类型，仅供 IR 内部使用，不作为普通值的 storage 类型
        FunctionTyID,
        BlockTyID
    };

    // 数值类型的提升等级：等级越高表示越"宽"（bool < char < u32 < i32 < u64 < i64 < f32 < f64）。
    // 仅覆盖 bool/char/定宽整数/定宽浮点；任意位宽与复合类型不在表中。
    // 当前仓库中没有任何代码读取该表；声明为 static，每个包含该头文件的翻译单元各持有一份副本。
    static std::map<IRTypeID, int> rankList = {
        {BoolTyID, 1},
        {CharTyID, 2},
        {UInteger32TyID, 3},
        {Integer32TyID, 4},
        {UInteger64TyID, 5},
        {Integer64TyID, 6},
        {Float32TyID, 7},
        {Float64TyID, 8},
    };

    // 所有 storage 类型的抽象基类。实例只能由 IRContext（或 NamingContext 对结构体）创建，
    // 外部只持有非拥有的 IRType* 引用；irTypeID 在构造后不可变。
    class IRType {
    protected:
        const IRTypeID irTypeID;
        explicit IRType(IRTypeID id) : irTypeID(id) {}

    public:
        virtual ~IRType() = default;

        // 若为指针类型则剥去一层指针返回其元素类型，否则返回自身
        IRType* unwrapPointer();
        // 逐层剥去指针，直到遇到非指针类型或 char*（字符串的 storage）为止；
        // char* 被视为一个整体的 storage 类型，不会继续剥成 char
        IRType* getStorageType();
        // 种类查询；isComplexType 表示指针/数组/引用/结构体这类复合 storage 类型
        IRTypeID getIRTypeID() const { return irTypeID; }
        bool isPointer() { return irTypeID == PointerTyID; }
        bool isRef() { return irTypeID == RefTyID; }
        bool isArray() { return irTypeID == ArrayTyID; }
        bool isStruct() { return irTypeID == StructTyID; }
        bool isComplexType() { return isPointer() || isArray() || isRef() || isStruct(); }
        // 结构相等判定（实现见 type.cpp）：先比指针身份，再按种类逐项递归比较
        bool isEqual(IRType* ty);

        // 生成用于 IR 文本输出/调试的类型名
        virtual fzlib::String toString() = 0;

        // 便捷工厂：全部转发到 IRContext::current()，返回当前活动上下文中驻留的唯一实例；
        // 没有活动 IRContext 时会抛出异常。getStringTy 返回 char*（字符串的 storage 类型）
        static IRType* getVoidTy();
        static IRType* getBoolTy();
        static IRType* getCharTy();
        static IRType* getInt32Ty();
        static IRType* getInt64Ty();
        static IRType* getIntNTy(unsigned bitWidth);
        static IRType* getUInt32Ty();
        static IRType* getUInt64Ty();
        static IRType* getUIntNTy(unsigned bitWidth);
        static IRType* getFloat32Ty();
        static IRType* getFloat64Ty();
        static IRType* getTypeInfoTy();
        static IRType* getStringTy();
        static IRType* getPointerTo(IRType* elementType);
        static IRType* getRefTo(IRType* elementType);
        static IRType* getArrayTy(IRType* elementType, uint64_t numElements);

        static IRType* getBlockTy();
        static IRType* getFunctionTy(IRType* returnType, std::vector<IRType*> params);
    };

    // void 类型，只用于函数返回值等"无值"位置
    class IRVoidType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRVoidType() : IRType(VoidTyID) {}
    public:
        fzlib::String toString() override;
    };

    // 浮点类型；32/64 位映射到 Float32TyID/Float64TyID，其它位宽归入 FloatNTyID
    class IRFloatType : public IRType {
        friend class IRType;
        friend class IRContext;
        // 浮点位宽
        unsigned bitWidth;

        // 用立即调用的 lambda 在基类初始化前按位宽选出 IRTypeID
        IRFloatType(unsigned bw)
            : IRType([&]() -> IRTypeID{
                switch (bw) {
                    case 32: return IRTypeID::Float32TyID;
                    case 64: return IRTypeID::Float64TyID;
                    default: return IRTypeID::FloatNTyID;
                }
            }()), bitWidth(bw) {}
    public:
        // 浮点位宽访问器
        unsigned getBitWidth() const { return bitWidth; }
        fzlib::String toString() override;
    };

    // 整数类型（同时承载 bool 与 char）。
    // 有符号：1 位 -> Bool，8 位 -> Char，32/64 位 -> 定宽整数，其它 -> IntegerNTyID；
    // 无符号：仅 32/64 位有定宽 ID，其它 -> UIntegerNTyID（无符号 1/8 位不会映射成 Bool/Char）。
    class IRIntegerType : public IRType {
        friend class IRType;
        friend class IRContext;
        // 是否无符号。注意：构造函数并未写入该字段（始终为默认 false），符号信息实际只体现在 irTypeID 中
        bool isUnsigned = false;
        // 整数位宽
        unsigned bitWidth;

        // sign 为 true 表示有符号；通过立即调用的 lambda 计算 IRTypeID
        explicit IRIntegerType(unsigned bw, bool sign = true):
            IRType([&]()->IRTypeID {
                if (sign) {
                    switch (bw) {
                        case 1:
                            return IRTypeID::BoolTyID;
                        case 8:
                            return IRTypeID::CharTyID;
                        case 32:
                            return IRTypeID::Integer32TyID;
                        case 64:
                            return IRTypeID::Integer64TyID;
                        default:
                            return IRTypeID::IntegerNTyID;
                    }
                }
                else {
                    switch (bw) {
                        case 32:
                            return IRTypeID::UInteger32TyID;
                        case 64:
                            return IRTypeID::UInteger64TyID;
                        default:
                            return IRTypeID::UIntegerNTyID;
                    }
                }
            }()), bitWidth(bw) {}

    public:
        // 整数位宽访问器
        unsigned getBitWidth() const { return bitWidth; }
        fzlib::String toString() override;
    };

    // 类型描述符的 storage 类型（toString 为 "tinfo"），值在后端表示运行时类型元数据(RuntimeTypeInfo)
    class IRTypeInfoType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRTypeInfoType() : IRType(TypeInfoTyID) {}
    public:
        fzlib::String toString() override;
    };

    // 指向 elementType 的原始指针类型；由 IRContext 按元素类型驻留
    class IRPointerType : public IRType {
        friend class IRType;
        friend class IRContext;
        // 被指向的元素 storage 类型（非拥有引用）
        IRType* elementType;

        explicit IRPointerType(IRType* elementTy) : IRType(PointerTyID), elementType(elementTy) {}

    public:
        // 元素类型访问器
        IRType* getElementType() const { return elementType; }
        fzlib::String toString() override;
    };

    // 引用类型（elementType&）；与指针类型分开驻留，以便后端区分引用语义
    class IRRefType : public IRType {
        friend class IRType;
        friend class IRContext;
        // 被引用的元素 storage 类型（非拥有引用）
        IRType* elementType;

        explicit IRRefType(IRType* elementTy) : IRType(RefTyID), elementType(elementTy) {}

    public:
        // 元素类型访问器
        IRType* getElementType() const { return elementType; }
        fzlib::String toString() override;
    };

    // 定长数组类型 elementType[numElements]；由 IRContext 以 (元素类型, 长度) 为键驻留
    class IRArrayType : public IRType {
        friend class IRType;
        friend class IRContext;
        // 数组元素 storage 类型（非拥有引用）
        IRType* elementType;
        // 元素个数
        uint64_t numElements;

        // 私有构造函数
        IRArrayType(IRType* elementTy, uint64_t num)
            : IRType(ArrayTyID), elementType(elementTy), numElements(num) {}

    public:
        // 元素类型 / 元素个数访问器
        IRType* getElementType() const { return elementType; }
        uint64_t getNumElements() const { return numElements; }
        fzlib::String toString() override;
    };

    // IR 内部实现
    // 基本块的类型标记，使 Block 也能作为 IRValue 拥有类型；全局唯一实例
    class IRBlockType : public IRType {
        friend class IRType;
        friend class IRContext;

        explicit IRBlockType() : IRType(BlockTyID) {}
    public:
        fzlib::String toString() override;
    };

    // 函数签名类型：返回类型 + 参数类型列表；由 IRContext 以 (返回类型, 参数列表) 为键驻留
    class IRFunctionType : public IRType {
        friend class IRType;
        friend class IRContext;

        // 参数 storage 类型，按形参顺序排列（非拥有引用）
        std::vector<IRType*> paramsType;
        // 返回值 storage 类型（非拥有引用）
        IRType* returnType;

        explicit IRFunctionType(IRType* ret, std::vector<IRType*> params)
            : IRType(FunctionTyID), paramsType(params), returnType(ret) {}
    public:
        // 打印签名、返回类型与参数类型访问器
        fzlib::String toString() override;
        IRType* getReturnType() const { return returnType; }
        const std::vector<IRType*>& getParamTypes() const { return paramsType; }
    };

    // 具名结构体的 storage 布局。
    // 与其它 IRType 不同，结构体不由 IRContext 驻留，而是由声明所在模块的 NamingContext 按 StructDeclId 拥有
    // （见 NamingContext::getStructTy）。canonical identity 为 (parentModID, name)：
    // 同一声明只对应一个 IRStructType 对象，isEqual 也只比较这两项而不比较字段。
    // 结构体可以先以 opaque（不完整、无字段）状态创建，待语义声明完成后由 lowering 调用 complete 一次性补全布局，
    // 这样递归结构体（经由指针自引用）也能在同一对象上闭合。
    class IRStructType : public IRType {
    public:
        // 单个字段的布局信息：字段名、字段 storage 类型（非拥有引用）、源码位置；
        // 字段在 fields 中的下标即其在 LLVM 结构体中的成员序号
        struct FieldInfo {
            fzlib::String name;
            IRType* type;
            PositionInfo info;
        };

        // 输出 "struct {字段类型, ...}"，只列字段类型，不含结构体名与字段名
        fzlib::String toString() override;

        // 结构体名 / 声明所属模块 ID，二者共同构成结构体的 canonical identity
        const fzlib::String& getName() const;
        const fzlib::String& getModuleID() const { return parentModID; }
        // 字段个数；opaque 状态下为 0
        std::size_t getCount() const;
        // 按字段名查找字段，未找到返回 std::nullopt；返回的是 FieldInfo 副本
        std::optional<FieldInfo> findMember(const fzlib::String& target) const;
        // 按声明顺序排列的全部字段
        const std::vector<FieldInfo>& getFields() const;
        // 将 opaque 结构体补全为完整布局；只能调用一次，字段类型不能为空且字段名不能重复，否则抛异常
        void complete(std::vector<FieldInfo> fs);
        // 是否已有完整布局（false 即 opaque）
        bool isComplete() const;
    private:
        // 只允许 IRType/IRContext/NamingContext 创建实例；实际创建者为 NamingContext
        friend class IRType;
        friend class IRContext;
        friend class NamingContext;
        // 直接以完整字段列表构造：标记为完整，并建立字段名到下标的索引
        explicit IRStructType(fzlib::String modID, fzlib::String n, std::vector<FieldInfo> list):
            IRType(StructTyID), parentModID(modID), name(n), isCompleteType(true), fields(std::move(list)) {
            for (std::size_t i = 0; i < fields.size(); ++i) fieldIndices[fields[i].name] = i;
        }

        // 构造 opaque 结构体：无字段、isCompleteType 为 false，之后通过 complete 补全
        explicit IRStructType(fzlib::String modID, fzlib::String n):
            IRType(StructTyID), parentModID(modID), name(n) {}

        // 声明所在模块 ID
        fzlib::String parentModID;
        // 结构体名
        fzlib::String name;
        // 布局是否完整；false 表示 opaque
        bool isCompleteType = false;
        // 字段名 -> fields 下标，供 findMember 快速查找
        std::map<fzlib::String, std::size_t> fieldIndices;
        // 字段布局，顺序与语义声明一致
        std::vector<FieldInfo> fields;
    };

}

#endif /* !SAKURAE_TYPE_HPP */
