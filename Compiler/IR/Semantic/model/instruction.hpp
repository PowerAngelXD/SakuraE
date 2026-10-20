#ifndef SAKURAE_INSTRUCTION_HPP
#define SAKURAE_INSTRUCTION_HPP

#include "Compiler/IR/Semantic/value/constant.hpp"

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
        create_struct,
        struct_gaddr,
        struct_safe_gaddr,
        call,
        load,
        gmem,
        gaddr,
        deref,
        param,
        enter_scope,
        leave_scope,
        _typeof,
        _sizeof,
        // 终结操作
        br,
        cond_br,
        ret
    };

    class Block;

    class Instruction: public IRValue {
        OpKind kind = OpKind::empty;
        std::vector<IRValue*> args;
        TypeInfo* typeOperand = nullptr;

        Block* parent = nullptr;
    public:
        Instruction(OpKind kind, TypeInfo* type, std::vector<IRValue*> params = {})
            : IRValue(type), kind(kind), args(std::move(params)) {
            if (!type) throw std::invalid_argument("Instruction requires a semantic result type");
        }

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
                    kind == OpKind::deref ||
                    kind == OpKind::struct_gaddr ||
                    kind == OpKind::struct_safe_gaddr;
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
                    kind == OpKind::load ||
                    kind == OpKind::create_struct;
        }

        void setParent(Block* blk) {
            parent = blk;
        }

        Block* getParent() const {
            return parent;
        }

        const std::vector<IRValue*>& getOperands() const {
            return args;
        }

        IRValue* arg(std::size_t pos) const {
            return args.at(pos);
        }

        const OpKind& getKind() const {
            return kind;
        }

        void setTypeOperand(TypeInfo* type) {
            typeOperand = type;
        }

        TypeInfo* getTypeOperand() const {
            return typeOperand;
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
                    result += " " + constant->getSemanticType()->toString() + " " + constant->toString();
                }
                else {
                    result += " " + (arg->getSemanticType() ? arg->getSemanticType()->toString() : "label") + " %" + arg->getName();
                }
            }
            return result;
        }
    };
}

#endif /* !SAKURAE_INSTRUCTION_HPP */
