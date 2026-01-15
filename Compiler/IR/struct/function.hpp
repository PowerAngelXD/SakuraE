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
    class Function: public Value {
        fzlib::String funcName;
        IRType* returnType;
        FormalParamsDefine formalParams;
        Scope funcScope;

        PositionInfo createInfo;
        
        std::vector<Block*> blocks;
        // Indicates the current maximum index of blocks
        int cursor = -1;

        Module* parent;
    public:
        Function(fzlib::String name, IRType* retType, PositionInfo info): 
            Value(IRType::getFunctionTy(retType, {})), funcName(name), returnType(retType), funcScope(info), createInfo(info) {}
        
        Function(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info): 
            Value(IRType::getFunctionTy(retType, 
                [&]() -> std::vector<IRType*> {
                    std::vector<IRType*> result;
                    for (auto param: params) {
                        result.push_back(param.second);
                    }
                    return result;
                }())), funcName(name), returnType(retType), formalParams(params), funcScope(info), createInfo(info) {}

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

        Value* buildBlock(fzlib::String id, std::vector<Instruction*> ops) {
            Block* block = new Block(id, ops);
            block->setParent(this);
            blocks.push_back(block);
            cursor ++;

            return block;
        }

        Value* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor ++;

            return block;
        }

        // Return current cursor
        Block* curBlock() {
            return blocks[cursor];
        }

        Block* block(std::size_t index) {
            return blocks[index];
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

        const int& cur() {
            return cursor;
        }

        Block* operator[] (std::size_t index) {
            return block(index);
        }
    };
}

#endif // !SAKURAE_FUNCTION_HPP