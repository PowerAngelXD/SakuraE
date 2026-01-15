#ifndef SAKURAE_INSTRUCTION_HPP
#define SAKURAE_INSTRUCTION_HPP

#include "Compiler/IR/value/constant.hpp"

namespace sakuraE::IR {
    enum class OpKind {
        empty,
        constant,
        add, 
        sub, 
        mul, 
        mod,
        div,
        lgc_equal, 
        lgc_not_equal,
        lgc_mr_than, 
        lgc_ls_than, 
        lgc_eq_mr_than, 
        lgc_eq_ls_than,
        lgc_and,
        lgc_or,
        declare, 
        assign, 
        create_block, 
        create_func, 
        create_module,
        create_array,
        indexing,
        call,
        load,
        gmem,
    };

    class Block;

    class Instruction: public Value {
        OpKind kind = OpKind::empty;
        std::vector<Value*> args;

        Block* parent = nullptr;
    public:
        Instruction(OpKind k, IRType* t): Value(t), kind(k) {}
        Instruction(OpKind k, IRType* t, std::vector<Value*> params): 
            Value(t), kind(k), args(params) {}

        ~Instruction() {
            for (auto arg: args) {
                delete arg;
            }
        }

        void setParent(Block* blk) {
            parent = blk;
        }

        Block* getParent() {
            return parent;
        }

        const std::vector<Value*>& getOperands() {
            return args;
        }

        Value* arg(std::size_t pos) {
            return args.at(pos);
        }

        const OpKind& getKind() {
            return kind;
        }

        Value* operator[] (std::size_t pos) {
            return args.at(pos);
        }
    };
}

#endif // !SAKURAE_INSTRUCTION_HPP