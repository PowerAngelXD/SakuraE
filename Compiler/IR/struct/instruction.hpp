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
        lgc_not,
        lgc_or,
        declare, 
        assign, 
        id_to_type,
        create_block, 
        create_func, 
        create_module,
        create_array,
        indexing,
        call,
        load,
        gmem,
        // terminal op
        br,
        cond_br,
        ret
    };

    class Block;

    class Instruction: public IRValue {
        OpKind kind = OpKind::empty;
        std::vector<IRValue*> args;

        Block* parent = nullptr;
    public:
        Instruction(OpKind k, IRType* t): IRValue(t), kind(k) {}
        Instruction(OpKind k, IRType* t, std::vector<IRValue*> params): 
            IRValue(t), kind(k), args(params) {}

        ~Instruction() {
            for (auto arg: args) {
                delete arg;
            }
        }

        bool isTerminal() {
            return kind == OpKind::br ||
                    kind == OpKind::cond_br;
        }

        void setParent(Block* blk) {
            parent = blk;
        }

        Block* getParent() {
            return parent;
        }

        const std::vector<IRValue*>& getOperands() {
            return args;
        }

        IRValue* arg(std::size_t pos) {
            return args.at(pos);
        }

        const OpKind& getKind() {
            return kind;
        }

        IRValue* operator[] (std::size_t pos) {
            return args.at(pos);
        }

        fzlib::String toString() {
            fzlib::String result = magic_enum::enum_name(kind);
            for (auto arg: args) {
                result += " $" + (arg ? arg->getName() : "<null>");
            }
            return result;
        }
    };
}

#endif // !SAKURAE_INSTRUCTION_HPP