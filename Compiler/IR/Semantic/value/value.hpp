#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/Semantic/type/type_info.hpp"
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace sakuraE::IR {
    // Semantic IR 中所有值（常量、指令结果、函数等）的公共基类。
    // 只携带语义类型(TypeInfo)与名称，不保存 storage 类型(IRType)；storage 类型由 lowering
    // 按 TypeInfo 计算并记录在 Backend 侧。
    // semanticType 是非拥有引用，TypeInfo 由所属 IRContext 的 TypeInfoContext 驻留并拥有。
    class IRValue {
    protected:
        TypeInfo* semanticType = nullptr;   // 语义类型（非拥有），Semantic 阶段的类型检查、可赋值性判断均以此为准
        fzlib::String name;                 // 值的名称/别名，用于 IR 打印与调试
    public:
        // seType 可以为空（例如稍后通过 setSemanticType 补齐的值）。
        explicit IRValue(TypeInfo* seType = nullptr, fzlib::String valueName = {})
        : semanticType(seType), name(std::move(valueName)) {}

        virtual ~IRValue() = default;


        // 名称访问器。
        void setName(const fzlib::String& n) {
            name = n;
        }

        const fzlib::String& getName() const {
            return name;
        }

        // 语义类型访问器；setSemanticType 不做校验，调用方负责保证类型来自同一 IRContext。
        TypeInfo* getSemanticType() const {
            return semanticType;
        }

        void setSemanticType(TypeInfo* newTypeInfo) {
            semanticType = newTypeInfo;
        }

    };

    // 可调用的值（如 Function），其语义类型必须是函数类型(FunctionType)，用于实现函数调用等功能。
    // 构造函数为 protected，只能由具体的可调用子类使用。
    class CallableValue: public IRValue {
    protected:
        CallableValue(TypeInfo* signature, fzlib::String valueName)
            : IRValue(signature, std::move(valueName)) {}

    public:
        // 返回函数签名；尚未设置签名（semanticType 为空）时返回 nullptr。
        const FunctionTypeInfo* getFunctionTypeInfo() const {
            return semanticType ? semanticType->getFunctionTypeInfo() : nullptr;
        }

        // 设置函数签名。允许传入 nullptr（清空签名），但非空时必须是函数语义类型，
        // 否则属于编译器内部错误，抛 std::invalid_argument。
        void setFunctionType(TypeInfo* signature) {
            if (signature && !signature->isFunction()) throw std::invalid_argument("Callable requires a semantic function type");
            setSemanticType(signature);
        }
    };

}
#endif /* !SAKURAE_VALUE_HPP */
