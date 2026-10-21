#ifndef SAKURAE_BLOCK_HPP
#define SAKURAE_BLOCK_HPP

#include "Compiler/IR/Semantic/value/value.hpp"
#include "instruction.hpp"
#include <cstddef>

namespace sakuraE::IR {
    class Function;
    // SakuraE IR 基本块：有序指令序列，本身也是 IRValue（可作为 br/cond_br 的跳转目标操作数）。
    // Block 拥有其中全部 Instruction（析构时 delete）；parent 为所属 Function 的非拥有引用。
    // 不变量：终结指令（br/cond_br/ret）只能位于末尾，其后不得再追加指令。
    class Block: public IRValue {
        std::vector<Instruction*> instructions;
        fzlib::String ID = "default-block";

        Function* parent = nullptr;
    public:
        // 构造基本块：id 为块标识，type 为块自身的语义类型（同时被用作终结/作用域指令的结果类型），
        // ops 为预先构造的指令（所有权转移给本块）。
        Block(fzlib::String id, TypeInfo* type, std::vector<Instruction*> ops):
            IRValue(type), instructions(std::move(ops)), ID(id) {}
        Block(fzlib::String id, TypeInfo* type):
            IRValue(type), ID(id) {}

        // 释放本块拥有的全部指令。
        ~Block() {
            for (auto ins: instructions) {
                delete ins;
            }
        }

        // 所属函数的设置与获取（非拥有引用）。
        void setParent(Function* fn) {
            parent = fn;
        }

        Function* getParent() const {
            return parent;
        }

        // 返回块标识与指令列表（只读）。
        const fzlib::String& getID() const {
            return ID;
        }

        const std::vector<Instruction*>& getInstructions() const {
            return instructions;
        }

        // 创建无操作数指令的便捷重载。
        IRValue* createInstruction(OpKind kind, TypeInfo* type, const fzlib::String& name) {
            return createInstruction(kind, type, {}, name);
        }

        // 在块末尾追加一条新指令并返回它；本块拥有该指令并设置其 parent 与名称。
        // 若块已终结则报错：终结指令之后的代码不可达，追加属于 IR 生成逻辑错误。
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

        // 将指令插入到终结指令之前（块未终结时直接追加）。
        // 用于需要在已终结块中补发的指令，如 leave_scope 必须在跳转/返回之前执行。
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

        // 创建无条件跳转；若块已终结则不重复创建并返回 nullptr（常见于 return 后的分支汇合）。
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

        // 创建条件跳转（cond 为真跳 thenBlock，否则跳 elseBlock）；块已终结时返回 nullptr。
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

        // 创建带返回值的 ret；块已终结时返回 nullptr。
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

        // 创建无返回值的 ret；与带值版本不同，这里不检查是否已终结，已终结时会由 createInstruction 抛错。
        IRValue* createReturn() {
            return createInstruction(OpKind::ret,
                                    getSemanticType(),
                                    {},
                                    "ret");
        }

        // 在块末尾追加 enter_scope，标记进入词法作用域（代码生成时压入 GC root 作用域）。
        IRValue* createEnterScope() {
            return createInstruction(OpKind::enter_scope,
                                    getSemanticType(),
                                    {},
                                    "enter_scope");
        }

        // 插入 leave_scope 到终结指令之前，确保离开作用域（弹出 GC root 作用域）发生在跳转/返回之前。
        IRValue* createLeaveScope() {
            return insertBeforeTerminal(OpKind::leave_scope,
                                    getSemanticType(),
                                    {},
                                    "leave_scope");
        }

        // 按下标访问指令（不做越界检查）。
        Instruction* op(std::size_t pos) {
            return instructions[pos];
        }

        Instruction* operator[] (std::size_t pos) {
            return instructions[pos];
        }

        // 生成调试用文本表示："@ID {指令;指令;...}"。
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
