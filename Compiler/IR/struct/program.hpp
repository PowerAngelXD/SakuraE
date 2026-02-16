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
            size_t targetSize = sizeof(void*) * 8;
            buildModule("__runtime", info, true);
            auto runtimeMod = curMod();
            
            runtimeMod->declareRuntimeFunction(
                "__alloc", 
                IRType::getPointerTo(IRType::getVoidTy()), 
                { {"size", IRType::getUIntNTy(targetSize)} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__free", 
                IRType::getVoidTy(), 
                { {"ptr", IRType::getPointerTo(IRType::getVoidTy())} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "create_string", 
                IRType::getPointerTo(IRType::getCharTy()), 
                { {"literal", IRType::getPointerTo(IRType::getCharTy())} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "free_string", 
                IRType::getVoidTy(), 
                { {"str", IRType::getPointerTo(IRType::getCharTy())} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "concat_string", 
                IRType::getPointerTo(IRType::getCharTy()), 
                { 
                    {"s1", IRType::getPointerTo(IRType::getCharTy())}, 
                    {"s2", IRType::getPointerTo(IRType::getCharTy())} 
                }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__print", 
                IRType::getVoidTy(), 
                { {"str", IRType::getPointerTo(IRType::getCharTy())} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__println", 
                IRType::getVoidTy(), 
                { {"str", IRType::getPointerTo(IRType::getCharTy())} }, 
                info
            );

            // gc methods
            runtimeMod->declareRuntimeFunction(
                "__gc_create_thread", 
                IRType::getVoidTy(), 
                {}, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__gc_alloc", 
                IRType::getPointerTo(IRType::getVoidTy()), 
                {
                    { "size", IRType::getUIntNTy(targetSize) },
                    { "ty", IRType::getUInt32Ty() }
                }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__gc_register", 
                IRType::getVoidTy(), 
                { {"addr", IRType::getPointerTo(IRType::getPointerTo(IRType::getVoidTy()))} }, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__gc_safe_point", 
                IRType::getVoidTy(), 
                {}, 
                info
            );

            runtimeMod->declareRuntimeFunction(
                "__gc_pop", 
                IRType::getVoidTy(), 
                { {"times", IRType::getUInt32Ty()} }, 
                info
            );
            
            runtimeMod->declareRuntimeFunction(
                "__gc_collect", 
                IRType::getVoidTy(), 
                {}, 
                info
            );
        }

        ~Program() {
            for (auto mod: moduleList) {
                delete mod;
            }
        }

        Program& buildModule(fzlib::String id, PositionInfo info, bool isRuntime = false) {
            Module* module = new Module(id, info);
            // Using runtime module
            if (!isRuntime)
                module->use(mod(0));
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