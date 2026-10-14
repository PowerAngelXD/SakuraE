#ifndef SAKURAE_LOWERING_CONTEXT_HPP
#define SAKURAE_LOWERING_CONTEXT_HPP

#include <Compiler/IR/Semantic/type/type_info.hpp>
#include <Compiler/IR/Semantic/model/struct.hpp>
#include <Compiler/IR/Backend/type/type.hpp>
#include <unordered_map>
#include <stdexcept>
#include <set>

namespace sakuraE::IR {
    class LoweringSession {
        IRContext& irContext;
        const StructDeclRegistry* structResolver;
        std::unordered_map<const TypeInfo*, IRType*> typeMap;
        std::map<StructDeclId, IRStructType*> structMap;
        std::set<StructDeclId> fillingStructs;

        IRType* loweringBasicType(const TypeInfo* tyInfo) const;
        void validateStructLayout(const StructDeclId& id) const;
    public:
        explicit LoweringSession(IRContext& ctx, const StructDeclRegistry* resolver = nullptr):
            irContext(ctx), structResolver(resolver) {}

        IRContext& context() const { return irContext; }

        IRType* lowerType(const TypeInfo* tyInfo);
    };
}

#endif
