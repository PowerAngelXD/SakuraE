#ifndef SAKURAE_STRUCT_HPP
#define SAKURAE_STRUCT_HPP

#include "Compiler/IR/Semantic/type/type_info.hpp"

#include <Compiler/Error/error.hpp>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <set>

namespace sakuraE::IR {
    class NamingContext;
    // IRStructDecl：语义层的结构体声明。
    // 以 StructDeclId（模块名 + 模块内局部序号 localId 的稳定标识，定义见 type_info.hpp）作为身份，
    // 语义类型(TypeInfo)中的结构体类型只持有 StructDeclId，通过 StructDeclRegistry 解析回本声明。
    // 声明分两阶段：先以"不完整"状态注册（允许递归/前向引用），再由 complete() 一次性填入字段。
    // defaultValues 中的 Constant* 是非拥有引用，所有权归所属 Module/IRContext。
    class IRStructDecl {
        StructDeclId declaration;
        fzlib::String parentModID;
        fzlib::String name;
        std::vector<SemanticField> fields;
        bool completeState = false;
        std::map<fzlib::String, Constant*> defaultValues;
        PositionInfo createInfo;

    public:
        // 构造一个尚未完成（无字段）的结构体声明；modID 为所属模块 ID，info 为声明位置。
        IRStructDecl(StructDeclId id, fzlib::String modID, fzlib::String n, PositionInfo info):
            declaration(id), parentModID(std::move(modID)), name(std::move(n)), createInfo(std::move(info)) {}

        // 返回稳定的结构体声明标识。
        StructDeclId getDeclId() const { return declaration; }

        // 返回结构体的源码名称（不含模块限定）。
        const fzlib::String& getName() const { return name; }

        // 按字段名线性查找字段；找不到返回 nullptr。字段数通常很少，线性查找足够。
        const SemanticField* find(const fzlib::String& target) const {
            for (const auto& field : fields) if (field.name == target) return &field;
            return nullptr;
        }

        // 字段访问：at 按下标访问（越界抛 std::out_of_range），getFields 返回全部字段，
        // isComplete 表示是否已经调用过 complete()。
        const SemanticField& at(std::size_t index) const { return fields.at(index); }
        const std::vector<SemanticField>& getFields() const { return fields; }
        bool isComplete() const { return completeState; }

        // 是否为指定字段登记了默认值。
        bool hasDefaultValue(const fzlib::String& fieldName) const {
            return defaultValues.contains(fieldName);
        }

        // 获取字段默认值常量（非拥有引用）；未登记时返回 nullptr。
        Constant* getDefaultValue(const fzlib::String& fieldName) const {
            const auto it = defaultValues.find(fieldName);
            return it == defaultValues.end() ? nullptr : it->second;
        }

        // 设置或覆盖字段默认值；传入 nullptr 表示清除该字段的默认值。
        void setDefaultValue(fzlib::String fieldName, Constant* value) {
            if (!value) {
                defaultValues.erase(fieldName);
                return;
            }
            defaultValues.insert_or_assign(std::move(fieldName), value);
        }

        // 完成结构体声明：填入字段列表与默认值。每个声明只能完成一次。
        // 校验每个字段都具有语义类型且字段名不重复，否则报告 IR 生成阶段错误。
        // 注意：defaults 会整体替换此前通过 setDefaultValue 设置的默认值。
        void complete(std::vector<SemanticField> fs,
                      std::map<fzlib::String, Constant*> defaults = {}) {
            // 重复完成属于编译器内部逻辑错误，而非用户源码错误，因此抛 std::logic_error。
            if (completeState) throw std::logic_error("Cannot complete an already completed semantic struct declaration");
            std::set<fzlib::String> names;
            // 字段类型为空或 insert 失败（名称已存在）均视为非法字段。
            for (const auto& field : fs) {
                if (!field.type || !names.insert(field.name).second) {
                    throw SakuraError(OccurredTerm::IR_GENERATING, "Invalid or duplicate semantic struct field.", field.info);
                }
            }
            fields = std::move(fs);
            completeState = true;
            defaultValues = std::move(defaults);
        }
    };

    // StructDeclRegistry：由 StructDeclId 解析结构体声明及其 NamingContext 的抽象接口。
    // 语义类型只保存 StructDeclId，需要字段信息或命名信息（lowering 生成结构体名）时通过此接口查询；
    // 返回的指针均为非拥有引用，找不到时返回 nullptr。
    class StructDeclRegistry {
    public:
        virtual ~StructDeclRegistry() = default;
        // 根据声明标识查找结构体声明。
        virtual const IRStructDecl* resolveStruct(const StructDeclId& id) const = 0;
        // 根据声明标识查找该声明所属模块的命名上下文（用于 lowering 时生成唯一名称）。
        virtual const NamingContext* resolveNamingContext(const StructDeclId& id) const = 0;
    };

}

#endif
