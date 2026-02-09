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
        std::variant<
            std::monostate, 
            int, 
            float, 
            fzlib::String, 
            char, 
            bool, 
            TypeInfo*,
            IRValue*
        > content;
        PositionInfo createInfo;

        Constant(IRType* ty, int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, const fzlib::String& val, PositionInfo info)
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, IRValue* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}

    public:
        static Constant* get(int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(IRValue* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getFromToken(const Token& tok);

        template<typename T>
        const T& getContentValue() const {
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

        fzlib::String toString() {
            return std::visit([](auto&& arg) -> fzlib::String {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return "null";
                } else if constexpr (std::is_same_v<T, int>) {
                    return std::to_string(arg);
                } else if constexpr (std::is_same_v<T, float>) {
                    return std::to_string(arg);
                } else if constexpr (std::is_same_v<T, fzlib::String>) {
                    return arg;
                } else if constexpr (std::is_same_v<T, char>) {
                    char buf[4] = {'\'', arg, '\'', '\0'};
                    return fzlib::String(buf);
                } else if constexpr (std::is_same_v<T, bool>) {
                    return arg ? "true" : "false";
                } else if constexpr (std::is_same_v<T, TypeInfo*>) {
                    return "<TypeInfo>";
                } else if constexpr (std::is_same_v<T, IRValue*>) {
                    return arg ? arg->getName() : "null";
                }
                return "unknown";
            }, content);
        }

        llvm::Type* toLLVMType(llvm::LLVMContext& ctx);
    };

}

#endif // !SAKURAE_CONSTANT_HPP
