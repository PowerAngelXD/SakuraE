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
        lgc_not,
        create_alloca, 
        store,
        create_array,
        indexing,
        call,
        load,
        gmem,
        gaddr,
        deref,
        param,
        enter_scope,
        leave_scope,
        free_cur_heap,
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

        bool isTerminal() {
            return kind == OpKind::br ||
                    kind == OpKind::cond_br ||
                    kind == OpKind::ret;
        }

        bool isLValue() {
            return kind == OpKind::create_alloca ||
                    kind == OpKind::indexing ||
                    kind == OpKind::param ||
                    kind == OpKind::gmem ||
                    kind == OpKind::deref;
        }

        bool isRValue() {
            return kind == OpKind::constant ||
                    kind == OpKind::create_array ||
                    kind == OpKind::call ||
                    kind == OpKind::add ||
                    kind == OpKind::sub ||
                    kind == OpKind::mul ||
                    kind == OpKind::div ||
                    kind == OpKind::mod ||
                    kind == OpKind::lgc_eq_ls_than ||
                    kind == OpKind::lgc_eq_mr_than ||
                    kind == OpKind::lgc_ls_than ||
                    kind == OpKind::lgc_mr_than ||
                    kind == OpKind::lgc_equal ||
                    kind == OpKind::lgc_not_equal ||
                    kind == OpKind::lgc_not ||
                    kind == OpKind::load;
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
            fzlib::String result = "%" + name + " = " + magic_enum::enum_name(kind);
            for (auto arg: args) {
                if (!arg) {
                    result += " <null>";
                    continue;
                }
                
                if (auto* constant = dynamic_cast<Constant*>(arg)) {
                    result += " " + constant->getType()->toString() + " " + constant->toString();
                } 
                else {
                    result += " " + arg->getType()->toString() + " %" + arg->getName();
                }
            }
            return result;
        }
    };
}

#endif // !SAKURAE_INSTRUCTION_HPP