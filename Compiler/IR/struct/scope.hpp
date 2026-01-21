#ifndef SAKURAE_SCOPE_HPP
#define SAKURAE_SCOPE_HPP

#include <stack>
#include <utility>

#include "Compiler/IR/value/constant.hpp"

namespace sakuraE::IR {
    template<typename T>
    requires std::is_pointer_v<T>
    struct Symbol: public IRValue {
        fzlib::String name = "DefaultSymbol";
        T address = nullptr;

        Symbol(fzlib::String n, T addr, IRType* t): IRValue(t), name(n), address(addr) {}
    };

    template<typename T>
    class Scope {
        std::vector<std::map<fzlib::String, Symbol<T>>> symbolTables;
        std::size_t cursor = 0;

        PositionInfo createInfo;

        Scope* parent = nullptr;

        std::map<fzlib::String, Symbol<T>>& top() {
            return symbolTables[cursor];
        }
    public:
        Scope(PositionInfo info): createInfo(info) {
            symbolTables.emplace_back();
        }

        void setParent(Scope* scope) {
            parent = scope;
        }

        void declare(fzlib::String n, T addr, IRType* t) {
            top().emplace(n, Symbol(n, addr, t));
        }

        void enter() {
            symbolTables.emplace_back();
            cursor ++;
        }

        void leave() {
            symbolTables.pop_back();
            cursor --;
        }

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

#endif // !SAKURAE_SCOPE_HPP
