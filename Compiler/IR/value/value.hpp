#ifndef SAKURAE_VALUE_HPP
#define SAKURAE_VALUE_HPP

#include "Compiler/IR/type/type.hpp"
#include "Compiler/Frontend/lexer.h"
#include <string>

namespace sakuraE::IR {
    class Value {
    protected:
        Type* type;
        fzlib::String name;
    public:
        explicit Value(Type* ty) : type(ty) {}
        virtual ~Value() = default;

        Type* getType() const { return type; }

        void setName(const fzlib::String& n) {
            name = n;
        }

        const fzlib::String& getName() {
            return name;
        }
    };

}
#endif // !SAKURAE_VALUE_HPP
