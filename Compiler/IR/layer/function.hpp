#ifndef SAKORAE_FUNCTION_HPP
#define SAKORAE_FUNCTION_HPP

#include "block.hpp"

namespace sakoraE::IR {
    class Function {
        fzlib::String func_name;
        std::vector<std::pair<fzlib::String, Type>> formal_params;
        
        std::vector<Block> blocks;
        // Indicates the current maximum index of blocks
        std::size_t cur = 0;
    public:
        Function(fzlib::String name): func_name(name) {}

        void insert(const Block& block) {
            blocks.push_back(block);
            cur ++;
        }

        // Return current cursor
        std::size_t current() {
            return cur;
        }

        const fzlib::String& getName() {
            return func_name;
        }

        const std::vector<std::pair<fzlib::String, Type>>& getFormalParams() {
            return formal_params;
        }
    };
}

#endif // !SAKORAE_FUNCTION_HPP