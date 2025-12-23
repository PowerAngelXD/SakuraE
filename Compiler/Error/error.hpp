#ifndef SAKORAE_ERROR_HPP
#define SAKORAE_ERROR_HPP

#include <iostream>
#include <sstream>

#include "includes/magic_enum.hpp"

namespace sakoraE {
    struct PositionInfo {
        int line = 0;
        int column = 0;
        fzlib::String details = "no details";
    };

    // Point the term where occurred the error
    enum OccurredTerm {
        LEXER, PARSER, IR_GENERATING, COMPILING, SYSTEM
    };

    class SakoraError {
        OccurredTerm term;
        fzlib::String content;
        PositionInfo info;
    public:
        SakoraError(OccurredTerm t, fzlib::String c, PositionInfo pinfo): 
            term(t), content(c), info(pinfo) {}
        
        const fzlib::String toString() {
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