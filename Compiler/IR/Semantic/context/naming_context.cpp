#include "naming_context.hpp"

#include "Compiler/IR/Backend/type/type.hpp"

#include <stdexcept>
#include <utility>

namespace sakuraE::IR {
    NamingContext::NamingContext(fzlib::String id): moduleID(std::move(id)) {}

    NamingContext::~NamingContext() = default;

    IRStructDecl* NamingContext::lookupStructDecl(const fzlib::String& name) const {
        const auto it = structDecls.find(name);
        return it == structDecls.end() ? nullptr : it->second.get();
    }

    const IRStructDecl* NamingContext::resolveStruct(const StructDeclId& id) const {
        for (const auto& [name, declaration] : structDecls) {
            if (declaration->getDeclId() == id) return declaration.get();
        }
        return nullptr;
    }

    IRStructType* NamingContext::lookupStructType(const fzlib::String& name) const {
        const auto* declaration = lookupStructDecl(name);
        if (!declaration) return nullptr;
        const auto it = structTypes.find(declaration->getDeclId());
        return it == structTypes.end() ? nullptr : it->second.get();
    }

    const NamingContext* NamingContext::resolveNamingContext(const StructDeclId& id) const {
        return resolveStruct(id) ? this : nullptr;
    }

    IRStructType* NamingContext::getStructTy(IRContext& context, const StructDeclId& id) const {
        const auto* declaration = resolveStruct(id);
        if (!declaration) throw std::invalid_argument("Unknown struct declaration in NamingContext");
        if (typeContext && typeContext != &context) {
            throw std::invalid_argument("NamingContext struct types belong to another IRContext");
        }
        auto it = structTypes.find(id);
        if (it == structTypes.end()) {
            it = structTypes.emplace(id, std::unique_ptr<IRStructType>(new IRStructType(id.module, declaration->getName()))).first;
        }
        typeContext = &context;
        return it->second.get();
    }

    StructDeclId NamingContext::lookupStructId(const fzlib::String& name) const {
        auto* declaration = lookupStructDecl(name);
        if (!declaration) throw std::invalid_argument("Unknown struct declaration");
        return declaration->getDeclId();
    }

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

        lookupStructDecl(name)->complete(std::move(fields), std::move(defaults));
    }

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
