#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "function.hpp"

namespace sakuraE::IR {
    class Program;

    // SakuraE Module
    // Rule: Every block id around '<' and '>'
    class Module {
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // Storage module global identifiers
        Scope moduleScope;

        std::vector<Function*> fnList;
        // Indicates the current maximum index of fnList
        int cursor = -1;

        // Determine whether a module has an entry
        bool hasEntry;
        // Indicate the entry function's position in FnList
        std::size_t entry;

        Program* program;
    public:
        Module(fzlib::String id, PositionInfo info):
            ID("<" + id + ">"), createInfo(info), moduleScope(info) {
            moduleScope.setParent(nullptr);
        }

        void setSourceProgram(Program* pgm) {
            program = pgm;
        }

        Program* getSourceProgram() {
            return program;
        }

        Value* buildFunction(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, retType, params, info);
            fnList.push_back(func);
            cursor ++;

            return Constant::get(IRType::getFunctionTy(func->getReturnType(), func->getParamsOnlyType()), createInfo);
        }

        Scope& modScope() {
            return moduleScope;
        }

        Function* curFunc() {
            return fnList[cursor];
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

        const int& cur() {
            return cursor;
        }

        const fzlib::String& id() {
            return ID;
        }
    };
}

#endif // !SAKURAE_MODULE_HPP