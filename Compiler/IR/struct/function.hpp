#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include "block.hpp"

namespace sakuraE::IR {
    struct Symbol {
        fzlib::String name;
        Type* type;

        Symbol(fzlib::String n, Type* t): name(n), type(t) {}
    };

    class Scope {
        std::vector<Symbol> symbolTable;
        PositionInfo createInfo;
    public:
        Scope(PositionInfo info): createInfo(info) {}

        void declare(fzlib::String n, Type* t) {
            symbolTable.emplace_back(n, t);
        }

        const Symbol& get(fzlib::String n) {
            for (const auto& sym: symbolTable) {
                if (sym.name == n) return sym;
            }

            throw SakuraError(OccurredTerm::IR_GENERATING, 
                            "Expected to get an unknown symbol in current scope!",
                            createInfo);
        }

        const Symbol& operator[] (fzlib::String n) {
            return get(n);
        }
    };

    using FormalParamsDefine = std::vector<std::pair<fzlib::String, Type*>>;
    class Module;

    // SakuraE Function
    class Function {
        fzlib::String funcName;
        FormalParamsDefine formalParams;
        Scope funcScope;

        PositionInfo createInfo;
        
        std::vector<Block*> blocks;
        // Indicates the current maximum index of blocks
        int cursor = -1;

        Module* parent;
    public:
        Function(fzlib::String name, PositionInfo info): 
            funcName(name), funcScope(info), createInfo(info) {}
        
        Function(fzlib::String name, FormalParamsDefine params, PositionInfo info): 
            funcName(name), formalParams(params), funcScope(info), createInfo(info) {}

        void setParent(Module* mod) {
            parent = mod;
        }

        Module* getParent() {
            return parent;
        }

        Value* buildBlock(fzlib::String id, std::vector<Instruction*> ops) {
            Block* block = new Block(id, ops);
            block->setParent(this);
            blocks.push_back(block);
            cursor ++;

            return Constant::get(cursor, Type::getBlockIndexTy(), createInfo);
        }

        Value* buildBlock(fzlib::String id) {
            Block* block = new Block(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor ++;

            return Constant::get(cursor, Type::getBlockIndexTy(), createInfo);
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