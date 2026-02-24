#ifndef SAKURAE_CONSTANT_HPP
#define SAKURAE_CONSTANT_HPP

#include <type_traits>
#include <variant>
#include <map>

#include <llvm/IR/Type.h>

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/value/value.hpp"
#include "array.hpp"
#include "Compiler/Frontend/lexer.h"
#include "Compiler/IR/type/type_info.hpp"

namespace sakuraE::IR {
    class Type;

    class Constant : public IRValue {
    private:
        std::variant<
            std::monostate,
            int,
            long long,
            unsigned int,
            unsigned long long,
            double,
            float,
            fzlib::String,
            char,
            bool,
            TypeInfo*,
            IRArray*
        > content;
        PositionInfo createInfo;

        Constant(IRType* ty, int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, unsigned int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, unsigned long long val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, long long val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
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
        Constant(IRType* ty, IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}

    public:
        static Constant* get(unsigned int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(unsigned long long val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(long long val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(int val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(char val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getDefault(IRType* ty, PositionInfo info);
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
                }
                else if constexpr (std::is_same_v<T, int>) {
                    return std::to_string(arg);
                }
                else if constexpr (std::is_same_v<T, float>) {
                    return std::to_string(arg);
                }
                else if constexpr (std::is_same_v<T, fzlib::String>) {
                    return arg;
                }
                else if constexpr (std::is_same_v<T, char>) {
                    char buf[4] = {'\'', arg, '\'', '\0'};
                    return fzlib::String(buf);
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    return arg ? "true" : "false";
                }
                else if constexpr (std::is_same_v<T, TypeInfo*>) {
                    return "<TypeInfo>";
                }
                else if constexpr (std::is_same_v<T, IRValue*>) {
                    return arg ? arg->getName() : "null";
                }
                else if constexpr (std::is_same_v<T, IRArray*>) {
                    fzlib::String result = "[";
                    for (std::size_t i = 0; i < arg->getSize(); i ++) {
                        if (i == arg->getSize() - 1)
                            result += arg->getArray()[i]->getName() + "]";
                        else
                            result += arg->getArray()[i]->getName() + ", ";
                    }
                    return result;
                }
                return "unknown";
            }, content);
        }

        llvm::Type* toLLVMType(llvm::LLVMContext& ctx);
    };

}

#endif // !SAKURAE_CONSTANT_HPP
