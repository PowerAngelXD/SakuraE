#ifndef SAKURAE_INSTRUCTION_HPP
#define SAKURAE_INSTRUCTION_HPP

#include "Compiler/IR/Semantic/value/constant.hpp"

namespace sakuraE::IR {
    // OpKind：语义 IR 指令的操作码。每条 Instruction 的结果语义类型(TypeInfo)由创建方给出，
    // 操作数含义由各操作码约定；Backend 代码生成（LLVMCodegenerator.cpp）按操作码分派。
    enum class OpKind {
        // 空操作，代码生成时直接跳过。
        empty,
        // 常量物化：arg(0) 为 Constant。
        constant,
        // 算术运算：arg(0)、arg(1) 为左右操作数（代码生成时先拆箱为原始值，再装箱结果）。
        add,
        sub,
        mul,
        mod,
        div,
        // 比较运算：等于、不等于、大于、小于、大于等于、小于等于；结果为布尔。
        lgc_equal,
        lgc_not_equal,
        lgc_mr_than,
        lgc_ls_than,
        lgc_eq_mr_than,
        lgc_eq_ls_than,
        // 逻辑非：arg(0) 为单个操作数。
        lgc_not,
        // 为局部变量创建栈槽（左值）；名称形如 "xxx.<标识符>"，代码生成会从名称中解析变量名。
        create_alloca,
        // 写入：arg(0) 为目标地址，arg(1) 为要存入的值。
        store,
        // 由数组常量创建数组：arg(0) 为数组 Constant。
        create_array,
        // 下标访问：arg(0) 为基地址，arg(1) 为下标；结果为元素地址（左值）。
        indexing,
        // 结构体构造（预留，当前 LLVM 代码生成未实现）。
        create_struct,
        // 结构体字段取地址 / 带检查的字段取地址（预留，当前 LLVM 代码生成未实现）。
        struct_gaddr,
        struct_safe_gaddr,
        // 函数调用；名称形如 "call.<函数名>"，代码生成从名称中截取被调函数名。
        call,
        // 读取：arg(0) 为地址，结果为该地址中的值。
        load,
        // 成员访问：arg(0) 为结构体基地址，arg(1) 为字段名 Constant；lowering 阶段解析为字段访问表(fieldAccess)。
        gmem,
        // 取地址：arg(0) 为左值地址；结果为指针或引用类型（一等指针在运行时装箱为 RuntimeValue）。
        gaddr,
        // 解引用：arg(0) 为指针值，结果为被指向对象的地址（左值）。
        deref,
        // 函数形参；名称形如 "param.<形参名>"。
        param,
        // 进入 / 离开词法作用域：代码生成时对应 GC root 作用域的压入与弹出。
        enter_scope,
        leave_scope,
        // typeof：返回类型描述符（LLVM 代码生成暂未实现，会抛错）。
        _typeof,
        // sizeof：由 typeOperand 给出目标语义类型，结果为其 storage 类型的分配大小（int64）。
        _sizeof,
        // 终结操作：br 无条件跳转（arg(0) 为目标块）；cond_br 条件跳转（条件、真块、假块）；
        // ret 返回（可无操作数），返回前会离开全部 GC 作用域。
        br,
        cond_br,
        ret
    };

    class Block;

    // Instruction：语义 IR 中的一条指令，本身也是 IRValue（其结果可作为其他指令的操作数）。
    // 操作数 args 与 parent 都是非拥有引用；typeOperand 用于 sizeof 等以类型为参数的指令。
    class Instruction: public IRValue {
        OpKind kind = OpKind::empty;
        std::vector<IRValue*> args;
        TypeInfo* typeOperand = nullptr;

        Block* parent = nullptr;
    public:
        // 构造指令；每条指令都必须具有结果语义类型（无值指令也使用占位类型），否则视为内部错误。
        Instruction(OpKind kind, TypeInfo* type, std::vector<IRValue*> params = {})
            : IRValue(type), kind(kind), args(std::move(params)) {
            if (!type) throw std::invalid_argument("Instruction requires a semantic result type");
        }

        // 是否为基本块终结指令（br / cond_br / ret）。
        bool isTerminal() {
            return kind == OpKind::br ||
                    kind == OpKind::cond_br ||
                    kind == OpKind::ret;
        }

        // 是否产生左值（可寻址的存储位置），供 store/gaddr 等判断目标合法性。
        bool isLValue() {
            return kind == OpKind::create_alloca ||
                    kind == OpKind::indexing ||
                    kind == OpKind::param ||
                    kind == OpKind::gmem ||
                    kind == OpKind::deref ||
                    kind == OpKind::struct_gaddr ||
                    kind == OpKind::struct_safe_gaddr;
        }

        // 是否产生右值（纯值结果）。注意 isLValue 与 isRValue 并非互补，如 store、gaddr 两者都不是。
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

        // 所属基本块的设置与获取（非拥有引用，由 Block 在追加指令时设置）。
        void setParent(Block* blk) {
            parent = blk;
        }

        Block* getParent() const {
            return parent;
        }

        // 操作数访问：getOperands 返回全部操作数；arg 与 operator[] 按下标访问（越界抛 std::out_of_range）。
        const std::vector<IRValue*>& getOperands() const {
            return args;
        }

        IRValue* arg(std::size_t pos) const {
            return args.at(pos);
        }

        // 返回操作码。
        const OpKind& getKind() const {
            return kind;
        }

        // 类型操作数的设置与获取（如 sizeof 的目标类型），未设置时为 nullptr。
        void setTypeOperand(TypeInfo* type) {
            typeOperand = type;
        }

        TypeInfo* getTypeOperand() const {
            return typeOperand;
        }

        IRValue* operator[] (std::size_t pos) {
            return args.at(pos);
        }

        // 生成调试用文本表示："%名称 = 操作码 [类型 值]..."。
        // 常量操作数直接打印值；其他值打印 %名称，无语义类型的值（如基本块）显示为 "label"。
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
