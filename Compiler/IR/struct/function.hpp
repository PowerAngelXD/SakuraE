#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include <stack>
#include <utility>

#include "block.hpp"
#include "scope.hpp"

namespace sakuraE::IR {
    using FormalParamsDefine = std::vector<std::pair<fzlib::String, IRType*>>;
    class Module;

    // SakuraE Function
    class Function: public IRValue {
        fzlib::String funcName;
        IRType* returnType;
        FormalParamsDefine formalParams;
        Scope<IRValue*> funcScope;

        PositionInfo createInfo;
        
        std::vector<Block*> blocks;
        // Indicates the current maximum index of blocks
        long cursor = -1;

        Module* parent;
    public:
        Function(fzlib::String n, IRType* retType, PositionInfo info): 
            IRValue(IRType::getFunctionTy(retType, {})), funcName("#" + n), returnType(retType), funcScope(info), createInfo(info) {}
        
        // just for pre-defing
        Function(fzlib::String n, PositionInfo info):
            IRValue(nullptr), funcName(n), returnType(nullptr), formalParams({}), funcScope(info), createInfo(info) {}
        
        Function(fzlib::String n, IRType* retType, FormalParamsDefine params, PositionInfo info): 
            IRValue(IRType::getFunctionTy(retType, 
                [&]() -> std::vector<IRType*> {
                    std::vector<IRType*> result;
                    for (auto param: params) {
                        result.push_back(param.second);
                    }
                    return result;
                }())), funcName(n), returnType(retType), formalParams(params), funcScope(info), createInfo(info) {}

        void setParent(Module* mod) {
            parent = mod;
        }

        void setFuncDefineInfo(FormalParamsDefine params, IRType* retType) {
            formalParams = params;
            returnType = retType;

            setType(IRType::getFunctionTy(retType, 
                [&]() -> std::vector<IRType*> {
                    std::vector<IRType*> result;
                    for (auto param: params) {
                        result.push_back(param.second);
                    }
                    return result;
                }()
            ));

            setName("#" + funcName);
        }

        Module* getParent() {
            return parent;
        }

        IRType* getReturnType() {
            return returnType;
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
            block->setName("%" + id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        IRValue* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
            block->setName("%" + id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        // Return current cursor
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

        const fzlib::String& getName() {
            return funcName;
        }

        Scope<IRValue*>& fnScope() {
            return funcScope;
        }

        const FormalParamsDefine& getFormalParams() const {
            return formalParams;
        }

        const long& cur() {
            return cursor;
        }

        Block* operator[] (int index) {
            return block(index);
        }

        fzlib::String toString() {
            fzlib::String result = funcName + "(";
            for (std::size_t i = 0; i < formalParams.size(); i ++) {
                auto arg = formalParams[i];
                if (i == formalParams.size() - 1)
                    result += arg.first + arg.second->toString();
                else
                    result += arg.first + arg.second->toString() + ", ";
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

#endif // !SAKURAE_FUNCTION_HPP