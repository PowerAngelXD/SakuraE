#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/struct/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "function.hpp"
#include <map>

namespace sakuraE::IR {
    class Program;

    // SakuraE Module
    class Module {
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // Storage module global identifiers
        Scope<IRValue*> moduleScope;

        std::vector<Function*> fnList;
        // Indicates the current maximum index of fnList
        long cursor = -1;

        std::vector<Module*> usingList;

        Program* program;
    public:
        Module(fzlib::String id, PositionInfo info):
            ID(id), createInfo(info), moduleScope(info) {
            moduleScope.setParent(nullptr);
        }

        ~Module() {
            for (auto fn: fnList) {
                delete fn;
            }
        }

        void setSourceProgram(Program* pgm) {
            program = pgm;
        }

        Program* getSourceProgram() {
            return program;
        }

        IRValue* buildFunction(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, retType, params, info);
            func->setName(name);
            func->buildBlock("entry");
            func->setParent(this);
            fnList.push_back(func);
            cursor = fnList.size() - 1;

            std::vector<IRType *> tParams;
            for (auto param: params) {
                tParams.push_back(param.second);
            }
            moduleScope.declare(name, func, IRType::getFunctionTy(retType, tParams));

            func->fnScope().setParent(&moduleScope);
            return func;
        }

        IRValue* declareRuntimeFunction(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, name, retType, params, info);
            func->setParent(this);
            fnList.push_back(func);
            cursor = fnList.size() - 1;

            std::vector<IRType *> tParams;
            for (auto param: params) {
                tParams.push_back(param.second);
                name += "_" + param.second->toString();
            }
            func->setName(name);
            moduleScope.declare(name, func, IRType::getFunctionTy(retType, tParams));

            func->fnScope().setParent(&moduleScope);
            return func;
        }

        Scope<IRValue*>& modScope() {
            return moduleScope;
        }

        Module& moveCursor(long target) {
            if (target >= 0 && target < static_cast<long>(fnList.size())) {
                cursor = target;
            }
            else
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Move cursor to a unknown place",
                                    createInfo);
            return *this;
        }

        Function* curFunc() {
            return fnList[cursor];
        }

        Function* func(long index) {
            return fnList[index];
        }

        Function* func(fzlib::String name, FormalParamsDefine params) {
            std::vector<Function*> basic_results;
            for (auto fn: fnList) {
                if (fn->getName() == name)
                    basic_results.push_back(fn);
            }

            for (auto& fn: basic_results) {
                bool equal = true;

                if (fn->getFormalParams().size() != params.size()) continue;

                for (std::size_t i = 0; i < fn->getFormalParams().size(); i ++) {
                    auto arg = fn->getFormalParams()[i];
                    if (arg.first == params[i].first &&
                        arg.second == params[i].second) continue;
                    else equal = false;
                }
                if (equal) return fn;
            }

            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Expected to get an unknown function in module: '" + ID + "'",
                            createInfo);
        }

        std::vector<Function*> getFunctions() {
            return fnList;
        }

        long& cur() {
            return cursor;
        }

        void reset() {
            cursor = 0;
            for (auto fn: fnList) {
                fn->reset();
            }
        }

        const fzlib::String& id() {
            return ID;
        }

        void use(Module* mod) {
            usingList.push_back(mod);
        }

        std::vector<Module*> getUsingList() {
            return usingList;
        }

        Symbol<IRValue*>* lookup(fzlib::String n) {
            std::map<fzlib::String, Module*> map;
            return lookup(n, map);
        }

        Symbol<IRValue*>* lookup(fzlib::String n, std::map<fzlib::String, Module*>& visited) {
            if (visited.contains(ID)) return nullptr;
            visited[ID] = this;

            auto result = moduleScope.lookup(n);
            if (result) return result;

            for (auto mod: usingList) {
                result = mod->lookup(n, visited);
                if (result) return result;
            }

            return nullptr;
        }

        fzlib::String toString() {
            fzlib::String result = "module " + ID + " {";
            for (auto fn: fnList) {
                result += fn->toString();
            }
            result += "}";
            return result;
        }
    };
}

#endif // !SAKURAE_MODULE_HPP
