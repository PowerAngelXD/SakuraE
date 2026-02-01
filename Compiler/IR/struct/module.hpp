#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "function.hpp"

namespace sakuraE::IR {
    class Program;

    // SakuraE Module
    // Rule: Every block id around '@<' and '>'
    class Module {
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // Storage module global identifiers
        Scope<IRValue*> moduleScope;

        std::vector<Function*> fnList;
        // Indicates the current maximum index of fnList
        long cursor = -1;

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

        IRValue* buildFunction(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, retType, params, info);
            func->setName("#" + name);
            func->buildBlock(name + ".init");
            fnList.push_back(func);
            cursor = fnList.size() - 1;

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

        const long& cur() {
            return cursor;
        }

        void reset() {
            cursor = 0;
        }

        const fzlib::String& id() {
            return ID;
        }

        fzlib::String toString() {
            fzlib::String result = ID + " {";
            for (auto fn: fnList) {
                result += fn->toString();
            }
            result += "}";
            return result;
        }
    };
}

#endif // !SAKURAE_MODULE_HPP