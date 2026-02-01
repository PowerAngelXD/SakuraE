#ifndef SAKURAE_PROGRAM_HPP
#define SAKURAE_PROGRAM_HPP

#include "module.hpp"

namespace sakuraE::IR {
    class Program {
        fzlib::String ID;

        std::vector<Module*> moduleList;
        // Indicates the current maximum index of moduleList
        int cursor = -1;
    public:
        Program(fzlib::String id): ID(id) {}

        Program& buildModule(fzlib::String id, PositionInfo info) {
            Module* module = new Module(id, info);
            moduleList.emplace_back(module);
            cursor ++;

            return *this;
        }

        Module* curMod () {
            return moduleList[cursor];
        }

        Module* mod(std::size_t index) {
            return moduleList[index];
        }

        void reset() {
            cursor = 0;
            for (auto mod: moduleList) {
                mod->reset();
            }
        }

        fzlib::String getID() {
            return ID;
        }

        fzlib::String toString() {
            fzlib::String result = ID + " {";
            for (auto mod: moduleList) {
                result += mod->toString();
            }
            result += "}";
            return result;
        }
    };
}

#endif // !SAKURAE_PROGRAM_HPP