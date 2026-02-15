#include "constant.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/type/type.hpp"

namespace sakuraE::IR {
    static std::map<int, Constant> i32Constants;
    static std::map<unsigned int, Constant> ui32Constants;
    static std::map<long long, Constant> i64Constants;
    static std::map<unsigned long long, Constant> ui64Constants;
    static std::map<float, Constant> f32Constants;
    static std::map<double, Constant> f64Constants;
    static std::map<fzlib::String, Constant> stringConstants;
    static std::map<char, Constant> charConstants;
    static std::map<bool, Constant> boolConstants;
    static std::map<TypeInfo*, Constant> typeInfoConstants;
    static std::map<IRValue*, Constant> ptrConstants;


    Constant* Constant::get(unsigned int val, PositionInfo info) {
        auto it = ui32Constants.find(val);
        if (it != ui32Constants.end()) {
            return &it->second;
        }

        IRType* uint32Ty = IRType::getUInt32Ty();
        auto newEntry = ui32Constants.emplace(val, Constant(uint32Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(unsigned long long val, PositionInfo info) {
        auto it = ui64Constants.find(val);
        if (it != ui64Constants.end()) {
            return &it->second;
        }

        IRType* uint64Ty = IRType::getUInt64Ty();
        auto newEntry = ui64Constants.emplace(val, Constant(uint64Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(long long val, PositionInfo info) {
        auto it = i64Constants.find(val);
        if (it != i64Constants.end()) {
            return &it->second;
        }

        IRType* int64Ty = IRType::getInt64Ty();
        auto newEntry = i64Constants.emplace(val, Constant(int64Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(int val, PositionInfo info) {
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

        IRType* float64Ty = IRType::getFloat32Ty();
        auto newEntry = f64Constants.emplace(val, Constant(float64Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(const fzlib::String& val, PositionInfo info) {
        auto it = stringConstants.find(val);
        if (it != stringConstants.end()) {
            return &it->second;
        }

        IRType* charTy = IRType::getCharTy();
        IRType* stringTy = IRType::getPointerTo(charTy);
        auto newEntry = stringConstants.emplace(val, Constant(stringTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(char val, PositionInfo info) {
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

    Constant* Constant::get(IRValue* val, PositionInfo info) {
        auto it = ptrConstants.find(val);
        if (it != ptrConstants.end()) {
            return &it->second;
        }

        IRType* ptrTy = IRType::getPointerTo(val->getType());
        auto newEntry = ptrConstants.emplace(val, Constant(ptrTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::getDefault(IRType* ty, PositionInfo info) {
        switch (ty->getIRTypeID())
        {
            case IRTypeID::Integer32TyID:
                return get((int)0, info);
            case IRTypeID::Integer64TyID:
                return get((int)0, info);
            case IRTypeID::BoolTyID:
                return get(false, info);
            case IRTypeID::UInteger32TyID:
                return get((unsigned int)0, info);
            case IRTypeID::UInteger64TyID:
                return get((unsigned long long)0, info);
            case IRTypeID::CharTyID:
                return get(' ', info);
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

                long long sign = 1;
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
                        unsigned long long val = std::stoull(val_part, nullptr, base);
                        return Constant::get(sign == 1 ? val : (unsigned long long)-(long long)val, tok.info);
                    } 
                    else if (suffix == "L") {
                        long long val = std::stoll(val_part, nullptr, base);
                        return Constant::get(sign * val, tok.info);
                    } 
                    else if (suffix == "U") {
                        unsigned int val = (unsigned int)std::stoul(val_part, nullptr, base);
                        return Constant::get(sign == 1 ? val : (unsigned int)-(int)val, tok.info);
                    } 
                    else {
                        long long val = std::stoll(val_part, nullptr, base);
                        val *= sign;
                        if (val > INT_MAX || val < INT_MIN) {
                            return Constant::get(val, tok.info);
                        }
                        return Constant::get((int)val, tok.info);
                    }
                } 
                catch (...) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                        "Literal value out of range or invalid: " + tok.content,
                                        tok.info);
                }
            }
            case TokenType::FLOAT_N:
                return Constant::get(std::stof(tok.content.c_str()), tok.info);
            case TokenType::STRING:
                return Constant::get(fzlib::String(tok.content.c_str()), tok.info);
            case TokenType::CHAR:
                return Constant::get(tok.content[0], tok.info);
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot create constant from non-constant token",
                                    tok.info);
        }
    }

    llvm::Type* Constant::toLLVMType(llvm::LLVMContext& ctx) {
        if (type) {
            return type->toLLVMType(ctx);
        }
        return nullptr;
    }
}
