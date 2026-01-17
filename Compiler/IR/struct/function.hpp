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
        Scope funcScope;

        PositionInfo createInfo;
        
        std::vector<Block*> blocks;
        // Indicates the current maximum index of blocks
        long cursor = -1;

        Module* parent;
    public:
        Function(fzlib::String n, IRType* retType, PositionInfo info): 
            IRValue(IRType::getFunctionTy(retType, {})), funcName(n), returnType(retType), funcScope(info), createInfo(info) {}
        
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
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        IRValue* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        // Return current cursor
        Block* curBlock() {
            return blocks[cursor];
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
                                    "Move cursor to a unkonwn place",
                                    createInfo);
            return *this;
        }

        const fzlib::String& getName() {
            return funcName;
        }

        Scope& fnScope() {
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
    };
}

#endif // !SAKURAE_FUNCTION_HPP