#ifndef SAKURAE_BACKEND_MODULE_HPP
#define SAKURAE_BACKEND_MODULE_HPP

#include "function.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
#include <map>
#include <memory>
#include <vector>

namespace sakuraE::IR {
    class Module;
    class IRContext;
    class IRStructType;

    // 单个语义 IR 模块 lowering 后的 Backend IR 模块，由 LoweringSession 创建并拥有。
    // 它拥有本模块的 BackendFunction；其余指针（导入函数、依赖模块、结构体布局、上下文）都是非拥有引用。
    class BackendModule {
    public:
        // 对应的语义 IR 模块
        const Module* source = nullptr;
        // 所属类型上下文；同一 Session 内所有 BackendModule 必须共享同一个 IRContext
        IRContext* context = nullptr;
        // 模块 ID，与 source->id() 一致；"__runtime" 表示运行时声明模块
        fzlib::String ID;
        // 本模块定义的函数，与 source 的函数列表一一对应，由本模块拥有
        std::vector<std::unique_ptr<BackendFunction>> functions;
        // 经 using 链（传递闭包）可见的其他模块函数，去重
        std::vector<const BackendFunction*> imports;
        // 直接 using 的模块（不含传递依赖），去重
        std::vector<BackendModule*> usingModules;
        // 符号表：本模块与导入函数的 sourceName 和 linkageName 都映射到对应函数，供调用目标解析
        std::map<fzlib::String, const BackendFunction*> symbols;
        // 本模块声明的结构体 -> storage 布局（opaque 或已补全），布局由 NamingContext 拥有
        std::map<StructDeclId, IRStructType*> structTypes;
        // 可见函数中来自 "__runtime" 模块的外部声明
        std::vector<const BackendFunction*> runtimeFunctions;

        // 按 sourceName 或 linkageName 查找可见函数；未找到返回 nullptr
        const BackendFunction* lookup(const fzlib::String& name) const;
        // 不创建任何 LLVM 对象，校验整张描述图的一致性：模块身份、函数归属、导入去重、签名/ABI/GC root 元数据、
        // 每个可达值的 storage 与运行时描述是否一致、gmem/sizeof/常量的附加表、符号表、结构体布局与运行时声明列表；
        // 任一不一致即抛出 runtime_error
        void verify() const;
        // 输出模块的调试文本：结构体布局（只打印名字，不展开递归布局）、函数签名与 ABI/root 元数据、导入、using 和运行时声明
        fzlib::String toString() const;
    };
}

#endif
