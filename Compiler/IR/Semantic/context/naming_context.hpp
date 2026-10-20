#ifndef SAKURAE_SEMANTIC_NAMING_CONTEXT_HPP
#define SAKURAE_SEMANTIC_NAMING_CONTEXT_HPP

#include "Compiler/IR/Semantic/model/struct.hpp"

#include <map>
#include <memory>

namespace sakuraE::IR {
    class IRContext;
    class IRStructType;

    class NamingContext : public StructDeclRegistry {
        fzlib::String moduleID;
        std::map<fzlib::String, std::unique_ptr<IRStructDecl>> structDecls;
        mutable std::map<StructDeclId, std::unique_ptr<IRStructType>> structTypes;
        mutable IRContext* typeContext = nullptr;

    public:
        explicit NamingContext(fzlib::String id);
        const fzlib::String& moduleId() const { return moduleID; }
        ~NamingContext() override;

        NamingContext(const NamingContext&) = delete;
        NamingContext& operator=(const NamingContext&) = delete;

        IRStructDecl* lookupStructDecl(const fzlib::String& name) const;
        const IRStructDecl* resolveStruct(const StructDeclId& id) const override;
        const NamingContext* resolveNamingContext(const StructDeclId& id) const override;
        const std::map<fzlib::String, std::unique_ptr<IRStructDecl>>& declarations() const {
            return structDecls;
        }
        StructDeclId lookupStructId(const fzlib::String& name) const;
        IRStructDecl* declareOpaqueStruct(fzlib::String name, PositionInfo info);
        void implStruct(fzlib::String name, std::vector<SemanticField> fields,
                        std::map<fzlib::String, Constant*> defaults, PositionInfo info);
        IRStructDecl* defineStruct(fzlib::String name, std::vector<SemanticField> fields, PositionInfo info);

        // Lowering bridge: storage types remain owned by this module namespace.
        IRStructType* getStructTy(IRContext& context, const StructDeclId& id) const;
        IRStructType* lookupStructType(const fzlib::String& name) const;
    };

}

#endif
