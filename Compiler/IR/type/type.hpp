#ifndef SAKORA_TYPE_HPP
#define SAKORA_TYPE_HPP

#include <iostream>
#include <vector>
#include <variant>
#include <sstream>

#include "includes/magic_enum.hpp"
#include "Compiler/Utils/Logger.hpp"

namespace sakoraE::IR {
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
        
        std::string toString() {
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
        TypeToken token;
        TypeModifier mod;
    public:
        Type(TypeToken t): token(t) {}
        Type(TypeToken t, TypeModifier m): token(t), mod(m) {}

        const TypeToken& getType() { return token; }
        const TypeModifier& getModifier() { return mod; }

        std::string toString() {
            std::ostringstream oss;
            oss << "{Type: " << magic_enum::enum_name(token) << ", Modifier: " <<  mod.toString() << "}";
            return oss.str();
        }
    };
}

#endif // !SAKORA_TYPE_HPP
