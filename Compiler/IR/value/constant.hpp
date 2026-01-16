#ifndef SAKURAE_CONSTANT_HPP
#define SAKURAE_CONSTANT_HPP

#include <variant>
#include <map>

#include <llvm/IR/Type.h>

#include "value.hpp"
#include "Compiler/Frontend/lexer.h"
#include "Compiler/IR/type/type_info.hpp"

namespace sakuraE::IR {
    class Type;

    class Constant : public IRValue {
    private:
        std::variant<std::monostate, int, double, fzlib::String, char, bool, TypeInfo*> content;
        PositionInfo createInfo;

        Constant(IRType* ty, int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, const fzlib::String& val, PositionInfo info)
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}

    public:
        static Constant* get(int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getFromToken(const Token& tok);

        template<typename T>
        const T& getIRValue() const {
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
