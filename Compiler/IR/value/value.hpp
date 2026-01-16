#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/type/type.hpp"
#include "Compiler/Frontend/lexer.h"
#include <string>

namespace sakuraE::IR {
    class IRValue {
    protected:
        IRType* type;
        fzlib::String name;
    public:
        explicit IRValue(IRType* ty) : type(ty) {}
        virtual ~IRValue() = default;

        IRType* getType() const { return type; }

        void setName(const fzlib::String& n) {
            name = n;
        }

        const fzlib::String& getName() {
            return name;
        }
    };

}
#endif // !SAKURAE_VALUE_HPP
