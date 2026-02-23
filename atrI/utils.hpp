#ifndef SAKURAE_ATRI_UTILS_HPP
#define SAKURAE_ATRI_UTILS_HPP

#include <iostream>
#include <fstream>

#include "includes/String.hpp"

namespace atri {
    inline fzlib::String readSourceFile(fzlib::String path) {
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);

        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + std::string(path.c_str()));
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string content;
        if (size > 0) {
            content.reserve(size);
            content.assign((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        }
        fzlib::String result = content;
        return result;
    }

    inline void writeFile(fzlib::String path, fzlib::String content) {
        std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);

        if (!file.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + std::string(path.c_str()));
        }

        std::vector<char> buffer(65536);
        file.rdbuf()->pubsetbuf(buffer.data(), buffer.size());

        file.write(content.c_str(), content.len());

        file.close();
    }

    inline bool contains(std::vector<fzlib::String> arr, fzlib::String target) {
        for (auto e: arr) {
            if (target == e) return true;
        }
        return false;
    }
}

#endif // !SAKURAE_ATRI_UTILS_HPP
