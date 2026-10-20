#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "Compiler/IR/Semantic/value/value.hpp"
#include "instruction.hpp"
#include <cstddef>

namespace sakuraE::IR {
    class Function;
    // SakuraE IR 基本块
    class Block: public IRValue {
        std::vector<Instruction*> instructions;
        fzlib::String ID = "default-block";

        Function* parent = nullptr;
    public:
        Block(fzlib::String id, TypeInfo* type, std::vector<Instruction*> ops):
            IRValue(type), instructions(std::move(ops)), ID(id) {}
        Block(fzlib::String id, TypeInfo* type):
            IRValue(type), ID(id) {}

        ~Block() {
            for (auto ins: instructions) {
                delete ins;
            }
        }

        void setParent(Function* fn) {
            parent = fn;
        }

        Function* getParent() const {
            return parent;
        }

        const fzlib::String& getID() const {
            return ID;
        }

        const std::vector<Instruction*>& getInstructions() const {
            return instructions;
        }

        IRValue* createInstruction(OpKind kind, TypeInfo* type, const fzlib::String& name) {
            return createInstruction(kind, type, {}, name);
        }

        IRValue* createInstruction(OpKind kind, TypeInfo* type, std::vector<IRValue*> operands,
                                   const fzlib::String& name) {
            if (!instructions.empty() && instructions.back()->isTerminal()) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                  "Cannot create any instruction after terminal code!",
                                  {0, 0, "InsideError"});
            }
            auto* instruction = new Instruction(kind, type, std::move(operands));
            instruction->setParent(this);
            instruction->setName(name);
            instructions.push_back(instruction);
            return instruction;
        }

        IRValue* insertBeforeTerminal(OpKind k, TypeInfo* t, std::vector<IRValue*> params, const fzlib::String& n) {
            Instruction* ins = new Instruction(
                k, t, std::move(params));
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
            if (instructions.empty())
                return createInstruction(OpKind::br,
                                        getSemanticType(),
                                    {targetBlock},
                                        "br.(" + targetBlock->getName() + ")");

            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::br,
                                        getSemanticType(),
                                    {targetBlock},
                                        "br.(" + targetBlock->getName() + ")");

            return nullptr;
        }

        IRValue* createCondBr(IRValue* cond, IRValue* thenBlock, IRValue* elseBlock) {
            if (instructions.empty()) 
                return createInstruction(OpKind::cond_br,
                                        getSemanticType(),
                                        {cond, thenBlock, elseBlock},
                                        "cond_br.(" + thenBlock->getName() + ").(" + elseBlock->getName() + ")");

            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::cond_br,
                                        getSemanticType(),
                                        {cond, thenBlock, elseBlock},
                                        "cond_br.(" + thenBlock->getName() + ").(" + elseBlock->getName() + ")");

            return nullptr;
        }

        IRValue* createReturn(IRValue* value) {
            if (instructions.empty()) 
                return createInstruction(OpKind::ret,
                                        getSemanticType(),
                                        {value},
                                        "ret");

            if (!instructions.back()->isTerminal())
                return createInstruction(OpKind::ret,
                                        getSemanticType(),
                                        {value},
                                        "ret");

            return nullptr;
        }

        IRValue* createReturn() {
            return createInstruction(OpKind::ret,
                                    getSemanticType(),
                                    {},
                                    "ret");
        }

        IRValue* createEnterScope() {
            return createInstruction(OpKind::enter_scope,
                                    getSemanticType(),
                                    {},
                                    "enter_scope");
        }

        IRValue* createLeaveScope() {
            return insertBeforeTerminal(OpKind::leave_scope,
                                    getSemanticType(),
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
            fzlib::String result = "@" + ID + " {";
            for (auto ins: instructions) {
                result += ins->toString() + ";";
            }
            result += "}";

            return result;
        }
    };
}

#endif /* !SAKURAE_BLOCK_HPP */
