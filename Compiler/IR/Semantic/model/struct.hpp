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

namespace sakuraE::IR {
    class IRStructDecl {
        StructDeclId declaration;
        fzlib::String parentModID;
        fzlib::String name;
        std::vector<SemanticField> fields;
        bool completeState = false;
        std::map<fzlib::String, Constant*> defaultValues;
        PositionInfo createInfo;

    public:
        IRStructDecl(StructDeclId id, fzlib::String modID, fzlib::String n, PositionInfo info):
            declaration(id), parentModID(std::move(modID)), name(std::move(n)), createInfo(std::move(info)) {}

        StructDeclId getDeclId() const { return declaration; }

        const fzlib::String& getName() const { return name; }

        const SemanticField* find(const fzlib::String& target) const {
            for (const auto& field : fields) if (field.name == target) return &field;
            return nullptr;
        }

        const SemanticField& at(std::size_t index) const { return fields.at(index); }
        const std::vector<SemanticField>& getFields() const { return fields; }
        bool isComplete() const { return completeState; }

        bool hasDefaultValue(const fzlib::String& fieldName) const {
            return defaultValues.contains(fieldName);
        }

        Constant* getDefaultValue(const fzlib::String& fieldName) const {
            const auto it = defaultValues.find(fieldName);
            return it == defaultValues.end() ? nullptr : it->second;
        }

        void setDefaultValue(fzlib::String fieldName, Constant* value) {
            if (!value) {
                defaultValues.erase(fieldName);
                return;
            }
            defaultValues.insert_or_assign(std::move(fieldName), value);
        }

        void complete(std::vector<SemanticField> fs,
                      std::map<fzlib::String, Constant*> defaults = {}) {
            fields = std::move(fs);
            completeState = true;
            defaultValues = std::move(defaults);
        }
    };

    class StructDeclRegistry {
    public:
        virtual ~StructDeclRegistry() = default;
        virtual const IRStructDecl* resolveStruct(const StructDeclId& id) const = 0;
    };

}

#endif
