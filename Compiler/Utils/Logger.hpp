#ifndef SAKURAE_LOGGER_HPP
#define SAKURAE_LOGGER_HPP

#include "Compiler/Error/error.hpp"

namespace sutils {
    inline void print(fzlib::String content) {
        std::cout << "[SakoraUtils|Debug]: " << content;
    }

    inline void println(fzlib::String content) {
        std::cout << "[SakoraUtils|Debug]: " <<  content << std::endl;
    }

    inline void reportError(sakuraE::OccurredTerm term, fzlib::String content, sakuraE::PositionInfo pinfo) {
        sakuraE::SakuraError error(term, content, pinfo);
        throw std::runtime_error(error.toString().c_str());
    }
}

#endif