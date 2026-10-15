#ifndef SAKURAE_CONSTANT_HPP
#define SAKURAE_CONSTANT_HPP

#include <type_traits>
#include <variant>
#include <map>
#include <cstdint>

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Semantic/value/value.hpp"
#include "array.hpp"
#include "Compiler/Frontend/lexer.h"
#include "Compiler/IR/Semantic/type/type_info.hpp"

namespace sakuraE::IR {
    class Type;

    class Constant : public IRValue {
    private:
        using Content = std::variant<
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
        >;
        Content content;
        static std::map<std::pair<TypeInfo*, Content>, Constant> constants;
        PositionInfo createInfo;

        Constant(TypeInfo* type, Content value, PositionInfo info)
            : IRValue(type), content(std::move(value)), createInfo(std::move(info)) {}
        static Constant* intern(TypeInfo* type, Content value, PositionInfo info);

    public:
        static Constant* get(std::uint32_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::uint64_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int64_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int32_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(float val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int8_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* getNullHandle(TypeInfo* type, PositionInfo info);
        static Constant* getDefault(TypeInfo* type, PositionInfo info);
        static Constant* getFromToken(const Token& tok, TypeInfoContext& types);
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

        bool isNullHandle() const {
            return std::holds_alternative<std::monostate>(content);
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

    };

}

#endif /* !SAKURAE_CONSTANT_HPP */
