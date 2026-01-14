#ifndef SAKURAE_CONSTANT_HPP
#define SAKURAE_CONSTANT_HPP

#include <variant>

#include <llvm/IR/Type.h>

#include "value.hpp"
#include "Compiler/Frontend/lexer.h"

namespace sakuraE::IR {
    class Type;

    class Constant : public Value {
    private:
        std::variant<std::monostate, int, double, fzlib::String, char, bool> content;
        PositionInfo createInfo;

        Constant(Type* type, int val, PositionInfo info)
            : Value(type), content(val), createInfo(info) {}
        Constant(Type* type, double val, PositionInfo info)
            : Value(type), content(val), createInfo(info) {}
        Constant(Type* type, const fzlib::String& val, PositionInfo info)
            : Value(type), content(val), createInfo(info) {}
        Constant(Type* type, char val, PositionInfo info)
            : Value(type), content(val), createInfo(info) {}
        Constant(Type* type, bool val, PositionInfo info)
            : Value(type), content(val), createInfo(info) {}
        Constant(Type* type, PositionInfo info)
            : Value(type), content(), createInfo(info) {}

        Constant(Type* type, int val)
            : Value(type), content(val) {}
        Constant(Type* type, double val)
            : Value(type), content(val) {}
        Constant(Type* type, const fzlib::String& val)
            : Value(type), content(val) {}
        Constant(Type* type, char val)
            : Value(type), content(val) {}
        Constant(Type* type, bool val)
            : Value(type), content(val) {}
        Constant(Type* type)
            : Value(type), content() {}

    public:
        static Constant* get(Type* t,  PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getFromToken(const Token& tok);

        template<typename T>
        const T& getValue() const {
            if (std::holds_alternative<T>(content)) {
                return std::get<T>(content);
            }
            throw SakuraError(OccurredTerm::IR_GENERATING, 
                                "Invalid type requested for constant value",
                                createInfo);
        }

        const PositionInfo& getInfo() const {
            return createInfo;
        }

        llvm::Type* toLLVMType(llvm::LLVMContext& ctx);
    };

}

#endif // !SAKURAE_CONSTANT_HPP
