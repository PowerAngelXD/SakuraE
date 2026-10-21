#ifndef SAKURAE_TYPE_INFO_HPP
#define SAKURAE_TYPE_INFO_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <variant>
#include <cstdint>
#include <vector>
#include <utility>

#include "Compiler/Error/error.hpp"
#include "Compiler/Utils/Logger.hpp"

namespace sakuraE::IR {
    class IRContext;
    class TypeInfo;
    class Constant;
    class TypeInfoContext;

    // 结构体声明的稳定身份：所属模块 ID + 模块内局部序号。
    // 语义类型(TypeInfo)只保存该 ID 而不保存声明或 storage 类型指针，
    // 由 NamingContext/StructDeclRegistry 解析声明，由 lowering 在声明所属 NamingContext 中取得 IRStructType。
    struct StructDeclId {
        fzlib::String module;       // 声明所在模块的 ID
        std::uint64_t localId = 0;  // 模块内按声明顺序分配的序号（见 NamingContext）

        bool operator==(const StructDeclId& other) const {
            return module == other.module && localId == other.localId;
        }
        // 按 (module, localId) 字典序比较，用作 std::map 键。
        bool operator<(const StructDeclId& other) const {
            return module < other.module || (module == other.module && localId < other.localId);
        }
    };

    // 结构体的语义字段：字段名、语义类型（非拥有）与声明位置。
    struct SemanticField {
        fzlib::String name;
        TypeInfo* type = nullptr;
        PositionInfo info;
    };

    enum TypeID {
        // 基础类型（由 makeBasicTypeID 创建，无组件）
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float32,
        Float64,
        Bool,
        Char,
        Void,
        String,
        // 复合类型（需要组件类型或声明 ID，不能通过 makeBasicTypeID 创建）
        Array,
        Pointer,
        Ref,
        Struct,
        FunctionType,
        // 类型描述（typeinfo 值的类型）。虽位于此处，但它没有组件，按基础类型处理
        TypeDescriptor
    };

    // 类型限定符：目前只支持 Nullable（T?），用于 nullable 包装 TypeInfo。
    enum class TypeQualifier {
        Normal,
        Nullable
    };

    // 语义可赋值性：source 的值能否赋给 target 类型。
    // 规则：同一 TypeInfo 可赋值；非 nullable 的 T 可赋值给 T?；其余（含 T? -> T）均不可赋值。
    // 空指针一律视为不可赋值。不做数值隐式转换。
    bool isAssignableTo(const TypeInfo* source, const TypeInfo* target);

    // 数组类型的组件：元素语义类型（非拥有）与编译期长度。
    class ArrayTypeInfo {
        TypeInfo* elementType;
        std::uint64_t elementCount;

    public:
        ArrayTypeInfo(TypeInfo* element, std::uint64_t count):
            elementType(element), elementCount(count) {}

        // 长度与元素类型访问器。
        std::uint64_t length() const { return elementCount; }
        TypeInfo* getElementTy() const { return elementType; }
    };

    // 指针类型(T*)的组件：被指向的语义类型（非拥有）。
    class PointerTypeInfo {
        TypeInfo* elementType;

    public:
        explicit PointerTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() const { return elementType; }
    };

    // 引用类型(T&)的组件：被引用的语义类型（非拥有）。
    class RefTypeInfo {
        TypeInfo* elementType;

    public:
        explicit RefTypeInfo(TypeInfo* element): elementType(element) {}

        TypeInfo* getElementTy() const { return elementType; }
    };

    // 结构体类型的组件：只保存 StructDeclId，结构体的字段等信息通过声明注册表解析。
    class StructTypeInfo {
        StructDeclId declaration;

    public:
        explicit StructTypeInfo(StructDeclId id): declaration(std::move(id)) {}

        const StructDeclId& getDeclId() const { return declaration; }
    };

    // 函数类型的组件：返回类型与参数类型列表（均为非拥有引用），即函数签名。
    class FunctionTypeInfo {
        TypeInfo* returnType;
        std::vector<TypeInfo*> argTypes;

    public:
        explicit FunctionTypeInfo(TypeInfo* retTy, std::vector<TypeInfo*> argTys): returnType(retTy), argTypes(std::move(argTys)) {}

        // 签名访问器。
        TypeInfo* getReturnType() const { return returnType; }
        const std::vector<TypeInfo*>& getArgTypes() const { return argTypes; }
        std::size_t getArgCount() const { return argTypes.size(); }
    };

    // 复合类型组件的 variant；基础类型使用 std::monostate。
    using ComplexTypeUnion = std::variant<std::monostate, ArrayTypeInfo, PointerTypeInfo, RefTypeInfo, StructTypeInfo, FunctionTypeInfo>;
    // 语义类型（源语言类型）。可赋值性、nullable、重载与函数签名都以 TypeInfo 为准，
    // lowering 再把它转换为 storage 类型(IRType)。
    // 不变量：TypeInfo 一律由所属 IRContext 的 TypeInfoContext 驻留并拥有，构造函数均为 private；
    // 同一 context 内结构相同的类型只有一个对象，因此可以直接用指针相等判断类型相等。
    // 组件类型必须与自身属于同一 IRContext（由 TypeInfoContext::validateComponent 保证）。
    class TypeInfo {
        friend class TypeInfoContext;

        IRContext& context;                 // 所属 IRContext
        const TypeQualifier qualifier;      // Normal 或 Nullable
        TypeID typeID;                      // 类型种类；nullable 包装与其 base 相同
        ComplexTypeUnion complexTypeInfo;   // 复合类型组件，基础类型为 monostate

        TypeInfo* base = nullptr;           // 仅 nullable 包装使用：指向被包装的非 nullable 类型

        // 基础类型。
        TypeInfo(IRContext& ctx, const TypeID tid): context(ctx), qualifier(TypeQualifier::Normal), typeID(tid) {}

        // 数组类型：element[count]。
        TypeInfo(IRContext& ctx, TypeInfo* element, std::uint64_t count):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(Array),
            complexTypeInfo(ArrayTypeInfo(element, count)) {}

        // 单组件类型：仅允许 Pointer/Ref，其他 TypeID 属于内部错误。
        // 用立即调用的 lambda 在成员初始化列表中按 id 选择 variant 分支。
        TypeInfo(IRContext& ctx, TypeID id, TypeInfo* elementType):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(id),
            complexTypeInfo([&]() -> ComplexTypeUnion {
                switch (id) {
                    case Pointer: return PointerTypeInfo(elementType);
                    case Ref: return RefTypeInfo(elementType);
                    default:
                        throw std::runtime_error(
                            "Cannot use this TypeID to create a single-element TypeInfo");
                }
            }()) {}

        // 函数类型：retTy(argTys...)。
        TypeInfo(IRContext& ctx, TypeInfo* retTy, std::vector<TypeInfo*> argTys):
            context(ctx), qualifier(TypeQualifier::Normal), typeID(FunctionType),
            complexTypeInfo(FunctionTypeInfo(retTy, argTys)) {}

        // 结构体类型：仅记录声明 ID。
        TypeInfo(IRContext& ctx, StructDeclId id)
            : context(ctx), qualifier(TypeQualifier::Normal), typeID(Struct), complexTypeInfo(StructTypeInfo(std::move(id))) {}

        // nullable 包装 T?：复制 base 的 context、TypeID 与组件，并记录 base，
        // 这样 isStruct()/isArray() 等判定对 T? 与 T 一致，而 getBase() 可取回 T。
        // 只支持 Nullable 限定符。
        TypeInfo(TypeInfo* b, const TypeQualifier qk)
            : context(b->context), qualifier(qk), typeID(b->typeID),
              complexTypeInfo(b->complexTypeInfo), base(b) {
            if (qk != TypeQualifier::Nullable) {
                throw std::invalid_argument("Only nullable TypeInfo wrappers are supported");
            }
        }
    public:
        ~TypeInfo() = default;

        // 生成可读的类型名，如 "i32"、"T?"、"i32*"、"i32[4]"、"mod.struct#0"、"func(i32) -> bool"。
        fzlib::String toString() const;

        // 类型种类判定。nullable 包装共享 base 的 TypeID，因此 T? 的判定结果与 T 相同；
        // isBasic 表示非复合类型（包括 Void、String、TypeDescriptor）。
        bool isArray() const { return typeID == Array; }
        bool isPointer() const { return typeID == Pointer; }
        bool isRef() const { return typeID == Ref; }
        bool isStruct() const { return typeID == Struct; }
        bool isFunction() const { return typeID == FunctionType; }
        bool isBasic() const {
            return !isArray() && !isPointer() && !isRef() && !isStruct() && !isFunction();
        }
        // 所属 IRContext 及其 TypeInfoContext（驻留池）。
        IRContext& getContext() const { return context; }
        TypeInfoContext& getTypeContext() const;
        // 函数类型返回签名组件，其他类型返回 nullptr。
        const FunctionTypeInfo* getFunctionTypeInfo() const {
            return isFunction() ? &std::get<FunctionTypeInfo>(complexTypeInfo) : nullptr;
        }
        // 是否为 nullable 包装 T?。
        bool isNullable() const {
            return qualifier == TypeQualifier::Nullable;
        }
        // 类型种类。
        const TypeID& getTypeID() const { return typeID; }

        // 去掉 nullable 包装：T? 返回 T，其他类型返回自身。
        TypeInfo* getBase() {
            return isNullable() ? base : this;
        }

        const TypeInfo* getBase() const {
            return isNullable() ? base : this;
        }

        // 数组元素类型；非数组返回 nullptr。
        TypeInfo* getElementType() const {
            if (!isArray()) return nullptr;
            return std::get<ArrayTypeInfo>(complexTypeInfo).getElementTy();
        }

        // 指针或引用所指向的类型；其他类型返回 nullptr。
        TypeInfo* getPointeeType() const {
            if (isPointer()) {
                return std::get<PointerTypeInfo>(complexTypeInfo).getElementTy();
            }
            if (isRef()) {
                return std::get<RefTypeInfo>(complexTypeInfo).getElementTy();
            }
            return nullptr;
        }

        // 注意：尽管名为 getFunctionType，返回的是函数类型的“返回类型”；非函数类型返回 nullptr。
        TypeInfo* getFunctionType() const {
            if (!isFunction()) return nullptr;
            return std::get<FunctionTypeInfo>(complexTypeInfo).getReturnType();
        }

        // 原始组件 variant。
        const ComplexTypeUnion& getComplexTypeInfo() const { return complexTypeInfo; }

        // 结构体声明 ID；非结构体返回 nullptr。返回指针指向本对象内部，生命周期与 TypeInfo 相同。
        const StructDeclId* getStructDeclId() const {
            if (!isStruct()) return nullptr;
            return &std::get<StructTypeInfo>(complexTypeInfo).getDeclId();
        }

        // 静态便捷工厂：转发到 IRContext::current()（线程局部的活动 context）的 TypeInfoContext。
        // 仅在目标 context 处于活动状态时使用；已持有 IRContext& 的代码应优先使用 typeInfoPool() 的实例工厂。
        static TypeInfo* makeBasicTypeID(const TypeID typeID);
        static TypeInfo* makeArrayTypeID(TypeInfo* element, const std::uint64_t count);
        static TypeInfo* makePointerTypeID(TypeInfo* typeID);
        static TypeInfo* makeRefTypeID(TypeInfo* typeID);
        static TypeInfo* makeStructTypeID(StructDeclId id);
        static TypeInfo* makeFunctionTypeID(TypeInfo* retTy, std::vector<TypeInfo*> argTys);
        static TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        static void clearAll();
    };

    // TypeInfo 的驻留池与工厂，由 IRContext 持有（typeInfoPool()/getTypeInfoManager()）。
    // 每类类型一张表，以组件身份为键，保证同一 context 内的 canonical identity；
    // 表通过 unique_ptr 拥有 TypeInfo，返回的指针均为非拥有引用，在 clear() 或 context 销毁前有效。
    class TypeInfoContext {
        IRContext& context;   // 所属 IRContext，所有组件类型都必须属于它
        std::map<TypeID, std::unique_ptr<TypeInfo>> primaryTypes;                                           // 基础类型
        std::map<std::pair<TypeInfo*, std::uint64_t>, std::unique_ptr<TypeInfo>> arrayTypes;                // (元素, 长度) -> 数组
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> pointerTypes;                                        // 被指类型 -> 指针
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> refTypes;                                            // 被引类型 -> 引用
        std::map<StructDeclId, std::unique_ptr<TypeInfo>> structTypes;                                      // 声明 ID -> 结构体
        std::map<std::pair<TypeInfo*, std::vector<TypeInfo*>>, std::unique_ptr<TypeInfo>> funcTypes;        // (返回, 参数) -> 函数
        std::map<TypeInfo*, std::unique_ptr<TypeInfo>> nullableTypes;                                       // T -> T?
        // 校验组件类型非空且属于本 context，否则抛 std::invalid_argument（拒绝跨 context 组合类型）。
        void validateComponent(const TypeInfo* type) const;

    public:
        explicit TypeInfoContext(IRContext& ctx): context(ctx) {}

        // 驻留池持有唯一对象，禁止拷贝。
        TypeInfoContext(const TypeInfoContext&) = delete;
        TypeInfoContext& operator=(const TypeInfoContext&) = delete;

        // 获取或创建基础类型；传入复合 TypeID（Array/Pointer/Ref/Struct/FunctionType）会被拒绝。
        TypeInfo* makeBasicTypeID(TypeID typeID);
        // 获取或创建数组/指针/引用类型；组件必须非空且属于本 context。
        TypeInfo* makeArrayTypeID(TypeInfo* element, std::uint64_t count);
        TypeInfo* makePointerTypeID(TypeInfo* typeID);
        TypeInfo* makeRefTypeID(TypeInfo* typeID);
        // 获取或创建结构体类型。只负责驻留，不检查声明是否存在，声明校验由 Program::validateSemanticType 完成。
        TypeInfo* makeStructTypeID(StructDeclId id);
        // 获取或创建函数类型；返回类型与每个参数类型都必须非空且属于本 context。
        TypeInfo* makeFunctionTypeID(TypeInfo* retTy, std::vector<TypeInfo*> argTys);
        // 获取或创建 T?；只允许非 nullable 的结构体或数组类型，违规时报 IR 生成错误。
        TypeInfo* wrapTypeAsNullable(TypeInfo* type, PositionInfo info);
        // 释放全部驻留的 TypeInfo；之后所有已返回的 TypeInfo* 均悬空。
        void clear();
    };
}

#endif
