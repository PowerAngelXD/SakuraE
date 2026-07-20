#include "constant.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/type/type.hpp"
#include "Compiler/IR/value/array.hpp"
#include <cstdint>
#include <limits>

namespace sakuraE::IR {
    static std::map<std::int32_t, Constant> i32Constants;
    static std::map<std::uint32_t, Constant> ui32Constants;
    static std::map<std::int64_t, Constant> i64Constants;
    static std::map<std::uint64_t, Constant> ui64Constants;
    static std::map<float, Constant> f32Constants;
    static std::map<double, Constant> f64Constants;
    static std::map<fzlib::String, Constant> stringConstants;
    static std::map<std::int8_t, Constant> charConstants;
    static std::map<bool, Constant> boolConstants;
    static std::map<TypeInfo*, Constant> typeInfoConstants;
    static std::map<IRArray*, Constant> arrConstants;


    Constant* Constant::get(std::uint32_t val, PositionInfo info) {
        auto it = ui32Constants.find(val);
        if (it != ui32Constants.end()) {
            return &it->second;
        }

        IRType* uint32Ty = IRType::getUInt32Ty();
        auto newEntry = ui32Constants.emplace(val, Constant(uint32Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(std::uint64_t val, PositionInfo info) {
        auto it = ui64Constants.find(val);
        if (it != ui64Constants.end()) {
            return &it->second;
        }

        IRType* uint64Ty = IRType::getUInt64Ty();
        auto newEntry = ui64Constants.emplace(val, Constant(uint64Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(std::int64_t val, PositionInfo info) {
        auto it = i64Constants.find(val);
        if (it != i64Constants.end()) {
            return &it->second;
        }

        IRType* int64Ty = IRType::getInt64Ty();
        auto newEntry = i64Constants.emplace(val, Constant(int64Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(std::int32_t val, PositionInfo info) {
        auto it = i32Constants.find(val);
        if (it != i32Constants.end()) {
            return &it->second;
        }

        IRType* int32Ty = IRType::getInt32Ty();
        auto newEntry = i32Constants.emplace(val, Constant(int32Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(float val, PositionInfo info) {
        auto it = f32Constants.find(val);
        if (it != f32Constants.end()) {
            return &it->second;
        }

        IRType* float32Ty = IRType::getFloat32Ty();
        auto newEntry = f32Constants.emplace(val, Constant(float32Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(double val, PositionInfo info) {
        auto it = f64Constants.find(val);
        if (it != f64Constants.end()) {
            return &it->second;
        }

        IRType* float64Ty = IRType::getFloat64Ty();
        auto newEntry = f64Constants.emplace(val, Constant(float64Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(const fzlib::String& val, PositionInfo info) {
        auto it = stringConstants.find(val);
        if (it != stringConstants.end()) {
            return &it->second;
        }

        IRType* stringTy = IRType::getStringTy();
        auto newEntry = stringConstants.emplace(val, Constant(stringTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(std::int8_t val, PositionInfo info) {
        auto it = charConstants.find(val);
        if (it != charConstants.end()) {
            return &it->second;
        }

        IRType* charTy = IRType::getCharTy();
        auto newEntry = charConstants.emplace(val, Constant(charTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(bool val, PositionInfo info) {
        auto it = boolConstants.find(val);
        if (it != boolConstants.end()) {
            return &it->second;
        }

        IRType* boolTy = IRType::getBoolTy();
        auto newEntry = boolConstants.emplace(val, Constant(boolTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(TypeInfo* val, PositionInfo info)  {
        auto it = typeInfoConstants.find(val);
        if (it != typeInfoConstants.end()) {
            return &it->second;
        }

        IRType* tinfoTy = IRType::getTypeInfoTy();
        auto newEntry = typeInfoConstants.emplace(val, Constant(tinfoTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(IRArray* val, PositionInfo info) {
        auto it = arrConstants.find(val);
        if (it != arrConstants.end()) {
            return &it->second;
        }

        IRType* arrTy = val->getType();
        auto newEntry = arrConstants.emplace(val, Constant(arrTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::getDefault(IRType* ty, PositionInfo info) {
        switch (ty->getIRTypeID())
        {
            case IRTypeID::Integer32TyID:
                return get(std::int32_t{0}, info);
            case IRTypeID::Integer64TyID:
                return get(std::int64_t{0}, info);
            case IRTypeID::BoolTyID:
                return get(false, info);
            case IRTypeID::UInteger32TyID:
                return get(std::uint32_t{0}, info);
            case IRTypeID::UInteger64TyID:
                return get(std::uint64_t{0}, info);
            case IRTypeID::CharTyID:
                return get(std::int8_t{' '}, info);
            case IRTypeID::Float32TyID:
                return get((float)0.0, info);
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unsupported default initialization for type: " + ty->toString(),
                                info);
        }
    }

    Constant* Constant::getFromToken(const Token& tok) {
        switch (tok.type) {
            case TokenType::BOOL_CONST:
                return Constant::get(tok.content == "true", tok.info);
            case TokenType::INT_N: {
                std::string str = tok.content.c_str();
                if (str.empty()) return Constant::get(0, tok.info);

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
                        return Constant::get(sign == 1 ? val : static_cast<std::uint64_t>(-static_cast<std::int64_t>(val)), tok.info);
                    } 
                    else if (suffix == "L") {
                        std::int64_t val = std::stoll(val_part, nullptr, base);
                        return Constant::get(static_cast<std::int64_t>(sign * val), tok.info);
                    } 
                    else if (suffix == "U") {
                        std::uint32_t val = static_cast<std::uint32_t>(std::stoul(val_part, nullptr, base));
                        return Constant::get(sign == 1 ? val : static_cast<std::uint32_t>(-static_cast<std::int32_t>(val)), tok.info);
                    } 
                    else {
                        std::int64_t val = std::stoll(val_part, nullptr, base);
                        val *= sign;
                        if (val > std::numeric_limits<std::int32_t>::max() ||
                            val < std::numeric_limits<std::int32_t>::min()) {
                            return Constant::get(static_cast<std::int64_t>(val), tok.info);
                        }
                        return Constant::get(static_cast<std::int32_t>(val), tok.info);
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
                    return Constant::get(std::stod(str), tok.info);
                }
                return Constant::get(std::stof(tok.content.c_str()), tok.info);
            }
            case TokenType::STRING:
                return Constant::get(fzlib::String(tok.content.c_str()), tok.info);
            case TokenType::CHAR:
                return Constant::get(static_cast<std::int8_t>(tok.content[0]), tok.info);
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
        i32Constants.clear();
        ui32Constants.clear();
        i64Constants.clear();
        ui64Constants.clear();
        f32Constants.clear();
        f64Constants.clear();
        stringConstants.clear();
        charConstants.clear();
        boolConstants.clear();
        typeInfoConstants.clear();
        arrConstants.clear();
    }

    llvm::Type* Constant::toLLVMType(llvm::LLVMContext& ctx) {
        if (type) {
            return type->toLLVMType(ctx);
        }
        return nullptr;
    }
}
