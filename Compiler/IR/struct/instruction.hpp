#ifndef SAKURAE_INSTRUCTION_HPP
#define SAKURAE_INSTRUCTION_HPP

#include "Compiler/IR/value/value.hpp"

namespace sakuraE::IR {
    enum class OpKind {
        empty,
        constant,
        add, 
        sub, 
        mul, 
        div,
        lgc_equal, 
        lgc_mr_than, 
        lgc_ls_than, 
        lgc_eq_mr_than, 
        lgc_eq_ls_than,
        decl, 
        assign, 
        decl_block, 
        decl_func, 
        decl_module,
        indexing,
        call
    };

    class Instruction: public Value {
        OpKind kind = OpKind::empty;
        std::vector<Value> args;
    public:
        Instruction(OpKind k, Type* t): kind(kind), Value(t) {}
        Instruction(OpKind k, Type* t, std::vector<Value> params): 
            kind(k), args(params), Value(t) {}

        const Value& arg(std::size_t pos) {
            return args.at(pos);
        }

        const OpKind& kind() {
            return kind;
        }

        const Value& operator[] (std::size_t pos) {
            return args.at(pos);
        }
    };
}

#endif // !SAKURAE_INSTRUCTION_HPP