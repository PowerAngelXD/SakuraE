#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/Frontend/lexer.h"
#include <string>
#include <utility>

namespace sakuraE::IR {
    class IRValue {
    protected:
        IRType* type = nullptr;
        TypeInfo* semanticType = nullptr;
        fzlib::String name;
    public:
        IRValue(IRType* storageType, TypeInfo* semanticType,
            fzlib::String valueName)
        : type(storageType),
          semanticType(semanticType),
          name(std::move(valueName)) {}

        explicit IRValue(IRType* storageType, TypeInfo* semanticType)
            : IRValue(storageType, semanticType, {}) {}

        explicit IRValue(IRType* storageType, fzlib::String valueName)
            : IRValue(storageType, nullptr, std::move(valueName)) {}

        explicit IRValue(IRType* storageType)
            : IRValue(storageType, nullptr, {}) {}

        virtual ~IRValue() = default;

        IRType* getType() const { return type; }

        void setName(const fzlib::String& n) {
            name = n;
        }

        void setType(IRType* t) {
            type = t;
        }

        const fzlib::String& getName() {
            return name;
        }

        TypeInfo* getSemanticType() const {
            return semanticType;
        }

        void setSemanticType(TypeInfo* newTypeInfo) {
            semanticType = newTypeInfo;
        }
    };

}
#endif /* !SAKURAE_VALUE_HPP */
