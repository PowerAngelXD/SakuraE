#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "Compiler/IR/value/value.hpp"
#include "instruction.hpp"
#include <cstddef>

namespace sakuraE::IR {
    class Function;
    // SakuraE IR Block
    // Rule: Every block id starts as ':'
    class Block: public IRValue {
        std::vector<Instruction*> instructions;
        fzlib::String ID = ":default-block";

        Function* parent = nullptr;
    public:
        Block(fzlib::String id, std::vector<Instruction*> ops): 
            IRValue(IRType::getBlockTy()), instructions(ops), ID(":" + id) {}
        Block(fzlib::String id):
            IRValue(IRType::getBlockTy()), instructions({}), ID(":" + id) {}

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
            if (!instructions.empty()) {
                if (instructions[instructions.size() - 1]->isTerminal())
                    throw SakuraError(OccurredTerm::IR_GENERATING,  
                                        "Cannot create any instruction after terminal code!",
                                        {0, 0, "InsideError"});
            }

            Instruction* ins = new Instruction(k, t, params);
            ins->setParent(this);
            ins->setName(n);
            instructions.push_back(ins);

            return ins;
        }

        IRValue* insertBeforeTerminal(OpKind k, IRType* t, std::vector<IRValue*> params, const fzlib::String& n) {
            Instruction* ins = new Instruction(k, t, params);
            ins->setParent(this);
            ins->setName(n);

            if (!instructions.empty() && instructions.back()->isTerminal()) {
                instructions.insert(instructions.end() - 1, ins);
            }
            else {
                instructions.push_back(ins);
            }

            return ins;
        }

        IRValue* createBr(IRValue* targetBlock) {
            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::br,
                                        IRType::getVoidTy(),
                                    {targetBlock},
                                        "br.(" + targetBlock->getName() + ")");
            
            return nullptr;
        }

        IRValue* createCondBr(IRValue* cond, IRValue* thenBlock, IRValue* elseBlock) {
            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::cond_br,
                                        IRType::getVoidTy(),
                                        {cond, thenBlock, elseBlock},
                                        "cond_br.(" + thenBlock->getName() + ").(" + elseBlock->getName() + ")");
            
            return nullptr;
        }

        IRValue* createReturn(IRValue* value) {
            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::ret,
                                        IRType::getVoidTy(),
                                        {value},
                                        "ret");
            
            return nullptr;
        }

        IRValue* createFree() {
            return insertBeforeTerminal(OpKind::free_cur_heap,
                                    IRType::getVoidTy(),
                                    {},
                                    "free_cur_heap");
        }

        IRValue* createEnterScope() {
            return createInstruction(OpKind::enter_scope,
                                    IRType::getVoidTy(),
                                    {},
                                    "enter_scope");
        }

        IRValue* createLeaveScope() {
            return insertBeforeTerminal(OpKind::leave_scope,
                                    IRType::getVoidTy(),
                                    {},
                                    "leave_scope");
        }

        Instruction* op(std::size_t pos) {
            return instructions[pos];
        }

        Instruction* operator[] (std::size_t pos) {
            return instructions[pos];
        }

        fzlib::String toString() {
            fzlib::String result = ID + " {";
            for (auto ins: instructions) {
                result += ins->toString() + ";";
            }
            result += "}";

            return result;
        }
    };
}

#endif // !SAKURAE_BLOCK_HPP
