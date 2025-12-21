#ifndef SAKORAE_ERROR_HPP
#define SAKORAE_ERROR_HPP

#include <iostream>
#include <sstream>

#include "includes/magic_enum.hpp"

namespace sakoraE {
    struct PositionInfo {
        int line;
        int column;
        std::string details;
    };

    // Point the term where occurred the error
    enum OccurredTerm {
        LEXER, PARSER, IR_GENERATING, COMPILING, SYSTEM
    };

    class SakoraError {
        OccurredTerm term;
        std::string content;
        PositionInfo info;
    public:
        SakoraError(OccurredTerm t, std::string c, PositionInfo pinfo): 
            term(t), content(c), info(pinfo) {}
        
        const std::string toString() {
            std::ostringstream oss;
            oss << "During term: " << magic_enum::enum_name(term) << ", An Error Occurred:\n"
                << "    Details: " << content << "\n";

            if (term !=  OccurredTerm::SYSTEM)
                oss << "    Occurred in: line: " << info.line << ", column: " << info.column << ";\n";
            
            return oss.str();
        }
    };
}

#endif