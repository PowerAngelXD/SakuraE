#ifndef SAKURAE_PROGRAM_HPP
#define SAKURAE_PROGRAM_HPP

#include "Compiler/Error/error.hpp"
#include "module.hpp"
#include <cstddef>

namespace sakuraE::IR {
    class Program {
        fzlib::String ID;

        std::vector<Module*> moduleList;
        // Indicates the current maximum index of moduleList
        int cursor = -1;
    public:
        Program(fzlib::String id): ID(id) {
            PositionInfo info = {0, 0, "System"};
            buildModule("RuntimeModule", info);
            auto runtimeMod = curMod();
            
            runtimeMod->buildFunction("__alloc", IRType::getPointerTo(IRType::getCharTy()), { {"size", IRType::getUInt64Ty()} }, info);
            runtimeMod->buildFunction("__free", IRType::getVoidTy(), { {"ptr", IRType::getPointerTo(IRType::getCharTy())} }, info);
            runtimeMod->buildFunction("create_string", IRType::getPointerTo(IRType::getCharTy()), { {"literal", IRType::getPointerTo(IRType::getCharTy())} }, info);
            runtimeMod->buildFunction("free_string", IRType::getVoidTy(), { {"str", IRType::getPointerTo(IRType::getCharTy())} }, info);
            runtimeMod->buildFunction("concat_string", IRType::getPointerTo(IRType::getCharTy()), { {"s1", IRType::getPointerTo(IRType::getCharTy())}, {"s2", IRType::getPointerTo(IRType::getCharTy())} }, info);
            runtimeMod->buildFunction("__print", IRType::getVoidTy(), { {"str", IRType::getPointerTo(IRType::getCharTy())} }, info);
            runtimeMod->buildFunction("__println", IRType::getVoidTy(), { {"str", IRType::getPointerTo(IRType::getCharTy())} }, info);

            buildModule("MainModule", info);
        }

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

        std::vector<Module*> getMods() {
            return moduleList;
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