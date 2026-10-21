#ifndef SAKURAE_MODULE_HPP
#define SAKURAE_MODULE_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include "Compiler/IR/Semantic/model/scope.hpp"
#include "function.hpp"
#include <map>
#include <cstring>

namespace sakuraE::IR {
    class Program;

    // SakuraE 模块：一个编译单元的语义 IR 容器。
    // 拥有其全部 Function（析构时 delete），并持有模块作用域与 NamingContext（结构体声明注册表、
    // 命名信息）；usingList 中的模块与 program 均为非拥有引用。
    class Module {
        // 模块 ID 与声明位置。
        fzlib::String ID = "$DefaultModule";
        PositionInfo createInfo;

        // 存储模块级全局标识符
        Scope<IRValue*> moduleScope;

        std::vector<Function*> fnList;
        // 当前函数（游标）在 fnList 中的索引；-1 表示尚无函数。
        long cursor = -1;

        // 本模块的命名上下文：管理结构体声明（StructDeclId 分配、opaque 声明与补全）。
        NamingContext namingContext;

        // 通过 use 引入的其他模块，名称查找时按顺序回退到这些模块。
        std::vector<Module*> usingList;

        // 所属 Program（非拥有引用）。
        Program* program = nullptr;
    public:
        // 构造模块：命名上下文以模块 ID 为前缀；模块作用域是最外层作用域，没有父作用域。
        Module(fzlib::String id, PositionInfo info):
            ID(std::move(id)), createInfo(info), moduleScope(info), namingContext(ID) {
            moduleScope.setParent(nullptr);
        }

        // 释放本模块拥有的全部函数。
        ~Module() {
            for (auto fn: fnList) {
                delete fn;
            }
        }

        // 所属 Program 的设置与获取。
        void setSourceProgram(Program* pgm) {
            program = pgm;
        }

        Program* getSourceProgram() const {
            return program;
        }

        // 创建并定义一个函数：自动创建 "entry" 入口块，加入函数列表并把游标移到它，
        // 在模块作用域中声明函数名，并将函数作用域的父作用域设为模块作用域。
        IRValue* buildFunction(fzlib::String name, TypeInfo* retType, FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, retType, std::move(params), info);
            func->setName(name);
            func->buildBlock("entry");
            func->setParent(this);
            fnList.push_back(func);
            cursor = fnList.size() - 1;

            moduleScope.declare(name, func, func->getSemanticType());

            func->fnScope().setParent(&moduleScope);
            return func;
        }

        // 声明运行时函数（仅声明、无函数体，不创建块）：name 为 IR 名称（可携带参数类型），
        // rawName 为源语言层面的原始名称，供 lookupRuntimeFunction 查找。
        IRValue* declareRuntimeFunction(fzlib::String name, fzlib::String rawName, TypeInfo* retType,
                                       FormalParamsDefine params, PositionInfo info) {
            Function* func = new Function(name, std::move(rawName), retType, std::move(params), info);
            func->setParent(this);
            fnList.push_back(func);
            cursor = fnList.size() - 1;

            moduleScope.declare(name, func, func->getSemanticType());

            func->fnScope().setParent(&moduleScope);
            return func;
        }

        // 按名称查找本模块中的结构体声明；找不到返回 nullptr。
        IRStructDecl* lookupStruct(fzlib::String name) {
            return namingContext.lookupStructDecl(name);
        }

        // 按 StructDeclId 解析结构体声明。
        const IRStructDecl* resolveStruct(const StructDeclId& id) const {
            return namingContext.resolveStruct(id);
        }

        // 以只读方式暴露命名上下文，作为结构体声明注册表(StructDeclRegistry)使用。
        const NamingContext& structRegistry() const { return namingContext; }

        // 声明 opaque 结构体（只有名称、尚无字段），用于支持前向引用与递归结构体。
        void declareStruct(fzlib::String name, PositionInfo info) {
            namingContext.declareOpaqueStruct(name, info);
        }

        // 补全此前声明的 opaque 结构体：填入字段与字段默认值。
        void implStruct(fzlib::String name, std::vector<SemanticField> fields,
                        std::map<fzlib::String, Constant*> defaults, PositionInfo info) {
            namingContext.implStruct(name, std::move(fields), std::move(defaults), info);
        }

        // 一次性声明并定义结构体（无默认值）。
        void declareAndImplStruct(fzlib::String name, std::vector<SemanticField> fields, PositionInfo info) {
            namingContext.defineStruct(std::move(name), std::move(fields), std::move(info));
        }

        // 返回模块作用域。
        Scope<IRValue*>& modScope() {
            return moduleScope;
        }

        // 移动函数游标；越界时报告 IR 生成错误。返回自身以便链式调用。
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

        // 返回游标指向的当前函数。模块中尚无函数（cursor 为 -1）或游标越界时报告 IR 生成错误，
        // 例如顶层出现函数/结构体定义以外的语句时，不会再访问 fnList[-1]
        Function* curFunc() {
            if (cursor < 0 || cursor >= static_cast<long>(fnList.size())) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "No current function: statements must appear inside a function body",
                                    createInfo);
            }
            return fnList[cursor];
        }

        // 按下标取函数（不做越界检查）。
        Function* func(long index) {
            return fnList[index];
        }

        // 按名称与完整形参列表（名称与语义类型都需一致）精确查找函数；找不到时报告 IR 生成错误。
        Function* func(fzlib::String name, FormalParamsDefine params) {
            std::vector<Function*> basic_results;
            for (auto fn: fnList) {
                if (fn->getName() == name)
                    basic_results.push_back(fn);
            }

            // 在同名候选中比较形参：语义类型按指针比较（依赖类型池对 TypeInfo 的驻留/复用）。
            for (auto& fn: basic_results) {
                bool equal = true;
                const auto formalParams = fn->getFormalParams();

                if (formalParams.size() != params.size()) continue;

                for (std::size_t i = 0; i < formalParams.size(); i ++) {
                    const auto& arg = formalParams[i];
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

        // 返回全部函数（只读）。
        const std::vector<Function*>& getFunctions() const {
            return fnList;
        }

        // 按原始名称与参数个数查找重载候选。IR 名称会携带参数类型后缀（"rawName_..."），
        // 因此有参函数按 "rawName_" 前缀匹配；无参函数要求名称与 rawName 完全相同。
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
                if (sameName && fn->getSemanticParamTypes().size() == arity) {
                    result.push_back(fn);
                }
            }
            return result;
        }

        // 返回函数游标的可写引用。
        long& cur() {
            return cursor;
        }

        // 重置游标：模块游标与每个函数的块游标都回到 0。
        void reset() {
            cursor = 0;
            for (auto fn: fnList) {
                fn->reset();
            }
        }

        // 返回模块 ID。
        const fzlib::String& id() const {
            return ID;
        }

        // 引入另一个模块（using），其符号在本模块查找失败后可见。
        void use(Module* mod) {
            usingList.push_back(mod);
        }

        // 返回已引入的模块列表。
        const std::vector<Module*>& getUsingList() const {
            return usingList;
        }

        // 在本模块及其引入模块中查找符号；找不到返回 nullptr。
        Symbol<IRValue*>* lookup(fzlib::String n) {
            std::map<fzlib::String, Module*> map;
            return lookup(n, map);
        }

        /* 运行时函数保留源语言层面的原始名称，而其 IR 名称会携带参数类型。
         * 此查找机制用于支持 println 等运行时 API，因为这些 API 的实现接受任意装箱值。
         * 先查本模块，再递归查引入的模块；找不到返回 nullptr。
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

        // 带访问记录的符号查找：visited 以模块 ID 记录已访问模块，避免 using 循环导致无限递归。
        // 先查模块作用域，再按 usingList 顺序深度优先查找，返回第一个命中。
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

        // 生成调试用文本表示："module ID {函数...}"。
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
