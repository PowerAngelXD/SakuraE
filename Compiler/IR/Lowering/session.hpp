#ifndef SAKURAE_LOWERING_CONTEXT_HPP
#define SAKURAE_LOWERING_CONTEXT_HPP

#include <Compiler/IR/Semantic/type/type_info.hpp>
#include <Compiler/IR/Semantic/model/struct.hpp>
#include <Compiler/IR/Backend/type/type.hpp>
#include <Compiler/IR/Backend/model/module.hpp>
#include <unordered_map>
#include <stdexcept>
#include <set>

namespace sakuraE::IR {
    class LoweringSession {
        IRContext& irContext;
        const StructDeclRegistry* structResolver;
        // 只缓存类型图已稳定（不含未完成结构体声明）的转换结果
        std::unordered_map<const TypeInfo*, IRType*> typeMap;
        std::map<StructDeclId, IRStructType*> structMap;
        std::set<StructDeclId> fillingStructs;
        std::unordered_map<const Function*, BackendFunction*> functionMap;
        std::unordered_map<const TypeInfo*, std::unique_ptr<RuntimeTypeDescription>> runtimeTypeMap;
        std::unordered_map<const Module*, std::unique_ptr<BackendModule>> backendModules;
        const RuntimeTypeDescription* lowerRuntimeType(const TypeInfo* type);
        ConstantKind lowerConstantKind(const Constant& constant) const;

        void validateModule(const Module& module) const;
        void lowerFunctionContents(BackendFunction& function);
        void refreshModule(const Module& module);
        void populateModule(BackendModule& module);

        // 单次 lowerType 调用中产生、尚未确认可缓存的结果
        struct PendingLowering {
            std::unordered_map<const TypeInfo*, IRType*> results;
            bool opaqueStruct = false;
        };

        IRType* loweringBasicType(const TypeInfo* tyInfo) const;
        IRType* lowerTypeSafer(const TypeInfo* tyInfo, PendingLowering& pending);
        void validateStruct(const StructDeclId& id) const;
    public:
        explicit LoweringSession(IRContext& ctx, const StructDeclRegistry* resolver = nullptr):
            irContext(ctx), structResolver(resolver) {}

        IRContext& context() const { return irContext; }

        IRType* lowerType(const TypeInfo* tyInfo);
        BackendFunction* lowerFunction(const Function& function);
        BackendModule* lowerModule(const Module& module);
    };
}

#endif
