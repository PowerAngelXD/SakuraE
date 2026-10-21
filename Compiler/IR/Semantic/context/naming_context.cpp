#include "naming_context.hpp"

#include "Compiler/IR/Backend/type/type.hpp"

#include <stdexcept>
#include <utility>

namespace sakuraE::IR {
    // 创建空命名空间；id 为所属模块 ID，之后分配的所有 StructDeclId 都使用它。
    NamingContext::NamingContext(fzlib::String id): moduleID(std::move(id)) {}

    // 在包含 IRStructType 完整定义的翻译单元中析构，使 unique_ptr<IRStructType> 可以正确释放。
    NamingContext::~NamingContext() = default;

    // 按名称查找语义声明（非拥有指针）。
    IRStructDecl* NamingContext::lookupStructDecl(const fzlib::String& name) const {
        const auto it = structDecls.find(name);
        return it == structDecls.end() ? nullptr : it->second.get();
    }

    // 线性扫描全部声明，按 StructDeclId 匹配。来自其他模块的 ID 在此找不到，返回 nullptr。
    const IRStructDecl* NamingContext::resolveStruct(const StructDeclId& id) const {
        for (const auto& [name, declaration] : structDecls) {
            if (declaration->getDeclId() == id) return declaration.get();
        }
        return nullptr;
    }

    // 名称 -> 声明 -> 声明 ID -> storage 类型；只查询，不创建，未经 lowering 时返回 nullptr。
    IRStructType* NamingContext::lookupStructType(const fzlib::String& name) const {
        const auto* declaration = lookupStructDecl(name);
        if (!declaration) return nullptr;
        const auto it = structTypes.find(declaration->getDeclId());
        return it == structTypes.end() ? nullptr : it->second.get();
    }

    // 本命名空间只能解析自己的声明，因此能解析即返回 this。
    const NamingContext* NamingContext::resolveNamingContext(const StructDeclId& id) const {
        return resolveStruct(id) ? this : nullptr;
    }

    // Lowering 桥接：为声明获取或创建命名 storage 类型。
    IRStructType* NamingContext::getStructTy(IRContext& context, const StructDeclId& id) const {
        const auto* declaration = resolveStruct(id);
        if (!declaration) throw std::invalid_argument("Unknown struct declaration in NamingContext");
        // storage 类型表只能服务于一个 IRContext；已绑定其他 context 时拒绝，避免类型混入别的 Program。
        if (typeContext && typeContext != &context) {
            throw std::invalid_argument("NamingContext struct types belong to another IRContext");
        }
        // 首次请求时创建 opaque（未完成布局）的 IRStructType；字段布局由 LoweringSession 在同一对象上补齐，
        // 以支持递归结构体及声明完成后的延迟补全。
        auto it = structTypes.find(id);
        if (it == structTypes.end()) {
            it = structTypes.emplace(id, std::unique_ptr<IRStructType>(new IRStructType(id.module, declaration->getName()))).first;
        }
        // 首次成功调用时绑定 context（重复赋值同一 context 无影响）。
        typeContext = &context;
        return it->second.get();
    }

    // 名称 -> 声明 ID；未知名称是调用方（编译器内部）的错误，抛 std::invalid_argument。
    StructDeclId NamingContext::lookupStructId(const fzlib::String& name) const {
        auto* declaration = lookupStructDecl(name);
        if (!declaration) throw std::invalid_argument("Unknown struct declaration");
        return declaration->getDeclId();
    }

    // 前置声明：分配新的 StructDeclId（局部序号 = 当前声明数量，因声明从不删除而保证唯一），
    // 声明处于未完成状态。
    IRStructDecl* NamingContext::declareOpaqueStruct(fzlib::String name, PositionInfo info) {
        if (lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Duplicate struct definition: '" + name + "'",
                              info);
        }
        auto decl = std::make_unique<IRStructDecl>(StructDeclId {moduleID, structDecls.size()}, moduleID, name, std::move(info));
        auto* result = decl.get();
        structDecls.emplace(name, std::move(decl));
        return result;
    }

    // 补全前置声明。用户源码重复定义或补全未声明结构体属于源码错误，报 SakuraError。
    void NamingContext::implStruct(fzlib::String name, std::vector<SemanticField> fields,
                                   std::map<fzlib::String, Constant*> defaults, PositionInfo info) {
        if (!lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Unknown struct definition: '" + name + "'",
                              info);
        }

        if (lookupStructDecl(name)->isComplete()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "You cannot redefine a struct that has already been defined.",
                              info);
        }

        // complete 会整体设置字段与默认值，并把声明标记为已完成。
        lookupStructDecl(name)->complete(std::move(fields), std::move(defaults));
    }

    // 声明并立即完成结构体，ID 分配规则与 declareOpaqueStruct 相同；不设置默认值。
    IRStructDecl* NamingContext::defineStruct(fzlib::String name, std::vector<SemanticField> fields, PositionInfo info) {
        if (lookupStructDecl(name)) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "Duplicate struct definition: '" + name + "'",
                              info);
        }

        auto decl = std::make_unique<IRStructDecl>(
            StructDeclId{moduleID, structDecls.size()}, moduleID, name, std::move(info));
        decl->complete(std::move(fields));
        auto* result = decl.get();
        structDecls.emplace(std::move(name), std::move(decl));
        return result;
    }

}
