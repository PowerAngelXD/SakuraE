#ifndef SAKORAE_LOGGER_HPP
#define SAKORAE_LOGGER_HPP

#include "Compiler/Error/error.hpp"

namespace sutils {
    inline void print(fzlib::String content) {
        std::cout << "[SakoraUtils|Debug]: " << content;
    }

    inline void println(fzlib::String content) {
        std::cout << "[SakoraUtils|Debug]: " <<  content << std::endl;
    }

    inline void reportError(sakoraE::OccurredTerm term, fzlib::String content, sakoraE::PositionInfo pinfo) {
        sakoraE::SakoraError error(term, content, pinfo);
        throw std::runtime_error(error.toString().c_str());
    }
}

#endif