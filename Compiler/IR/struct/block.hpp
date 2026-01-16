#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "instruction.hpp"

namespace sakuraE::IR {
    class Function;
    // SakuraE IR Block
    // Rule: Every block id starts as '@'
    class Block: public IRValue {
        std::vector<Instruction*> instructions;
        fzlib::String ID = "@default-block";

        Function* parent = nullptr;
    public:
        Block(fzlib::String id, std::vector<Instruction*> ops): 
            IRValue(IRType::getBlockTy()), instructions(ops), ID("@" + id) {}
        Block(fzlib::String id):
            IRValue(IRType::getBlockTy()), instructions({}), ID("@" + id) {}

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

        IRValue* createInstruction(OpKind k, IRType* t, const fzlib::String& n) {
            if (instructions[instructions.size() - 1]->isTerminal())
                throw SakuraError(OccurredTerm::IR_GENERATING,  
                                    "Cannot create any instruction after terminal code!",
                                    {0, 0, "InsideError"});

            Instruction* ins = new Instruction(k, t, {});
            ins->setParent(this);
            ins->setName(n);
            instructions.push_back(ins);

            return ins;
        }

        IRValue* createInstruction(OpKind k, IRType* t, std::vector<IRValue*> params, const fzlib::String& n) {
            if (instructions[instructions.size() - 1]->isTerminal())
                throw SakuraError(OccurredTerm::IR_GENERATING,  
                                    "Cannot create any instruction after terminal code!",
                                    {0, 0, "InsideError"});

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
