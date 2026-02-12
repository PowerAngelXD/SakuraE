#include "constant.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/IR/type/type.hpp"

namespace sakuraE::IR {
    static std::map<int, Constant> intConstants;
    static std::map<unsigned int, Constant> uIntConstants;
    static std::map<long long, Constant> llConstants;
    static std::map<unsigned long long, Constant> uLLConstants;
    static std::map<double, Constant> doubleConstants;
    static std::map<fzlib::String, Constant> stringConstants;
    static std::map<char, Constant> charConstants;
    static std::map<bool, Constant> boolConstants;
    static std::map<TypeInfo*, Constant> typeInfoConstants;
    static std::map<IRValue*, Constant> ptrConstants;


    Constant* Constant::get(unsigned int val, PositionInfo info) {
        auto it = uIntConstants.find(val);
        if (it != uIntConstants.end()) {
            return &it->second;
        }

        IRType* uint32Ty = IRType::getUInt32Ty();
        auto newEntry = uIntConstants.emplace(val, Constant(uint32Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(unsigned long long val, PositionInfo info) {
        auto it = uLLConstants.find(val);
        if (it != uLLConstants.end()) {
            return &it->second;
        }

        IRType* uInt64Ty = IRType::getUInt64Ty();
        auto newEntry = uLLConstants.emplace(val, Constant(uInt64Ty, val, info));
        return &newEntry.first->second;
    }
    Constant* Constant::get(long long val, PositionInfo info) {
        auto it = llConstants.find(val);
        if (it != llConstants.end()) {
            return &it->second;
        }

        IRType* int64Ty = IRType::getInt64Ty();
        auto newEntry = llConstants.emplace(val, Constant(int64Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(int val, PositionInfo info) {
        auto it = intConstants.find(val);
        if (it != intConstants.end()) {
            return &it->second;
        }

        IRType* int32Ty = IRType::getInt32Ty();
        auto newEntry = intConstants.emplace(val, Constant(int32Ty, val, info));
        return &newEntry.first->second;
    }

    Constant* Constant::get(float val, PositionInfo info) {
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
            case IRTypeID::FloatTyID:
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
            case TokenType::INT_N:
                return Constant::get(std::stoi(tok.content.c_str()), tok.info);
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
