#ifndef SAKURAE_SCOPE_HPP
#define SAKURAE_SCOPE_HPP

#include <stack>
#include <utility>

#include "Compiler/IR/value/constant.hpp"

namespace sakuraE::IR {
    struct Symbol: public Value {
        fzlib::String name = "DefaultSymbol";
        Value* address = nullptr;

        Symbol(): Value(Type::getVoidTy()) {};
        Symbol(fzlib::String n, Value* addr, Type* t): Value(t), name(n), address(addr) {}
    };

    class Scope {
        std::vector<std::map<fzlib::String, Symbol>> symbolTables;
        std::size_t cursor = 0;

        PositionInfo createInfo;

        Scope* parent = nullptr;

        std::map<fzlib::String, Symbol>& top() {
            return symbolTables[cursor];
        }
    public:
        Scope(PositionInfo info): createInfo(info) {
            symbolTables.emplace_back();
        }

        void setParent(Scope* scope) {
            parent = scope;
        }

        void declare(fzlib::String n, Value* addr, Type* t) {
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

        Symbol* lookup(const fzlib::String& name) {
            for (auto it = symbolTables.rbegin(); it != symbolTables.rend(); it --) {
                if (it->find(name) != it->end()) {
                    return &(*it)[name];
                }
            }

            if (parent)
                return parent->lookup(name);
            
            return nullptr;
        }
    };
}

#endif // !SAKURAE_SCOPE_HPP
