#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/type/type_info.hpp"
#include "Compiler/Frontend/lexer.h"
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace sakuraE::IR {
    struct FuncSemanticSignature {
        std::vector<TypeInfo*> paramTypes;
        TypeInfo* returnType = nullptr;
    };

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

    // 可调用的值，用于实现函数调用等功能
    class CallableValue: public IRValue {
        std::shared_ptr<FuncSemanticSignature> funcSemanticSignature;

    protected:
        CallableValue(IRType* storageType, fzlib::String valueName,
                      std::shared_ptr<FuncSemanticSignature> signature = nullptr)
            : IRValue(storageType, std::move(valueName)),
              funcSemanticSignature(std::move(signature)) {}

    public:
        std::shared_ptr<FuncSemanticSignature> getFuncSemanticSignature() const {
            return funcSemanticSignature;
        }

        void setFuncSemanticSignature(std::shared_ptr<FuncSemanticSignature> signature) {
            funcSemanticSignature = std::move(signature);
        }
    };

}
#endif /* !SAKURAE_VALUE_HPP */
