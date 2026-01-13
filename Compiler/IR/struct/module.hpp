#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "function.hpp"

namespace sakuraE::IR {
    // SakuraE Module
    // Rule: Every block id starts as '$'
    class Module {
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // Storage global identifiers
        Scope globalScope;

        std::vector<Function> fnList;
        // Indicates the current maximum index of fnList
        std::size_t cursor = 0;

        // Determine whether a module has an entry
        bool hasEntry;
        // Indicate the entry function's position in FnList
        std::size_t entry;

    public:
        Module(fzlib::String id, PositionInfo info):
            ID("$" + id), createInfo(info), globalScope(info) {}

        Module& buildFunction(const Function& fn) {
            fnList.push_back(fn);
            cursor ++;

            return *this;
        }

        Module& buildFunction(fzlib::String name, PositionInfo info) {
            fnList.emplace_back(name, info);
            cursor ++;

            return *this;
        }

        Scope& scope() {
            return globalScope;
        }

        Function& curFunc() {
            return fnList[cursor];
        }

        Function& func(fzlib::String name, FormalParamsDefine params) {
            std::vector<Function> basic_results;
            for (auto fn: fnList) {
                if (fn.getName() == name) 
                    basic_results.push_back(fn);
            }

            for (auto& fn: basic_results) {
                bool equal = true;

                if (fn.getFormalParams().size() != params.size()) continue;

                for (std::size_t i = 0; i < fn.getFormalParams().size(); i ++) {
                    auto arg = fn.getFormalParams()[i];
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

        const std::size_t& cur() {
            return cursor;
        }

        const fzlib::String& id() {
            return ID;
        }
    };
}

#endif // !SAKURAE_MODULE_HPP