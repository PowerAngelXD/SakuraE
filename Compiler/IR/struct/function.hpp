#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include <stack>
#include <utility>

#include "block.hpp"

namespace sakuraE::IR {
    struct Symbol: public Value {
        fzlib::String name;
        Value* address;

        Symbol(fzlib::String n, Value* addr, Type* t): name(n), address(addr), Value(t) {}
    };

    class Scope {
        std::vector<std::map<fzlib::String, Symbol>> symbolTable;
        std::size_t cursor = 0;

        PositionInfo createInfo;

        Scope* parent = nullptr;

        std::map<fzlib::String, Symbol>& top() {
            return symbolTable[cursor];
        }
    public:
        Scope(PositionInfo info): createInfo(info) {
            symbolTable.emplace_back();
        }

        void declare(fzlib::String n, Value* addr, Type* t) {
            top().emplace(n, Symbol(n, addr, t));
        }

        Symbol* lookup(const fzlib::String& name) {
            for (auto it = symbolTable.rbegin(); it != symbolTable.rend(); it --) {
                if (it->find(name) != it->end()) {
                    return &(*it)[name];
                }
            }

            if (parent)
                return parent->lookup(name);
            
            return nullptr;
        }
    };

    using FormalParamsDefine = std::vector<std::pair<fzlib::String, Type*>>;
    class Module;

    // SakuraE Function
    class Function {
        fzlib::String funcName;
        Type* returnType;
        FormalParamsDefine formalParams;
        Scope funcScope;

        PositionInfo createInfo;
        
        std::vector<Block*> blocks;
        // Indicates the current maximum index of blocks
        int cursor = -1;

        Module* parent;
    public:
        Function(fzlib::String name, Type* retType, PositionInfo info): 
            funcName(name), returnType(retType), funcScope(info), createInfo(info) {}
        
        Function(fzlib::String name, Type* retType, FormalParamsDefine params, PositionInfo info): 
            funcName(name), returnType(retType), formalParams(params), funcScope(info), createInfo(info) {}

        void setParent(Module* mod) {
            parent = mod;
        }

        Module* getParent() {
            return parent;
        }

        Type* getReturnType() {
            return returnType;
        }

        std::vector<Type*> getParamsOnlyType() {
            std::vector<Type*> result;
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

            return Constant::get(Type::getBlockTy(), createInfo);
        }

        Value* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor ++;

            return Constant::get(Type::getBlockTy(), createInfo);
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

        Scope& scope() {
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