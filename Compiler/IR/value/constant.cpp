#include "constant.hpp"

namespace sakuraE::IR {
    static std::map<int, Constant> intConstants;
    static std::map<double, Constant> doubleConstants;
    static std::map<fzlib::String, Constant> stringConstants;
    static std::map<char, Constant> charConstants;
    static std::map<bool, Constant> boolConstants;
    static std::map<TypeInfo*, Constant> typeInfoConstants;
    static std::map<IRValue*, Constant> ptrConstants;


    Constant* Constant::get(int val, PositionInfo info) {
        auto it = intConstants.find(val);
        if (it != intConstants.end()) {
            return &it->second;
        }

        IRType* int32Ty = IRType::getInt32Ty();
        auto newEntry = intConstants.emplace(val, Constant(int32Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(double val, PositionInfo info) {
        auto it = doubleConstants.find(val);
        if (it != doubleConstants.end()) {
            return &it->second;
        }

        IRType* floatTy = IRType::getFloatTy();
        auto newEntry = doubleConstants.emplace(val, Constant(floatTy, val, info));
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
