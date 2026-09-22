#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/Backend/type/type.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
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
        TypeInfo* semanticType = nullptr;   // 实际在Semantic处理阶段中使用的type
        IRType* storageType = nullptr;      // lowering 目标，仅作储存用
        fzlib::String name;                 // 值的别名
    public:
        explicit IRValue(TypeInfo* seType = nullptr, fzlib::String valueName = {})
        : semanticType(seType), storageType(nullptr), name(std::move(valueName)) {}

        explicit IRValue(IRType* storage, fzlib::String valueName = {})
        : semanticType(nullptr), storageType(storage), name(std::move(valueName)) {}

        IRValue(IRType* storage, TypeInfo* seType, fzlib::String valueName = {})
        : semanticType(seType), storageType(storage), name(std::move(valueName)) {}

        virtual ~IRValue() = default;


        void setName(const fzlib::String& n) {
            name = n;
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

        IRType* getStorageType() const {
            return storageType;
        }

        IRType* getType() const {
            return storageType;
        }

        void setStorageType(IRType* newType) {
            storageType = newType;
        }

    };

    // 可调用的值，用于实现函数调用等功能
    class CallableValue: public IRValue {
        std::shared_ptr<FuncSemanticSignature> funcSemanticSignature;

    protected:
        CallableValue(IRType* storageType, fzlib::String valueName,
                      std::shared_ptr<FuncSemanticSignature> signature = nullptr)
            : IRValue(storageType, nullptr, std::move(valueName)),
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
