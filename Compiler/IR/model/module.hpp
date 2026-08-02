#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/context.hpp"
#include "Compiler/IR/model/scope.hpp"
#include "Compiler/IR/type/type.hpp"
#include "function.hpp"
#include <map>
#include <cstring>

namespace sakuraE::IR {
    class Program;

    // SakuraE 模块
    class Module {
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // 存储模块级全局标识符
        Scope<IRValue*> moduleScope;

        std::vector<Function*> fnList;
        // fnList 当前索引的最大值
        long cursor = -1;

        NamingContext namingContext;

        std::vector<Module*> usingList;

        Program* program;
    public:
        Module(fzlib::String id, PositionInfo info):
            ID(std::move(id)), createInfo(info), moduleScope(info), namingContext(ID) {
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

        IRValue* buildFunction(fzlib::String name, IRType* retType, FormalParamsDefine params, PositionInfo info,
                               std::vector<TypeInfo*> semanticParams = {}, TypeInfo* semanticReturn = nullptr) {
            Function* func = new Function(name, retType, params, info,
                                          std::move(semanticParams), semanticReturn);
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

        IRStructDecl* lookupStruct(fzlib::String name) {
            return namingContext.lookupStructDecl(name);
        }

        IRStructType* lookupStructType(const fzlib::String& name) const {
            return namingContext.lookupStructType(name);
        }

        void declareStruct(fzlib::String name, PositionInfo info) {
            namingContext.declareOpaqueStruct(name, info);
        }

        void implStruct(fzlib::String name, std::vector<IRStructType::FieldInfo> fields,
                        std::map<fzlib::String, Constant*> defaults, PositionInfo info) {
            namingContext.implStruct(name, std::move(fields), std::move(defaults), info);
        }

        void declareAndImplStruct(fzlib::String name, std::vector<IRStructType::FieldInfo> fields, PositionInfo info) {
            namingContext.defineStruct(std::move(name), std::move(fields), std::move(info));
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

        std::vector<Function*> findFunctionCandidates(
            const fzlib::String& rawName,
            std::size_t arity
        ) const {
            std::vector<Function*> result;
            const auto prefix = rawName + "_";
            for (auto* fn : fnList) {
                const auto& name = fn->getName();
                const bool sameName = arity == 0
                    ? name == rawName
                    : (name.len() > prefix.len() &&
                       std::strncmp(name.c_str(), prefix.c_str(), prefix.len()) == 0);
                if (sameName && fn->getFormalParams().size() == arity) {
                    result.push_back(fn);
                }
            }
            return result;
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

        /* 运行时函数保留源语言层面的原始名称，而其 IR 名称会携带参数类型。
         * 此查找机制用于支持 println 等运行时 API，因为这些 API 的实现接受任意装箱值。
         */
        Function* lookupRuntimeFunction(fzlib::String rawName) {
            for (auto* fn: fnList) {
                if (fn->getRawName() == rawName) {
                    return fn;
                }
            }
            for (auto* mod: usingList) {
                if (auto* fn = mod->lookupRuntimeFunction(rawName)) {
                    return fn;
                }
            }
            return nullptr;
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

#endif /* !SAKURAE_MODULE_HPP */
