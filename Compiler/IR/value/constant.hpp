#ifndef SAKURAE_CONSTANT_HPP
#define SAKURAE_CONSTANT_HPP

#include <type_traits>
#include <variant>
#include <map>
#include <cstdint>

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
            std::int32_t,
            std::int64_t,
            std::uint32_t,
            std::uint64_t,
            double,
            float,
            fzlib::String,
            std::int8_t,
            bool,
            TypeInfo*,
            IRArray*
        > content;
        PositionInfo createInfo;

        Constant(IRType* ty, std::int32_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, std::uint32_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, std::uint64_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, std::int64_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, const fzlib::String& val, PositionInfo info)
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, std::int8_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}
        Constant(IRType* ty, IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"})
            : IRValue(ty), content(val), createInfo(info) {}

    public:
        static Constant* get(std::uint32_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::uint64_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int64_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int32_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(float val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int8_t val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getDefault(IRType* ty, PositionInfo info);
        static Constant* getFromToken(const Token& tok);
        static void clearAll();

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
                else if constexpr (std::is_same_v<T, std::int32_t> ||
                                   std::is_same_v<T, std::int64_t> ||
                                   std::is_same_v<T, std::uint32_t> ||
                                   std::is_same_v<T, std::uint64_t>) {
                    return std::to_string(arg);
                }
                else if constexpr (std::is_same_v<T, float>) {
                    return std::to_string(arg);
                }
                else if constexpr (std::is_same_v<T, fzlib::String>) {
                    return arg;
                }
                else if constexpr (std::is_same_v<T, std::int8_t>) {
                    char buf[4] = {'\'', static_cast<char>(arg), '\'', '\0'};
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

#endif /* !SAKURAE_CONSTANT_HPP */
