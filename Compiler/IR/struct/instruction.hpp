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
        decl, 
        assign, 
        decl_block, 
        decl_func, 
        decl_module,
        indexing,
        make_call_params,
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
        Instruction(OpKind k, Type* t): kind(kind), Value(t) {}
        Instruction(OpKind k, Type* t, std::vector<Value*> params): 
            kind(k), args(params), Value(t) {}

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

        const OpKind& kind() {
            return kind;
        }

        Value* operator[] (std::size_t pos) {
            return args.at(pos);
        }
    };
}

#endif // !SAKURAE_INSTRUCTION_HPP