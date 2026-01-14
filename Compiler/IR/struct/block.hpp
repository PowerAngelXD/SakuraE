#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "instruction.hpp"

namespace sakuraE::IR {
    class Function;
    // SakuraE IR Block
    // Rule: Every block id starts as '@'
    class Block {
        std::vector<Instruction*> instructions;
        fzlib::String ID = "@default-block";

        Function* parent = nullptr;
    public:
        Block(fzlib::String id, std::vector<Instruction*> ops): 
            instructions(ops), ID("@" + id) {}
        Block(fzlib::String id):
            instructions({}), ID("@" + id) {}

        ~Block() {
            for (auto ins: instructions) {
                delete ins;
            }
        }

        void setParent(Function* fn) {
            parent = fn;
        }

        Function* getParent() {
            return parent;
        }

        const fzlib::String& getID() {
            return ID;
        }

        const std::vector<Instruction*>& getInstructions() {
            return instructions;
        }

        Value* createInstruction(OpKind k, Type* t, const fzlib::String& n) {
            Instruction* ins = new Instruction(k, t, {});
            ins->setParent(this);
            ins->setName(n);
            instructions.push_back(ins);

            return ins;
        }

        Value* createInstruction(OpKind k, Type* t, std::vector<Value*> params, const fzlib::String& n) {
            Instruction* ins = new Instruction(k, t, params);
            ins->setParent(this);
            ins->setName(n);
            instructions.push_back(ins);

            return ins;
        }

        Instruction* op(std::size_t pos) {
            return instructions[pos];
        }

        Instruction* operator[] (std::size_t pos) {
            return instructions[pos];
        }
    };
}

#endif // !SAKURAE_BLOCK_HPP
