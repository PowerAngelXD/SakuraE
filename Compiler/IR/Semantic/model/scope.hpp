#ifndef SAKURAE_SCOPE_HPP
#define SAKURAE_SCOPE_HPP

#include <stack>
#include <utility>

#include "Compiler/IR/Semantic/value/constant.hpp"

namespace sakuraE::IR {
    // 符号表条目：把标识符名称绑定到一个"地址"值上。
    // T 必须是指针类型：语义 IR 阶段为 IRValue*（create_alloca/param 指令、Function 等），
    // LLVM codegen 阶段为 llvm::Value*（FunctionEmiiter 中的 alloca）。
    // 继承 IRValue 只是为了复用其名称与语义类型(TypeInfo)字段；类型为非拥有引用，
    // codegen 侧声明时传 nullptr。address 同样是非拥有引用，生命周期由所属 Function/Module 管理。
    template<typename T>
    requires std::is_pointer_v<T>
    struct Symbol: public IRValue {
        T address = nullptr;

        Symbol(fzlib::String n, T addr, TypeInfo* type):
            IRValue(type, std::move(n)), address(addr) {}
    };

    // 词法作用域：一个 Scope 对应一个函数（Function::funcScope）或一个模块（Module::moduleScope）。
    // 内部用"表栈"表示嵌套块：每次 enter() 压入一层新的符号表，leave() 弹出。
    // 查找规则：先在本 Scope 内从最内层向最外层逐层查找，找不到再沿 parent 链向外层 Scope 查找
    // （函数作用域的 parent 是模块作用域，模块作用域的 parent 为 nullptr）。
    template<typename T>
    class Scope {
        // 嵌套块的符号表栈，symbolTables[0] 为最外层（函数体/模块顶层）。
        std::vector<std::map<fzlib::String, Symbol<T>>> symbolTables;
        // 当前最内层符号表的下标，始终等于 symbolTables.size() - 1。
        std::size_t cursor = 0;

        // 创建该作用域时的源码位置（仅记录，当前未用于查找）。
        PositionInfo createInfo;

        // 外层作用域（非拥有引用）；本 Scope 查找失败时委托给它。
        Scope* parent = nullptr;

        // 当前最内层符号表；declare 总是写入这一层。
        std::map<fzlib::String, Symbol<T>>& top() {
            return symbolTables[cursor];
        }
    public:
        // 构造时即压入最外层符号表，保证 top() 始终有效。
        Scope(PositionInfo info): createInfo(info) {
            symbolTables.emplace_back();
        }

        // 设置外层作用域；由 Module::buildFunction 等把函数作用域挂到模块作用域下。
        void setParent(Scope* scope) {
            parent = scope;
        }

        // 在最内层声明符号。使用 emplace：同一层已存在同名符号时不会覆盖，
        // 也不报错，保留先声明的那个；不同层同名则形成遮蔽（内层优先）。
        void declare(fzlib::String n, T addr, TypeInfo* type) {
            top().emplace(n, Symbol(n, addr, type));
        }

        // 进入嵌套块：压入一层新的符号表。
        void enter() {
            symbolTables.emplace_back();
            cursor ++;
        }

        // 离开嵌套块：弹出最内层符号表，其中的符号全部失效。
        // 必须与 enter() 配对；不检查是否已在最外层，多调用会破坏表栈。
        void leave() {
            symbolTables.pop_back();
            cursor --;
        }

        // 按名称查找符号：从最内层到最外层逐层查找，命中即返回（实现内层遮蔽外层），
        // 本 Scope 未命中则递归查找 parent，仍未找到返回 nullptr。
        // 返回的指针指向 std::map 节点：同层后续插入不会使其失效，
        // 但对应层被 leave() 弹出后即悬空，调用方不应跨块持有。
        Symbol<T>* lookup(const fzlib::String& name) {
            for (auto it = symbolTables.rbegin(); it != symbolTables.rend(); it ++) {
                auto found = it->find(name);
                if (found != it->end()) {
                    return &(found->second);
                }
            }

            if (parent)
                return parent->lookup(name);
            
            return nullptr;
        }
    };
}

#endif /* !SAKURAE_SCOPE_HPP */
