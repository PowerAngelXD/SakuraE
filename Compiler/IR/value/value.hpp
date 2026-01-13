#ifndef SAKURAE_VALUE_HPP
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>

#include "type.hpp"
#include "Compiler/Frontend/lexer.h"

namespace sakuraE {
    class Value {
        std::variant<std::monostate, int, double, fzlib::String, char, bool, std::size_t> content;
        IR::Type type;
        PositionInfo create_info;
    public:
        Value()=default;
        Value(int val, PositionInfo info): 
            content(val), type(IR::TypeToken::Integer), create_info(info) {}
        Value(double val, PositionInfo info): 
            content(val), type(IR::TypeToken::Float), create_info(info) {}
        Value(fzlib::String val, PositionInfo info): 
            content(val), type(IR::TypeToken::String), create_info(info) {}
        Value(char val, PositionInfo info): 
            content(val), type(IR::TypeToken::Char), create_info(info) {}
        Value(bool val, PositionInfo info): 
            content(val), type(IR::TypeToken::Bool), create_info(info) {}
        Value(std::size_t val, IR::TypeToken tok, PositionInfo info): 
            content(val), type(tok), create_info(info) {}
        
        const IR::Type& getType() const {
            return type;
        }

        llvm::Type* getLLVMType(llvm::LLVMContext& context) {
            return type.toLLVMType(context);
        }

        const PositionInfo& getInfo() const {
            return create_info;
        }

        template<typename T>
        const T& getValue() const {
            if (std::holds_alternative<T>(content)) {
                return std::get<T>(content);
            }
            throw SakuraError(OccurredTerm::IR_GENERATING, 
                                "Unknown value type, but expect to get it",
                                create_info);
        }

        bool operator== (Value value) {
            if (type != value.type) return false;
            
            switch (type.getType())
            {
            case IR::TypeToken::Integer:
                return getValue<int>() == value.getValue<int>();
            case IR::TypeToken::Float:
                return getValue<double>() == value.getValue<double>();
            case IR::TypeToken::Bool:
                return getValue<bool>() == value.getValue<bool>();
            case IR::TypeToken::String:
                return getValue<fzlib::String>() == value.getValue<fzlib::String>();
            case IR::TypeToken::Char:
                return getValue<char>() == value.getValue<char>();
            case IR::TypeToken::BlockIndex:
                return getValue<std::size_t>() == value.getValue<std::size_t>();
            case IR::TypeToken::FunctionIndex:
                return getValue<std::size_t>() == value.getValue<std::size_t>();
            case IR::TypeToken::ModuleIndex:
                return getValue<std::size_t>() == value.getValue<std::size_t>();
            
            default:
                return false;
            }
        }

        bool operator!= (Value value) {
            return !operator==(value);
        }

        static Value make(Token tok) {
            switch (tok.type)
            {
            case TokenType::BOOL_CONST:
                return Value(tok.content == "true"?true:false, tok.info);
            case TokenType::INT_N:
                return Value(atoi(tok.content.c_str()), tok.info);
            case TokenType::FLOAT_N:
                return Value(atof(tok.content.c_str()), tok.info);
            case TokenType::STRING:
                return Value(tok.content, tok.info);
            case TokenType::CHAR:
                return Value(tok.content.at(0), tok.info);
            
            default:
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Unknown type of token to convert",
                                tok.info);
            }
        }

        template<typename T>
        static Value make(T value, PositionInfo info) {
            return Value(value, info);
        }

        template<typename T>
        static Value make(T value, IR::TypeToken tok, PositionInfo info) {
            return Value(value, tok, info);
        }
    };
}

#endif // !SAKURAE_VALUE_HPP