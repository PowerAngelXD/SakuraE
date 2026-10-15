#include "constant.hpp"
#include <cstdint>
#include <limits>

namespace sakuraE::IR {
    std::map<std::pair<TypeInfo*, Constant::Content>, Constant> Constant::constants;

    Constant* Constant::intern(TypeInfo* type, Content value, PositionInfo info) {
        if (!type) throw std::invalid_argument("Constant requires a semantic type");
        auto key = std::make_pair(type, value);
        const auto it = constants.find(key);
        if (it != constants.end()) return &it->second;
        return &constants.emplace(std::move(key), Constant(type, std::move(value), std::move(info))).first->second;
    }

    Constant* Constant::getNullHandle(TypeInfo* type, PositionInfo info) {
        if (!type || !type->isNullable() || (!type->isStruct() && !type->isArray())) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "null requires a nullable struct or array type.", info);
        }
        return intern(type, std::monostate{}, std::move(info));
    }

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

    Constant* Constant::get(TypeInfo* value, PositionInfo info) {
        if (!value) throw std::invalid_argument("Type descriptor requires a semantic type");
        return intern(value->getTypeContext().makeBasicTypeID(TypeID::TypeDescriptor), value, std::move(info));
    }

    Constant* Constant::get(IRArray* value, PositionInfo info) {
        if (!value) throw std::invalid_argument("Array constant requires an array");
        return intern(value->getSemanticType(), value, std::move(info));
    }

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
        switch (tok.type) {
            case TokenType::BOOL_CONST:
                return Constant::get(tok.content == "true", types, tok.info);
            case TokenType::INT_N: {
                std::string str = tok.content.c_str();
                if (str.empty()) return Constant::get(0, types, tok.info);

                std::int64_t sign = 1;
                size_t pos = 0;
                if (str[pos] == '-') {
                    sign = -1;
                    pos++;
                }

                int base = 10;
                if (pos + 1 < str.size() && str[pos] == '0') {
                    char p = std::tolower(str[pos + 1]);
                    if (p == 'x') { base = 16; pos += 2; }
                    else if (p == 'b') { base = 2; pos += 2; }
                    else if (p == 'o') { base = 8; pos += 2; }
                }

                std::string suffix = "";
                size_t end_pos = str.size();
                auto isEndsWith = [&](const std::string& s) {
                    if (str.size() - pos < s.size()) return false;
                    std::string sub = str.substr(str.size() - s.size());
                    for (char &c : sub) c = std::toupper(c);
                    return sub == s;
                };

                if (isEndsWith("UL") || isEndsWith("LU")) { suffix = "UL"; end_pos -= 2; }
                else if (isEndsWith("U")) { suffix = "U"; end_pos -= 1; }
                else if (isEndsWith("L")) { suffix = "L"; end_pos -= 1; }

                std::string val_part = str.substr(pos, end_pos - pos);
                try {
                    if (suffix == "UL") {
                        std::uint64_t val = std::stoull(val_part, nullptr, base);
                        return Constant::get(sign == 1 ? val : static_cast<std::uint64_t>(-static_cast<std::int64_t>(val)), types, tok.info);
                    } 
                    else if (suffix == "L") {
                        std::int64_t val = std::stoll(val_part, nullptr, base);
                        return Constant::get(static_cast<std::int64_t>(sign * val), types, tok.info);
                    } 
                    else if (suffix == "U") {
                        std::uint32_t val = static_cast<std::uint32_t>(std::stoul(val_part, nullptr, base));
                        return Constant::get(sign == 1 ? val : static_cast<std::uint32_t>(-static_cast<std::int32_t>(val)), types, tok.info);
                    } 
                    else {
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
                break;
            }
            case TokenType::FLOAT_N: {
                std::string str = tok.content.c_str();
                if (!str.empty() && (str.back() == 'f')) {
                    str.pop_back();
                    return Constant::get(std::stod(str), types, tok.info);
                }
                return Constant::get(std::stof(tok.content.c_str()), types, tok.info);
            }
            case TokenType::STRING:
                return Constant::get(fzlib::String(tok.content.c_str()), types, tok.info);
            case TokenType::CHAR:
                return Constant::get(static_cast<std::int8_t>(tok.content[0]), types, tok.info);
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot create constant from non-constant token",
                                tok.info);
        }

        throw SakuraError(OccurredTerm::IR_GENERATING,
                          "Cannot create constant from token",
                          tok.info);
    }

    void Constant::clearAll() {
        constants.clear();
    }
}
