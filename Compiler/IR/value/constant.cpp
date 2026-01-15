#include "constant.hpp"

namespace sakuraE::IR {
    static std::map<int, Constant> intConstants;
    static std::map<double, Constant> doubleConstants;
    static std::map<fzlib::String, Constant> stringConstants;
    static std::map<char, Constant> charConstants;
    static std::map<bool, Constant> boolConstants;

    static std::map<Type*, Constant> typeLabelConstants;

    Constant* Constant::get(Type* t, PositionInfo info) {
        auto it = typeLabelConstants.find(t);
        if (it != typeLabelConstants.end()) {
            return &it->second;
        }
        
        auto newEntry = typeLabelConstants.emplace(t, Constant(t, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(int val, PositionInfo info) {
        auto it = intConstants.find(val);
        if (it != intConstants.end()) {
            return &it->second;
        }

        Type* int32Ty = Type::getInt32Ty();
        auto newEntry = intConstants.emplace(val, Constant(int32Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(double val, PositionInfo info) {
        auto it = doubleConstants.find(val);
        if (it != doubleConstants.end()) {
            return &it->second;
        }

        Type* floatTy = Type::getFloatTy();
        auto newEntry = doubleConstants.emplace(val, Constant(floatTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(const fzlib::String& val, PositionInfo info) {
        auto it = stringConstants.find(val);
        if (it != stringConstants.end()) {
            return &it->second;
        }

        Type* charTy = Type::getIntNTy(8);
        Type* arrayTy = Type::getArrayTy(charTy, val.len() + 1);
        Type* stringTy = Type::getPointerTo(arrayTy);
        auto newEntry = stringConstants.emplace(val, Constant(stringTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(char val, PositionInfo info) {
        auto it = charConstants.find(val);
        if (it != charConstants.end()) {
            return &it->second;
        }

        Type* charTy = Type::getIntNTy(8);
        auto newEntry = charConstants.emplace(val, Constant(charTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(bool val, PositionInfo info) {
        auto it = boolConstants.find(val);
        if (it != boolConstants.end()) {
            return &it->second;
        }

        Type* boolTy = Type::getIntNTy(1);
        auto newEntry = boolConstants.emplace(val, Constant(boolTy, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::getFromToken(const Token& tok) {
        switch (tok.type) {
            case TokenType::BOOL_CONST:
                return Constant::get(tok.content == "true", tok.info);
            case TokenType::INT_N:
                return Constant::get(std::stoi(tok.content.c_str()), tok.info);
            case TokenType::FLOAT_N:
                return Constant::get(std::stod(tok.content.c_str()), tok.info);
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
