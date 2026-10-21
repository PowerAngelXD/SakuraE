#ifndef SAKURAE_SEMANTIC_NAMING_CONTEXT_HPP
#define SAKURAE_SEMANTIC_NAMING_CONTEXT_HPP

#include "Compiler/IR/Semantic/model/struct.hpp"

#include <map>
#include <memory>

namespace sakuraE::IR {
    class IRContext;
    class IRStructType;

    // 模块级命名空间，由 Semantic Module 持有，构成命名结构体的模块级边界。
    // 拥有两张职责不同的表：
    // - structDecls：结构体名称 -> 语义声明 IRStructDecl（字段、默认值、完成状态）；
    // - structTypes：StructDeclId -> 命名结构体 storage 类型 IRStructType，供 lowering 按需创建。
    // IRStructDecl 不拥有 IRStructType；两者都由本对象拥有，对外返回的指针均为非拥有引用，
    // 销毁 NamingContext 前必须停止使用相关 Session 与类型指针。
    // 作为 StructDeclRegistry 为 lowering 提供只读的声明解析。
    class NamingContext : public StructDeclRegistry {
        fzlib::String moduleID;   // 所属模块 ID，写入每个 StructDeclId
        std::map<fzlib::String, std::unique_ptr<IRStructDecl>> structDecls;   // 语义声明表（按名称）
        // 命名结构体 storage 类型表。由 lowering 通过 const 的 getStructTy 延迟填充，故为 mutable；
        // 语义声明的解析不受其影响。
        mutable std::map<StructDeclId, std::unique_ptr<IRStructType>> structTypes;
        // storage 类型表首次使用时绑定的 IRContext；之后拒绝其他 context 的请求，禁止跨 context 复用。
        mutable IRContext* typeContext = nullptr;

    public:
        // id 为模块 ID。
        explicit NamingContext(fzlib::String id);
        // 所属模块 ID。
        const fzlib::String& moduleId() const { return moduleID; }
        // 析构定义在 .cpp：此处 IRStructType 只有前置声明，unique_ptr 需要完整类型才能析构。
        ~NamingContext() override;

        // 拥有声明与类型对象，禁止拷贝。
        NamingContext(const NamingContext&) = delete;
        NamingContext& operator=(const NamingContext&) = delete;

        // 按名称查找语义声明；不存在返回 nullptr。
        IRStructDecl* lookupStructDecl(const fzlib::String& name) const;
        // StructDeclRegistry 接口：按 StructDeclId 解析本命名空间内的声明；不存在返回 nullptr。
        const IRStructDecl* resolveStruct(const StructDeclId& id) const override;
        // StructDeclRegistry 接口：声明属于本命名空间时返回 this，否则返回 nullptr。
        const NamingContext* resolveNamingContext(const StructDeclId& id) const override;
        // 全部语义声明（按名称排序）。
        const std::map<fzlib::String, std::unique_ptr<IRStructDecl>>& declarations() const {
            return structDecls;
        }
        // 按名称取声明 ID；未知名称属于内部错误，抛 std::invalid_argument。
        StructDeclId lookupStructId(const fzlib::String& name) const;
        // 注册前置（opaque）声明，字段稍后通过 implStruct 补全；同名重复定义报 IR 生成错误。
        IRStructDecl* declareOpaqueStruct(fzlib::String name, PositionInfo info);
        // 补全已前置声明的结构体字段与默认值；未声明或已完成的结构体报 IR 生成错误。
        void implStruct(fzlib::String name, std::vector<SemanticField> fields,
                        std::map<fzlib::String, Constant*> defaults, PositionInfo info);
        // 一次性声明并补全结构体（无默认值）；同名重复定义报 IR 生成错误。
        IRStructDecl* defineStruct(fzlib::String name, std::vector<SemanticField> fields, PositionInfo info);

        // Lowering 桥接接口：storage 类型始终由本模块命名空间拥有。
        // 获取或创建声明对应的 opaque IRStructType（布局由 lowering 补齐）；
        // 未知声明或 context 与已绑定的不一致时抛 std::invalid_argument。
        IRStructType* getStructTy(IRContext& context, const StructDeclId& id) const;
        // 按名称查询已创建的 storage 类型，不触发创建/lowering；尚未创建时返回 nullptr。
        IRStructType* lookupStructType(const fzlib::String& name) const;
    };

}

#endif
