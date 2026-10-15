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
    class Program : public StructDeclRegistry {
        IRContext context;
        fzlib::String ID;

        std::vector<Module*> moduleList;
        // moduleList 当前索引的最大值
        int cursor = -1;
        std::unique_ptr<LoweringSession> loweringSession;
    public:
        Program(fzlib::String id): ID(id) {
            PositionInfo info = {0, 0, "System"};
            size_t targetSize = sizeof(void*) * 8;
            buildModule("__runtime", info, true);
            auto runtimeMod = curMod();
            loweringSession = std::make_unique<LoweringSession>(context, this);
            auto& types = context.typeInfoPool();
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

            // GC 方法
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

        ~Program() {
            loweringSession.reset();
            for (auto mod: moduleList) {
                delete mod;
            }
        }

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

        Module* curMod () {
            return moduleList[cursor];
        }

        Module* mod(std::size_t index) {
            return moduleList[index];
        }

        void reset() {
            // Codegen 重置遍历游标时仍需复用函数签名 lowering 所属的持久 Session。
            cursor = 0;
            for (auto mod: moduleList) {
                mod->reset();
            }
        }

        fzlib::String getID() {
            return ID;
        }

        IRContext& getContext() {
            return context;
        }

        void validateSemanticType(const TypeInfo* type, PositionInfo info) const {
            std::set<const TypeInfo*> visited;
            std::function<void(const TypeInfo*)> validate = [&](const TypeInfo* current) {
                if (!current || &current->getContext() != &context) {
                    throw SakuraError(OccurredTerm::IR_GENERATING, "Semantic type does not belong to this Program.", info);
                }
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

        LoweringSession& lowering() const {
            return *loweringSession;
        }

        const IRStructDecl* resolveStruct(const StructDeclId& id) const override {
            for (const auto* module: moduleList) {
                if (const auto* declaration = module->resolveStruct(id)) {
                    return declaration;
                }
            }
            return nullptr;
        }

        const NamingContext* resolveNamingContext(const StructDeclId& id) const override {
            for (const auto* module : moduleList) {
                if (module->resolveStruct(id)) return &module->structRegistry();
            }
            return nullptr;
        }

        std::vector<Module*> getMods() {
            return moduleList;
        }

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
