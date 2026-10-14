#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include <stack>
#include <utility>

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Backend/context/context.hpp"
#include "block.hpp"
#include "scope.hpp"

namespace sakuraE::IR {
    using FormalParamsDefine = std::vector<std::pair<fzlib::String, IRType*>>;
    class Module;

    // SakuraE 函数
    class Function: public CallableValue {
        fzlib::String rawName;
        IRType* returnType;
        FormalParamsDefine formalParams;
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

        Module* parent;
    public:
        Function(fzlib::String n, IRType* retType, PositionInfo info):
            CallableValue(nullptr, n),
            returnType(retType), funcScope(info), createInfo(info) {
            setStorageType(IRType::getFunctionTy(retType, {}));
        }

        // 仅用于预声明
        Function(fzlib::String n, PositionInfo info):
            CallableValue(nullptr, n), returnType(nullptr), formalParams({}),
            funcScope(info), createInfo(info) {}

        Function(fzlib::String n, IRType* retType, FormalParamsDefine params, PositionInfo info,
                 std::vector<TypeInfo*> semanticParams = {}, TypeInfo* semanticReturn = nullptr):
            CallableValue(nullptr, n), returnType(retType), formalParams(params),
                funcScope(info), createInfo(info) {
            std::vector<IRType*> storageParams;
            for (const auto& parameter : params) storageParams.push_back(parameter.second);
            setStorageType(IRType::getFunctionTy(retType, std::move(storageParams)));
            if (semanticReturn) setFunctionType(semanticReturn->getContext().typeInfoPool().makeFunctionTypeID(semanticReturn, std::move(semanticParams)));
        }

        Function(fzlib::String n, fzlib::String raw, IRType* retType, FormalParamsDefine params, PositionInfo info):
            CallableValue(nullptr, n), rawName(raw), returnType(retType), formalParams(params), funcScope(info), createInfo(info) {
            std::vector<IRType*> storageParams;
            for (const auto& parameter : params) storageParams.push_back(parameter.second);
            setStorageType(IRType::getFunctionTy(retType, std::move(storageParams)));
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

        fzlib::String getRawName() { return rawName; }

        void setFuncDefineInfo(FormalParamsDefine params, IRType* retType,
                               std::vector<TypeInfo*> semanticParams = {},
                               TypeInfo* semanticReturn = nullptr) {
            formalParams = params;
            returnType = retType;
            if (semanticReturn) setFunctionType(semanticReturn->getContext().typeInfoPool().makeFunctionTypeID(semanticReturn, std::move(semanticParams)));

            setStorageType(IRType::getFunctionTy(retType,
                [&]() -> std::vector<IRType*> {
                    std::vector<IRType*> result;
                    for (auto param: params) {
                        result.push_back(param.second);
                    }
                    return result;
                }()
            ));
        }

        void setReturnChecker(bool state) {
            hasReturn = state;
        }

        bool getReturnChecker() {
            return hasReturn;
        }

        Module* getParent() {
            return parent;
        }

        IRType* getReturnType() {
            return returnType;
        }

        TypeInfo* getSemanticReturnType() const {
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getReturnType() : nullptr;
        }

        const std::vector<TypeInfo*>& getSemanticParamTypes() const {
            static const std::vector<TypeInfo*> empty;
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getArgTypes() : empty;
        }

        std::vector<IRType*> getParamsOnlyType() {
            std::vector<IRType*> result;
            for (auto param: formalParams) {
                result.push_back(param.second);
            }

            return result;
        }

        IRValue* buildBlock(fzlib::String id, std::vector<Instruction*> ops) {
            Block* block = new Block(id, ops);
            block->setName(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        IRValue* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
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

        std::vector<Block*> getBlocks() {
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

        const FormalParamsDefine& getFormalParams() const {
            return formalParams;
        }

        long& cur() {
            return cursor;
        }

        Block* operator[] (int index) {
            return block(index);
        }

        PositionInfo& getInfo() {
            return createInfo;
        }

        fzlib::String toString() {
            fzlib::String result = "func " + name + "(";
            for (std::size_t i = 0; i < formalParams.size(); i ++) {
                auto arg = formalParams[i];
                if (i == formalParams.size() - 1)
                    result += arg.first + ": " + arg.second->toString();
                else
                    result += arg.first + ": " + arg.second->toString() + ", ";
            }
            result += ") -> " + returnType->toString() + " {";

            for (auto block: blocks) {
                result += block->toString();
            }

            result += "}";
            return result;
        }
    };
}

#endif /* !SAKURAE_FUNCTION_HPP */
