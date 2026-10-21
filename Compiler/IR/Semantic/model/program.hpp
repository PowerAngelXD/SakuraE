#ifndef SAKURAE_PROGRAM_HPP
#define SAKURAE_PROGRAM_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Lowering/session.hpp"
#include "module.hpp"
#include <cstddef>
#include <memory>
#include <functional>
#include <set>

namespace sakuraE::IR {
    // Program：整个编译程序的语义 IR 根对象，同时作为跨模块的结构体声明注册表(StructDeclRegistry)。
    // 持有 IRContext（语义类型池等，所有 TypeInfo 都必须属于该上下文）、全部 Module（拥有，析构时 delete），
    // 以及一个与 Program 同寿命的持久 LoweringSession（缓存 TypeInfo->IRType 等 lowering 结果，供 codegen 复用）。
    // 构造时会先创建 "__runtime" 模块并声明全部运行时函数；之后创建的模块都会自动 use 该模块。
    class Program : public StructDeclRegistry {
        // IR 上下文：拥有语义类型池；必须最先构造、最后析构（成员声明顺序保证），因为模块与 Session 都引用它。
        IRContext context;
        fzlib::String ID;

        std::vector<Module*> moduleList;
        // 当前模块在 moduleList 中的索引（游标）；-1 表示尚无模块。
        int cursor = -1;
        // 持久 lowering 会话；需在析构函数中先于模块显式释放。
        std::unique_ptr<LoweringSession> loweringSession;
    public:
        // 构造 Program：创建运行时模块并声明运行时 API（内存分配、字符串、I/O、
        // 运行时类型元数据(RuntimeTypeInfo)构造、数组越界检查、GC root/作用域管理等）。
        // targetSize 为宿主指针位宽，决定 size 参数使用 UInt64 还是 UInt32。
        Program(fzlib::String id): ID(id) {
            PositionInfo info = {0, 0, "System"};
            size_t targetSize = sizeof(void*) * 8;
            buildModule("__runtime", info, true);
            auto runtimeMod = curMod();
            // LoweringSession 持有 context 引用并以本 Program 作为结构体声明注册表。
            loweringSession = std::make_unique<LoweringSession>(context, this);
            auto& types = context.typeInfoPool();
            // 声明一个运行时函数：IR 名称为 "原始名_参数类型..."，以区分重载；原始名称保留用于链接与查找。
            const auto declareRuntime = [&](fzlib::String name, TypeInfo* resultType,
                                            std::vector<std::pair<fzlib::String, TypeInfo*>> parameters,
                                            PositionInfo declarationInfo) {
                auto irName = name;
                for (const auto& parameter : parameters) {
                    // IR 名称依据语义类型生成；实际 runtime linkage 使用原始名称。
                    irName += "_" + parameter.second->toString();
                }
                runtimeMod->declareRuntimeFunction(irName, name, resultType, std::move(parameters), declarationInfo);
            };

            declareRuntime(
                "__alloc",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                { {"size", types.makeBasicTypeID(targetSize == 64 ? TypeID::UInt64 : TypeID::UInt32)} },
                info
            );

            declareRuntime(
                "__free",
                types.makeBasicTypeID(TypeID::Void),
                { {"ptr", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))} },
                info
            );

            declareRuntime(
                "create_string",
                types.makeBasicTypeID(TypeID::String),
                { {"literal", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Char))} },
                info
            );

            declareRuntime(
                "free_string",
                types.makeBasicTypeID(TypeID::Void),
                { {"str", types.makeBasicTypeID(TypeID::String)} },
                info
            );

            declareRuntime(
                "concat_string",
                types.makeBasicTypeID(TypeID::String),
                {
                    {"s1", types.makeBasicTypeID(TypeID::String)},
                    {"s2", types.makeBasicTypeID(TypeID::String)}
                },
                info
            );

            declareRuntime(
                "print",
                types.makeBasicTypeID(TypeID::Void),
                { {"value", types.makeBasicTypeID(TypeID::String)} },
                info
            );

            declareRuntime(
                "println",
                types.makeBasicTypeID(TypeID::Void),
                { {"value", types.makeBasicTypeID(TypeID::String)} },
                info
            );

            declareRuntime(
                "input",
                types.makeBasicTypeID(TypeID::String),
                {},
                info
            );

            declareRuntime(
                "inputc",
                types.makeBasicTypeID(TypeID::Char),
                {},
                info
            );

            // 运行时值分配、数组越界检查与运行时类型元数据(RuntimeTypeInfo)构造函数。
            declareRuntime(
                "__runtime_alloc_value",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                {},
                info
            );

            declareRuntime(
                "__runtime_check_array_bounds",
                types.makeBasicTypeID(TypeID::Void),
                {
                    {"index", types.makeBasicTypeID(TypeID::Int64)},
                    {"length", types.makeBasicTypeID(TypeID::UInt64)}
                },
                info
            );

            declareRuntime(
                "__runtime_array_bounds_error",
                types.makeBasicTypeID(TypeID::Void),
                {
                    {"index", types.makeBasicTypeID(TypeID::Int64)},
                    {"length", types.makeBasicTypeID(TypeID::UInt64)}
                },
                info
            );

            declareRuntime(
                "__runtime_type_info_basic",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                { {"kind", types.makeBasicTypeID(TypeID::Char)} },
                info
            );

            declareRuntime(
                "__runtime_type_info_pointer",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                { {"element", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))} },
                info
            );

            declareRuntime(
                "__runtime_type_info_reference",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                { {"element", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))} },
                info
            );

            declareRuntime(
                "__runtime_type_info_array",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                {
                    {"element", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))},
                    {"count", types.makeBasicTypeID(TypeID::UInt64)}
                },
                info
            );

            declareRuntime(
                "__runtime_type_info_struct",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                {
                    {"name", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Char))},
                    {"layout", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))}
                },
                info
            );

            // GC 方法：GC 托管分配、GC root 注册（地址/装箱值/值槽）、作用域进入/离开、弹出、回收及结构体类型元数据。
            declareRuntime(
                "__gc_alloc",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                {
                    { "size", types.makeBasicTypeID(targetSize == 64 ? TypeID::UInt64 : TypeID::UInt32) },
                    { "ty", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)) },
                    { "member_count", types.makeBasicTypeID(TypeID::UInt64) }
                },
                info
            );

            declareRuntime(
                "__gc_register",
                types.makeBasicTypeID(TypeID::Void),
                { {"addr", types.makePointerTypeID(types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)))} },
                info
            );

            declareRuntime(
                "__gc_register_value",
                types.makeBasicTypeID(TypeID::Void),
                { {"value", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void))} },
                info
            );

            declareRuntime(
                "__gc_register_value_slot",
                types.makeBasicTypeID(TypeID::Void),
                { {"slot", types.makePointerTypeID(types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)))} },
                info
            );

            declareRuntime(
                "__gc_enter_scope",
                types.makeBasicTypeID(TypeID::Void),
                {},
                info
            );

            declareRuntime(
                "__gc_leave_scope",
                types.makeBasicTypeID(TypeID::Void),
                {},
                info
            );

            declareRuntime(
                "__gc_pop",
                types.makeBasicTypeID(TypeID::Void),
                { {"times", types.makeBasicTypeID(TypeID::UInt32)} },
                info
            );

            declareRuntime(
                "__gc_collect",
                types.makeBasicTypeID(TypeID::Void),
                {},
                info
            );

            declareRuntime(
                "__gc_get_struct_type",
                types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)),
                {
                    { "name", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Char)) },
                    { "ptr_count", types.makeBasicTypeID(TypeID::UInt32) },
                    { "ptr_offsets", types.makePointerTypeID(types.makeBasicTypeID(TypeID::Void)) }
                },
                info
            );

        }

        // 销毁顺序：先释放 LoweringSession（它可能引用模块与类型），再释放全部模块；IRContext 作为成员最后析构。
        ~Program() {
            loweringSession.reset();
            for (auto mod: moduleList) {
                delete mod;
            }
        }

        // 创建新模块并把游标移到它。模块 ID 必须唯一（以命名上下文的模块 ID 判断）。
        // 非运行时模块会自动 use 运行时模块 mod(0)，因此运行时模块必须第一个创建。
        Program& buildModule(fzlib::String id, PositionInfo info, bool isRuntime = false) {
            for (const auto* existing : moduleList) {
                if (existing->structRegistry().moduleId() == id) {
                    throw SakuraError(OccurredTerm::IR_GENERATING, "Duplicate module identifier.", info);
                }
            }
            Module* module = new Module(id, info);
            module->setSourceProgram(this);
            // 使用运行时模块
            if (!isRuntime)
                module->use(mod(0));
            moduleList.emplace_back(module);
            cursor ++;

            return *this;
        }

        // 返回游标指向的当前模块（不做越界检查）。
        Module* curMod () {
            return moduleList[cursor];
        }

        // 按下标取模块（不做越界检查）；mod(0) 为运行时模块。
        Module* mod(std::size_t index) {
            return moduleList[index];
        }

        void reset() {
            // 重置模块游标及各模块/函数游标，供 codegen 重新遍历。
            // 不重建 LoweringSession：codegen 重置遍历游标时仍需复用函数签名 lowering 所属的持久 Session。
            cursor = 0;
            for (auto mod: moduleList) {
                mod->reset();
            }
        }

        // 返回程序 ID。
        fzlib::String getID() {
            return ID;
        }

        // 返回 IR 上下文（可变/只读两个重载）。
        IRContext& getContext() {
            return context;
        }

        const IRContext& getContext() const {
            return context;
        }

        // 语义验证：递归检查语义类型及其所有组成部分（结构体字段、数组元素、指针/引用目标、
        // 函数返回与参数类型）都非空、属于本 Program 的 IRContext，且结构体声明可解析。
        // 不合法时报告 IR 生成错误，info 为报告位置。
        void validateSemanticType(const TypeInfo* type, PositionInfo info) const {
            std::set<const TypeInfo*> visited;
            std::function<void(const TypeInfo*)> validate = [&](const TypeInfo* current) {
                // 空类型或来自其他上下文的类型都不可用于本 Program。
                if (!current || &current->getContext() != &context) {
                    throw SakuraError(OccurredTerm::IR_GENERATING, "Semantic type does not belong to this Program.", info);
                }
                // 已访问则跳过：避免递归结构体（字段经指针引用自身）导致无限递归。
                if (!visited.insert(current).second) return;
                if (current->isStruct()) {
                    const auto* declaration = resolveStruct(*current->getStructDeclId());
                    if (!declaration) throw SakuraError(OccurredTerm::IR_GENERATING, "Unknown semantic struct declaration.", info);
                    for (const auto& field : declaration->getFields()) validate(field.type);
                }
                else if (current->isArray()) validate(current->getElementType());
                else if (current->isPointer() || current->isRef()) validate(current->getPointeeType());
                else if (const auto* signature = current->getFunctionTypeInfo()) {
                    validate(signature->getReturnType());
                    for (const auto* argument : signature->getArgTypes()) validate(argument);
                }
            };
            validate(type);
        }

        // 返回持久 lowering 会话。const 方法也返回可变引用：Session 内部缓存不属于 Program 的逻辑状态。
        LoweringSession& lowering() const {
            return *loweringSession;
        }

        // 在全部模块中按 StructDeclId 解析结构体声明；找不到返回 nullptr。
        const IRStructDecl* resolveStruct(const StructDeclId& id) const override {
            for (const auto* module: moduleList) {
                if (const auto* declaration = module->resolveStruct(id)) {
                    return declaration;
                }
            }
            return nullptr;
        }

        // 返回声明了该结构体的模块的命名上下文；找不到返回 nullptr。
        const NamingContext* resolveNamingContext(const StructDeclId& id) const override {
            for (const auto* module : moduleList) {
                if (module->resolveStruct(id)) return &module->structRegistry();
            }
            return nullptr;
        }

        // 返回全部模块（只读）。
        const std::vector<Module*>& getMods() const {
            return moduleList;
        }

        // 生成调试用文本表示："ID {模块...}"。
        fzlib::String toString() {
            fzlib::String result = ID + " {";
            for (auto mod: moduleList) {
                result += mod->toString();
            }
            result += "}";
            return result;
        }
    };
}

#endif /* !SAKURAE_PROGRAM_HPP */
