#ifndef SAKURAE_STRUCT_HPP
#define SAKURAE_STRUCT_HPP

#include "Compiler/IR/type/type.hpp"

#include <Compiler/Error/error.hpp>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace sakuraE::IR {
    class IRStructDecl {
        fzlib::String parentModID;
        fzlib::String name;
        std::unique_ptr<IRStructType> type;
        std::map<fzlib::String, Constant*> defaultValues;
        PositionInfo createInfo;

    public:
        IRStructDecl(fzlib::String modID, fzlib::String n, std::unique_ptr<IRStructType> t, PositionInfo info):
            parentModID(modID), name(std::move(n)), type(std::move(t)), createInfo(std::move(info)) {}

        const fzlib::String& getName() const { return name; }
        IRStructType* getType() const { return type.get(); }

        std::optional<IRStructType::FieldInfo> find(const fzlib::String& target) const {
            return type->findMember(target);
        }

        const IRStructType::FieldInfo& at(std::size_t index) const {
            return type->getFields().at(index);
        }

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

        void complete(std::vector<IRStructType::FieldInfo> fs,
                      std::map<fzlib::String, Constant*> defaults = {}) {
            type->complete(std::move(fs));
            defaultValues = std::move(defaults);
        }
    };

}

#endif
