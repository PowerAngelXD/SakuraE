#ifndef SAKORA_TYPE_HPP
#define SAKORA_TYPE_HPP

#include <iostream>
#include <vector>
#include <variant>
#include <sstream>

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include "includes/magic_enum.hpp"
#include "includes/String.hpp"

#include "Compiler/Utils/Logger.hpp"

namespace sakoraE::IR {
    using LLVMTypePtr = std::unique_ptr<llvm::Type>;

    enum class TypeToken {
        Integer, Char,
        Float, String,
        Bool, Custom, Function, 
        Null
    };

    enum class ValueType {
        // Value Type
        Value, Pointer, Ref, 
        // Struct Type
        Array,
        // Flag modifier
        Undefined
    };

    struct ArrayModifier {
        int dimension = 1;
        std::vector<int> each_len;

        ArrayModifier(int d, std::vector<int> el): dimension(d), each_len(el) {}
    };

    class TypeModifier {
        ValueType tm_token = ValueType::Undefined;
        std::variant<std::monostate, ArrayModifier> mod_content;
    public:
        TypeModifier()=default;
        TypeModifier(ValueType t): tm_token(t) {}
        TypeModifier(ValueType t, int d, std::vector<int> el):  
            tm_token(t), mod_content(ArrayModifier(d, el)) {}

        TypeModifier(const TypeModifier& type_mod): 
            tm_token(type_mod.tm_token), mod_content(type_mod.mod_content) {} 

        const ValueType& getValueType() {
            return tm_token;
        }

        bool hasStructMod()  {
            return !std::holds_alternative<std::monostate>(mod_content);
        }

        const ArrayModifier& getModAsArray() {
            if (!std::holds_alternative<ArrayModifier>(mod_content))
                sutils::reportError(OccurredTerm::SYSTEM, "This Modifier's containing is not Array!", {});
            
            return std::get<ArrayModifier>(mod_content);
        }
        
        fzlib::String toString() {
            std::ostringstream oss;
            oss << "[ValueType: " << magic_enum::enum_name(tm_token) << ", Struct: ";

            if (!hasStructMod())
                oss << "<No Struct>";
            else if (std::holds_alternative<ArrayModifier>(mod_content)) {
                auto m = std::get<ArrayModifier>(mod_content);
                oss << "[Array: D = " << m.dimension << ", Each len = [";
                for (std::size_t i = 0; i < m.each_len.size(); i ++) {
                    auto j = m.each_len[i];
                    if (i == m.each_len.size() - 1) 
                        oss << j;
                    else
                        oss << j << ", ";
                }
                oss << "]";
            }

            oss << "]";

            return oss.str();
        }

        bool operator== (const TypeModifier& tm) {
            if (tm_token != tm.tm_token) return false;
            else if (std::holds_alternative<std::monostate>(mod_content) && 
                    !std::holds_alternative<std::monostate>(tm.mod_content))
                return false;
            else if (std::holds_alternative<ArrayModifier>(mod_content) && 
                    !std::holds_alternative<ArrayModifier>(tm.mod_content))
                return false;
            else return true;
        }

        bool operator!= (const TypeModifier& tm) {
            return !operator==(tm);
        }
    };
    
    class Type {
        TypeToken token = TypeToken::Null;
        TypeModifier mod;
    public:
        Type()=default;
        Type(TypeToken t): token(t) {}
        Type(TypeToken t, TypeModifier m): token(t), mod(m) {}

        const TypeToken& getType() { return token; }
        const TypeModifier& getModifier() { return mod; }

        fzlib::String toString() {
            std::ostringstream oss;
            oss << "{Type: " << magic_enum::enum_name(token) << ", Modifier: " <<  mod.toString() << "}";
            return oss.str();
        }

        // TODO: Fix this method - llvm::Type has protected destructor and cannot be managed by unique_ptr
        // This method should return llvm::Type* instead of unique_ptr<llvm::Type>
        /*
        std::unique_ptr<llvm::Type> toLLVMType(llvm::LLVMContext& ctx) {
            std::unique_ptr<llvm::Type> llvmType = nullptr;
            switch (token)
            {
            case TypeToken::Integer:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getInt32Ty(ctx));
                break;
            case TypeToken::Float:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getFloatTy(ctx));
                break;
            case TypeToken::Char:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getInt8Ty(ctx));
                break;
            case TypeToken::Bool:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getInt1Ty(ctx));
                break;
            case TypeToken::Null:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getVoidTy(ctx));
                break;
            case TypeToken::String:
                llvmType = std::make_unique<llvm::Type>(llvm::Type::getInt8Ty(ctx)->getPointerTo());
                break;
            
            default:
                break;
            }

            // TODO: 没有设计类型不符的检查
            if (mod.getValueType() == ValueType::Pointer)
                llvmType = std::make_unique<llvm::Type>(llvmType->getPointerTo());
            else if (mod.getValueType() == ValueType::Ref)
                llvmType = std::make_unique<llvm::Type>(llvmType->getPointerTo());
            else if (mod.getValueType() == ValueType::Array) {
                auto arr = mod.getModAsArray();
                for (auto it = arr.each_len.rbegin(); it != arr.each_len.rend(); it ++) {
                    llvmType = std::make_unique<llvm::Type>(llvm::ArrayType::get(llvmType.get(), *it));
                }
            }

            return llvmType;
        }
        */
    };
}

#endif // !SAKORA_TYPE_HPP
