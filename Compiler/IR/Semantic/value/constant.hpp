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

    // 编译期常量值。保存语义类型(TypeInfo)与一个 variant 负载，不保存 storage 类型。
    // 所有 Constant 都按 (语义类型, 负载) 驻留在全局静态表 constants 中：
    // 相同类型与相同值返回同一个 Constant*，不同 IRContext 的同值常量因 TypeInfo* 不同而互不复用。
    // 构造函数为 private，只能通过下面的静态工厂获取。
    class Constant : public IRValue {
    private:
        // 常量负载：
        // - std::monostate：null 句柄（仅用于 nullable 结构体/数组，见 getNullHandle）；
        // - 有/无符号 32/64 位整数、double/float：数值字面量；
        // - fzlib::String：字符串字面量文本；
        // - std::int8_t：字符字面量；
        // - bool：布尔字面量；
        // - TypeInfo*：类型描述(TypeDescriptor)常量的负载，即被描述的语义类型（非拥有）；
        // - IRArray*：数组字面量（非拥有，归 IRArray::arrPool 所有）。
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
        // 驻留表：键为 (语义类型, 负载)。值按值存储于 std::map，节点地址稳定，
        // 因此可以安全地向外返回 &it->second。PositionInfo 不参与键，首次创建时的位置被保留。
        static std::map<std::pair<TypeInfo*, Content>, Constant> constants;
        PositionInfo createInfo;   // 首次创建时的源码位置，用于报错

        Constant(TypeInfo* type, Content value, PositionInfo info)
            : IRValue(type), content(std::move(value)), createInfo(std::move(info)) {}
        // 驻留入口：type 不能为空；已存在相同键则直接返回（忽略本次 info），否则插入新常量。
        static Constant* intern(TypeInfo* type, Content value, PositionInfo info);

    public:
        // 数值/字符串/字符/布尔工厂：显式接收 TypeInfoContext，以确定常量属于哪个 IRContext，
        // 并把 C++ 类型映射到对应的基础语义类型（uint32->UInt32、int8->Char、fzlib::String->String 等）。
        // info 默认为占位位置，表示非来自源码 token 的常量。
        static Constant* get(std::uint32_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::uint64_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int64_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int32_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(float val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(double val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(const fzlib::String& val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(std::int8_t val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        static Constant* get(bool val, TypeInfoContext& types, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        // 类型描述常量：负载是被描述的 TypeInfo*，常量自身的语义类型为 TypeDescriptor，
        // 使用负载类型所属的语义 context，因此无需传入 TypeInfoContext。
        static Constant* get(TypeInfo* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        // 数组常量：语义类型取自 IRArray::getSemanticType()（元素所属 context 驻留的数组类型）。
        static Constant* get(IRArray* val, PositionInfo info = {0, 0, "NormalConstant, Not from token"});
        // null 常量：type 必须是 nullable 的结构体或数组类型，否则报 IR 生成错误；
        // 不同 nullable 类型得到不同的 null 常量。
        static Constant* getNullHandle(TypeInfo* type, PositionInfo info);
        // 变量未显式初始化时使用的默认值：数值为 0、bool 为 false、char 为空格 ' '。
        // 仅支持整数/浮点/bool/char 基础类型，其余（string、结构体、数组等）报 IR 生成错误。
        static Constant* getDefault(TypeInfo* type, PositionInfo info);
        // 由字面量 token 构造常量，语义类型由 token 种类与后缀决定（不从 storage 类型反推），
        // 详细规则见实现。非常量 token 报 IR 生成错误。
        static Constant* getFromToken(const Token& tok, TypeInfoContext& types);
        // 清空驻留表；之后所有已返回的 Constant* 均悬空。不释放 IRArray（见 IRArray::clearArrayPool）。
        static void clearAll();

        // 按 C++ 类型读取负载；请求的类型与实际负载不符时报 IR 生成错误。
        template<typename T>
        const T& getContentValue() const {
            if (std::holds_alternative<T>(content)) {
                return std::get<T>(content);
            }
            throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Invalid type requested for constant value",
                                createInfo);
        }

        // 常量创建位置。
        const PositionInfo& getInfo() const {
            return createInfo;
        }

        // 是否为 null 句柄（负载为 monostate）。
        bool isNullHandle() const {
            return std::holds_alternative<std::monostate>(content);
        }

        // 取 TypeInfo*/IRArray* 负载；负载不是对应类型时返回 nullptr，而不是抛错。
        TypeInfo* getTypePayload() const {
            const auto* value = std::get_if<TypeInfo*>(&content);
            return value ? *value : nullptr;
        }

        const IRArray* getArrayPayload() const {
            const auto* value = std::get_if<IRArray*>(&content);
            return value ? *value : nullptr;
        }

        // 生成常量的文本表示，用于 IR 打印与调试。
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
                    // 字符常量打印为 'c' 形式。
                    char buf[4] = {'\'', static_cast<char>(arg), '\'', '\0'};
                    return fzlib::String(buf);
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    return arg ? "true" : "false";
                }
                else if constexpr (std::is_same_v<T, TypeInfo*>) {
                    return "<TypeInfo>";
                }
                // 注意：Content 中不包含 IRValue*，此分支在当前 variant 下不会被实例化。
                else if constexpr (std::is_same_v<T, IRValue*>) {
                    return arg ? arg->getName() : "null";
                }
                // 打印为 [e0, e1, ...]，元素使用其名称；IRArray 保证非空。
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
                // double 负载没有专门分支，当前会落到这里返回 "unknown"。
                return "unknown";
            }, content);
        }

    };

}

#endif /* !SAKURAE_CONSTANT_HPP */
