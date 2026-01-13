#ifndef SAKURAE_PROGRAM_HPP
#define SAKURAE_PROGRAM_HPP

#include "module.hpp"

namespace sakuraE::IR {
    class Program {
        fzlib::String ID;

        std::vector<Module> moduleList;
        // Indicates the current maximum index of moduleList
        std::size_t cur = 0;
    public:
        Program(fzlib::String id): ID(id) {}

        Program& buildModule(fzlib::String id, PositionInfo info) {
            moduleList.emplace_back(id, info);
            cur ++;

            return *this;
        }

        Module& curMod () {
            return moduleList[cur];
        }

        const Module& mod(std::size_t index) {
            return moduleList[index];
        }
    };
}

#endif // !SAKURAE_PROGRAM_HPP