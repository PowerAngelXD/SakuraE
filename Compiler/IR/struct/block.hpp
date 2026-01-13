#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "instruction.hpp"

namespace sakuraE::IR {
    // SakuraE IR Block
    // Rule: Every block id starts as '@'
    class Block {
        std::vector<Instruction> instructions;
        fzlib::String ID = "@default-block";
    public:
        Block(fzlib::String id, std::vector<Instruction> ops): 
            instructions(ops), ID("@" + id) {}
        Block(fzlib::String id):
            instructions({}), ID("@" + id) {}

        const fzlib::String& getID() {
            return ID;
        }

        Block& insert(const Instruction& ins) {
            instructions.push_back(ins);

            return *this;
        }

        Block& insert(OpKind k) {
            instructions.emplace_back(k);

            return *this;
        }

        Block& insert(OpKind k, std::vector<Value> params) {
            instructions.emplace_back(k, params);

            return *this;
        }

        const Instruction& op(std::size_t pos) {
            return instructions[pos];
        }

        const Instruction& operator[] (std::size_t pos) {
            return instructions[pos];
        }
    };
}

#endif // !SAKURAE_BLOCK_HPP
