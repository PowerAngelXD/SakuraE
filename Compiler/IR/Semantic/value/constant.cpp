#include "constant.hpp"
#include <cstdint>
#include <limits>

namespace sakuraE::IR {
    // 全局常量驻留表的定义（跨所有 IRContext 共享，靠 TypeInfo* 区分归属）。
    std::map<std::pair<TypeInfo*, Constant::Content>, Constant> Constant::constants;

    // 所有工厂的公共驻留入口。缺少语义类型属于编译器内部错误（std::invalid_argument）。
    // 命中时直接返回已有常量，新传入的 info 被丢弃；std::map 节点地址稳定，返回的指针在 clearAll 前一直有效。
    Constant* Constant::intern(TypeInfo* type, Content value, PositionInfo info) {
        if (!type) throw std::invalid_argument("Constant requires a semantic type");
        auto key = std::make_pair(type, value);
        const auto it = constants.find(key);
        if (it != constants.end()) return &it->second;
        return &constants.emplace(std::move(key), Constant(type, std::move(value), std::move(info))).first->second;
    }

    // null 只允许出现在 nullable 结构体/数组类型上（与 TypeInfoContext::wrapTypeAsNullable 的限制一致），
    // 这里再次校验，防止给非 nullable 或其他种类的类型构造 null。负载为 std::monostate。
    Constant* Constant::getNullHandle(TypeInfo* type, PositionInfo info) {
        if (!type || !type->isNullable() || (!type->isStruct() && !type->isArray())) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "null requires a nullable struct or array type.", info);
        }
        return intern(type, std::monostate{}, std::move(info));
    }

    // 以下基础类型工厂：从给定 TypeInfoContext 取对应的基础语义类型后驻留。
    Constant* Constant::get(std::uint32_t value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::UInt32), value, std::move(info));
    }

    Constant* Constant::get(std::uint64_t value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::UInt64), value, std::move(info));
    }

    Constant* Constant::get(std::int64_t value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Int64), value, std::move(info));
    }

    Constant* Constant::get(std::int32_t value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Int32), value, std::move(info));
    }

    Constant* Constant::get(float value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Float32), value, std::move(info));
    }

    Constant* Constant::get(double value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Float64), value, std::move(info));
    }

    Constant* Constant::get(const fzlib::String& value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::String), value, std::move(info));
    }

    Constant* Constant::get(std::int8_t value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Char), value, std::move(info));
    }

    Constant* Constant::get(bool value, TypeInfoContext& types, PositionInfo info) {
        return intern(types.makeBasicTypeID(TypeID::Bool), value, std::move(info));
    }

    // 类型描述常量：语义类型为负载所属 context 的 TypeDescriptor 基础类型，负载即被描述的类型。
    Constant* Constant::get(TypeInfo* value, PositionInfo info) {
        if (!value) throw std::invalid_argument("Type descriptor requires a semantic type");
        return intern(value->getTypeContext().makeBasicTypeID(TypeID::TypeDescriptor), value, std::move(info));
    }

    // 数组常量：语义类型由 IRArray 根据首元素类型与长度推导（同一 context 驻留）。
    Constant* Constant::get(IRArray* value, PositionInfo info) {
        if (!value) throw std::invalid_argument("Array constant requires an array");
        return intern(value->getSemanticType(), value, std::move(info));
    }

    // 默认初始化值。按 TypeID 分派，直接使用传入的 type 作为常量的语义类型。
    // 注意 Char 的默认值是空格 ' ' 而不是 '\0'。String、TypeDescriptor 及复合类型不支持默认值，
    // 调用方（createAlloca）会预先排除 String/TypeDescriptor，其余情况报 IR 生成错误。
    Constant* Constant::getDefault(TypeInfo* type, PositionInfo info) {
        if (!type) throw std::invalid_argument("Default constant requires a semantic type");
        switch (type->getTypeID()) {
            case Int32: return intern(type, std::int32_t{0}, info);
            case Int64: return intern(type, std::int64_t{0}, info);
            case UInt32: return intern(type, std::uint32_t{0}, info);
            case UInt64: return intern(type, std::uint64_t{0}, info);
            case Bool: return intern(type, false, info);
            case Char: return intern(type, std::int8_t{' '}, info);
            case Float32: return intern(type, 0.0f, info);
            case Float64: return intern(type, 0.0, info);
            default: throw SakuraError(OccurredTerm::IR_GENERATING, "Unsupported semantic default initialization.", info);
        }
    }

    Constant* Constant::getFromToken(const Token& tok, TypeInfoContext& types) {
        // 语义类型完全由 token 种类和字面量后缀决定。
        switch (tok.type) {
            case TokenType::BOOL_CONST:
                return Constant::get(tok.content == "true", types, tok.info);
            // 整数字面量规则：
            // - 可选前导 '-'，可选进制前缀 0x/0b/0o（不区分大小写）；
            // - 后缀（不区分大小写）：UL/LU -> UInt64，U -> UInt32，L -> Int64；
            // - 无后缀：按 int64 解析，落在 int32 范围内为 Int32，否则自动提升为 Int64。
            // 解析失败或超出目标类型可表示范围（stoll/stoull/stoul 抛异常）时统一报 IR 生成错误。
            case TokenType::INT_N: {
                std::string str = tok.content.c_str();
                // 空内容视为 0（防御性处理）。
                if (str.empty()) return Constant::get(0, types, tok.info);

                std::int64_t sign = 1;
                size_t pos = 0;
                if (str[pos] == '-') {
                    sign = -1;
                    pos++;
                }

                // 仅当 '0' 之后还有字符时才识别进制前缀，单独的 "0" 按十进制处理。
                int base = 10;
                if (pos + 1 < str.size() && str[pos] == '0') {
                    char p = std::tolower(str[pos + 1]);
                    if (p == 'x') { base = 16; pos += 2; }
                    else if (p == 'b') { base = 2; pos += 2; }
                    else if (p == 'o') { base = 8; pos += 2; }
                }

                // 后缀检测：只在去掉符号和进制前缀后的剩余部分足够长时才比较末尾字符。
                std::string suffix = "";
                size_t end_pos = str.size();
                auto isEndsWith = [&](const std::string& s) {
                    if (str.size() - pos < s.size()) return false;
                    std::string sub = str.substr(str.size() - s.size());
                    for (char &c : sub) c = std::toupper(c);
                    return sub == s;
                };

                // 先检测双字符后缀，避免 "UL" 被误判成单个 "L"。
                if (isEndsWith("UL") || isEndsWith("LU")) { suffix = "UL"; end_pos -= 2; }
                else if (isEndsWith("U")) { suffix = "U"; end_pos -= 1; }
                else if (isEndsWith("L")) { suffix = "L"; end_pos -= 1; }

                std::string val_part = str.substr(pos, end_pos - pos);
                try {
                    // 无符号后缀带负号时，按补码取反后截断，结果等价于对 2^N 取模。
                    if (suffix == "UL") {
                        std::uint64_t val = std::stoull(val_part, nullptr, base);
                        return Constant::get(sign == 1 ? val : static_cast<std::uint64_t>(-static_cast<std::int64_t>(val)), types, tok.info);
                    } 
                    // L 后缀：sign * val 按 int64 计算，-9223372036854775808L 会因 stoll 溢出而报错。
                    else if (suffix == "L") {
                        std::int64_t val = std::stoll(val_part, nullptr, base);
                        return Constant::get(static_cast<std::int64_t>(sign * val), types, tok.info);
                    } 
                    // U 后缀：stoul 结果截断为 32 位，超过 32 位的部分被静默丢弃（不报错）。
                    else if (suffix == "U") {
                        std::uint32_t val = static_cast<std::uint32_t>(std::stoul(val_part, nullptr, base));
                        return Constant::get(sign == 1 ? val : static_cast<std::uint32_t>(-static_cast<std::int32_t>(val)), types, tok.info);
                    } 
                    else {
                        // 无后缀：先应用符号再判断 int32 范围，从而 -2147483648 仍是 Int32。
                        std::int64_t val = std::stoll(val_part, nullptr, base);
                        val *= sign;
                        if (val > std::numeric_limits<std::int32_t>::max() ||
                            val < std::numeric_limits<std::int32_t>::min()) {
                            return Constant::get(static_cast<std::int64_t>(val), types, tok.info);
                        }
                        return Constant::get(static_cast<std::int32_t>(val), types, tok.info);
                    }
                } 
                catch (...) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                        "Literal value out of range or invalid: " + tok.content,
                                        tok.info);
                }
                // 不可达：上面每个分支都已返回或抛出。
                break;
            }
            // 浮点字面量：带 'f' 后缀时去掉后缀并按 double 解析得到 Float64；
            // 无后缀时按 float 解析得到 Float32（注意：与 C/C++ 中 f 后缀表示 float 的约定相反）。
            // 无效内容时 std::stod/std::stof 抛出的 std::invalid_argument/out_of_range 不会被转成 SakuraError。
            case TokenType::FLOAT_N: {
                std::string str = tok.content.c_str();
                if (!str.empty() && (str.back() == 'f')) {
                    str.pop_back();
                    return Constant::get(std::stod(str), types, tok.info);
                }
                return Constant::get(std::stof(tok.content.c_str()), types, tok.info);
            }
            // 字符串字面量：负载为 token 文本（引号已由词法分析去除）。
            case TokenType::STRING:
                return Constant::get(fzlib::String(tok.content.c_str()), types, tok.info);
            // 字符字面量：取文本首字节作为 int8 负载，语义类型为 Char。
            case TokenType::CHAR:
                return Constant::get(static_cast<std::int8_t>(tok.content[0]), types, tok.info);
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot create constant from non-constant token",
                                tok.info);
        }

        // 不可达的兜底（仅为满足编译器的返回路径检查）。
        throw SakuraError(OccurredTerm::IR_GENERATING,
                          "Cannot create constant from token",
                          tok.info);
    }

    // 清空驻留表，释放全部 Constant；IRArray 负载需另行通过 IRArray::clearArrayPool 释放。
    void Constant::clearAll() {
        constants.clear();
    }
}
