#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/Semantic/type/type_info.hpp"
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace sakuraE::IR {
    class IRValue {
    protected:
        TypeInfo* semanticType = nullptr;   // 实际在Semantic处理阶段中使用的type
        fzlib::String name;                 // 值的别名
    public:
        explicit IRValue(TypeInfo* seType = nullptr, fzlib::String valueName = {})
        : semanticType(seType), name(std::move(valueName)) {}

        virtual ~IRValue() = default;


        void setName(const fzlib::String& n) {
            name = n;
        }

        const fzlib::String& getName() const {
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
    protected:
        CallableValue(TypeInfo* signature, fzlib::String valueName)
            : IRValue(signature, std::move(valueName)) {}

    public:
        const FunctionTypeInfo* getFunctionTypeInfo() const {
            return semanticType ? semanticType->getFunctionTypeInfo() : nullptr;
        }

        void setFunctionType(TypeInfo* signature) {
            if (signature && !signature->isFunction()) throw std::invalid_argument("Callable requires a semantic function type");
            setSemanticType(signature);
        }
    };

}
#endif /* !SAKURAE_VALUE_HPP */
