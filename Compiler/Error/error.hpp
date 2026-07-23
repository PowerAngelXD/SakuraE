#ifndef SAKURAE_ERROR_HPP
#define SAKURAE_ERROR_HPP

#include <iostream>
#include <sstream>

#include "includes/magic_enum.hpp"
#include "includes/String.hpp"

namespace sakuraE {
    struct PositionInfo {
        int line = 0;
        int column = 0;
        fzlib::String details = "no details";
    };

    // 指向错误发生的位置
    enum OccurredTerm {
        LEXER, PARSER, IR_GENERATING, COMPILING, RUNTIME, SYSTEM
    };

    class SakuraError: public std::exception {
        OccurredTerm term;
        fzlib::String content;
        PositionInfo info;
    public:
        SakuraError(OccurredTerm t, fzlib::String c, PositionInfo pinfo): 
            term(t), content(c), info(pinfo) {}

        OccurredTerm occurredTerm() const { return term; }
        const fzlib::String& message() const { return content; }
        const PositionInfo& position() const { return info; }
        
        const fzlib::String toString() const {
            std::ostringstream oss;
            oss << "During term: " << magic_enum::enum_name(term) << ", An Error Occurred:\n"
                << "    Details: " << content << "\n";

            if (term != OccurredTerm::SYSTEM && term != OccurredTerm::RUNTIME)
                oss << "    Occurred in: line: " << info.line << ", column: " << info.column << ";\n";
            
            return oss.str();
        }
    };
}

#endif
