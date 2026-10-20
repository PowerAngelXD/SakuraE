#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include <stack>
#include <utility>

#include "Compiler/Error/error.hpp"
#include "block.hpp"
#include "scope.hpp"

namespace sakuraE::IR {
    using FormalParamsDefine = std::vector<std::pair<fzlib::String, TypeInfo*>>;
    class Module;

    // SakuraE 函数
    class Function: public CallableValue {
        fzlib::String rawName;
        std::vector<fzlib::String> formalParamNames;
        Scope<IRValue*> funcScope;

        PositionInfo createInfo;

        std::vector<Block*> blocks;
        // 当前代码块索引的最大值
        long cursor = -1;

        // break 和 continue 管理器
        struct LoopInfo {
            IRValue* continueTarget;
            IRValue* breakTarget;
        };

        // 返回值检查器
        bool hasReturn = false;

        std::stack<LoopInfo> loopStack;

        Module* parent = nullptr;
        TypeInfo* controlType() const {
            if (!getReturnType()) throw std::logic_error("Function must have a semantic signature before building blocks");
            return getReturnType()->getTypeContext().makeBasicTypeID(TypeID::Void);
        }
    public:
        Function(fzlib::String n, TypeInfo* retType, PositionInfo info):
            Function(std::move(n), retType, {}, std::move(info)) {}

        // 仅用于预声明，定义时通过 setFuncDefineInfo 建立语义签名。
        Function(fzlib::String n, PositionInfo info):
            CallableValue(nullptr, std::move(n)), funcScope(info), createInfo(info) {}

        Function(fzlib::String n, TypeInfo* retType, FormalParamsDefine params, PositionInfo info):
            CallableValue(nullptr, std::move(n)), funcScope(info), createInfo(info) {
            setFuncDefineInfo(std::move(params), retType);
        }

        Function(fzlib::String n, fzlib::String raw, TypeInfo* retType,
                 FormalParamsDefine params, PositionInfo info):
            Function(std::move(n), retType, std::move(params), std::move(info)) {
            rawName = std::move(raw);
        }

        ~Function() {
            for (auto blk: blocks) {
                delete blk;
            }
        }

        void enterLoop(IRValue* c, IRValue* b) {
            loopStack.push({c, b});
        }

        void leaveLoop() {
            if (!loopStack.empty()) loopStack.pop();
        }

        bool isLookEmpty() { return loopStack.empty(); }

        LoopInfo& getLoopTop() { return loopStack.top(); }

        void setParent(Module* mod) {
            parent = mod;
        }

        const fzlib::String& getRawName() const { return rawName; }

        void setFuncDefineInfo(FormalParamsDefine params, TypeInfo* retType) {
            if (!retType) throw std::invalid_argument("Function requires a semantic return type");
            std::vector<TypeInfo*> paramTypes;
            std::vector<fzlib::String> paramNames;
            for (const auto& param : params) {
                paramNames.push_back(param.first);
                paramTypes.push_back(param.second);
            }
            // 类型工厂验证空组件及 context 归属；验证成功后再更新定义。
            auto* signature = retType->getTypeContext().makeFunctionTypeID(
                retType, std::move(paramTypes));
            formalParamNames = std::move(paramNames);
            CallableValue::setFunctionType(signature);
        }

        void setFunctionType(TypeInfo* signature) {
            if (!signature || !signature->isFunction()
                || signature->getFunctionTypeInfo()->getArgCount() != formalParamNames.size()) {
                throw std::invalid_argument("Function signature must match its named parameters");
            }
            CallableValue::setFunctionType(signature);
        }

        void setReturnChecker(bool state) {
            hasReturn = state;
        }

        bool getReturnChecker() {
            return hasReturn;
        }

        Module* getParent() const {
            return parent;
        }

        TypeInfo* getReturnType() const {
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getReturnType() : nullptr;
        }

        TypeInfo* getSemanticReturnType() const {
            return getReturnType();
        }

        const std::vector<TypeInfo*>& getSemanticParamTypes() const {
            static const std::vector<TypeInfo*> empty;
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getArgTypes() : empty;
        }

        const std::vector<TypeInfo*>& getParamsOnlyType() const {
            return getSemanticParamTypes();
        }

        IRValue* buildBlock(fzlib::String id, std::vector<Instruction*> ops) {
            Block* block = new Block(id, controlType(), std::move(ops));
            block->setName(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        IRValue* buildBlock(fzlib::String id) {
            Block* block = new Block(id, controlType());
            block->setName(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        // 返回当前游标
        Block* curBlock() {
            return blocks[cursor];
        }

        void reset() {
            cursor = 0;
        }

        const std::vector<Block*>& getBlocks() const {
            return blocks;
        }

        Block* block(int index) {
            return blocks[index];
        }

        Function& moveCursor(long target) {
            if (target >= 0 && target < static_cast<long>(blocks.size())) {
                cursor = target;
            }
            else
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Move cursor to a unknown place",
                                    createInfo);
            return *this;
        }

        Scope<IRValue*>& fnScope() {
            return funcScope;
        }

        const std::vector<fzlib::String>& getFormalParamNames() const {
            return formalParamNames;
        }

        FormalParamsDefine getFormalParams() const {
            FormalParamsDefine result;
            const auto& types = getSemanticParamTypes();
            for (std::size_t i = 0; i < formalParamNames.size(); ++i) {
                result.emplace_back(formalParamNames[i], types.at(i));
            }
            return result;
        }

        long& cur() {
            return cursor;
        }

        Block* operator[] (int index) {
            return block(index);
        }

        const PositionInfo& getInfo() const {
            return createInfo;
        }

        fzlib::String toString() {
            fzlib::String result = "func " + name + "(";
            const auto formalParams = getFormalParams();
            for (std::size_t i = 0; i < formalParams.size(); i ++) {
                auto arg = formalParams[i];
                if (i == formalParams.size() - 1)
                    result += arg.first + ": " + arg.second->toString();
                else
                    result += arg.first + ": " + arg.second->toString() + ", ";
            }
            result += ") -> " + (getReturnType() ? getReturnType()->toString() : "<undeclared>") + " {";

            for (auto block: blocks) {
                result += block->toString();
            }

            result += "}";
            return result;
        }
    };
}

#endif /* !SAKURAE_FUNCTION_HPP */
