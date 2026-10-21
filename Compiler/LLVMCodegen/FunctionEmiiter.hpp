#ifndef SAKURAE_FUNCTIONEMIITER_HPP
#define SAKURAE_FUNCTIONEMIITER_HPP

#include <cstddef>
#include <cstdint>
#include <map>

#include "Compiler/IR/Semantic/model/scope.hpp"
#include "includes/String.hpp"

// 前向声明：头文件只保存指针/引用，避免引入完整的 LLVM 与 Backend IR 头文件
namespace llvm {
    class AllocaInst;
    class BasicBlock;
    class Function;
    class Type;
    class Value;
}

namespace sakuraE::IR {
    class BackendFunction;
    struct RuntimeTypeDescription;
}

namespace sakuraE::Codegen {
    class LLVMCodeGenerator;
    struct ModuleEmmiter;

    // 单个 BackendFunction 到 llvm::Function 的发射器。
    // 先由 impl() 按 ABI 声明 LLVM 函数，再由 codegen() 生成函数体（external 函数只声明）；
    // 同时负责函数内的 GC root scope、entry block 中的 alloca 槽位以及参数槽位管理。
    // 由所属 ModuleEmmiter 创建并 delete。
    struct FunctionEmiiter {
        // 被发射的 Backend IR 函数（非拥有引用，由 LoweringSession 持有）
        const IR::BackendFunction& backend;
        // 为 true 时只生成 LLVM 函数声明（runtime 函数或从其他模块导入的函数），不生成函数体
        bool external;
        // impl() 创建的 LLVM 函数；其所有权属于 parent->content 所在的 llvm::Module
        llvm::Function* content = nullptr;
        // 源语言标识符 -> 存储槽位（alloca）的作用域表，参数与局部变量在此声明
        IR::Scope<llvm::Value*> scope;
        // 所属模块发射器（非拥有），用于按名查找函数声明和获取 llvm::Module/DataLayout
        ModuleEmmiter* parent = nullptr;
        // 共享的代码生成上下文（LLVMContext、IRBuilder、值映射）
        LLVMCodeGenerator& codegenContext;
        // 参数名 -> 入口处为参数创建的 alloca 槽位，供 param 指令取地址
        std::map<fzlib::String, llvm::AllocaInst*> paramAllocaMap;
        // 当前已进入但尚未离开的 GC root scope 层数，用于 ret 前一次性全部离开
        uint32_t gcScopeDepth = 0;
        // 名为 "entry" 的 LLVM 入口基本块；所有 alloca 都集中插入这里，便于 mem2reg 提升
        llvm::BasicBlock* entryBlock = nullptr;

        // 仅保存引用/指针，LLVM 函数在 impl() 中才创建
        FunctionEmiiter(const IR::BackendFunction& source,
                       bool isExternal,
                       ModuleEmmiter* p,
                       LLVMCodeGenerator& codegen);
        ~FunctionEmiiter();

        // GC root scope：分别发射 __gc_enter_scope / __gc_leave_scope 调用并维护 gcScopeDepth；
        // gcLeaveScope 在深度为 0 时不发射任何调用，gcLeaveAllScopes 依次离开所有已进入的 scope
        void gcEnterScope();
        void gcLeaveScope();
        void gcLeaveAllScopes();
        // 发射 __gc_alloc(size, gcTy, elemCount)，返回托管对象 payload 指针；
        // elemCount 为空时传 0。int 重载以目标平台 intptr 宽度的常量传入 size
        llvm::Value* gcAlloc(llvm::Value* size, llvm::Value* gcTy, llvm::Value* elemCount = nullptr);
        llvm::Value* gcAlloc(int size, llvm::Value* gcTy, uint64_t elemCount = 0);
        // 将存放托管对象指针（如托管数组 payload）的槽位地址注册为 root（__gc_register）
        void gcRegisterRoot(llvm::Value* addr);
        // 将存放 RuntimeValue* 的槽位注册为 root（__gc_register_value_slot），由运行时按值的类型标签追踪其托管 payload
        void gcRegisterValueRoot(llvm::Value* addr);
        // 按运行时类型元数据选择注册方式：ManagedArray 走 gcRegisterRoot，其余走 gcRegisterValueRoot
        void gcRegisterManagedSlot(llvm::Value* addr, const IR::RuntimeTypeDescription& type);
        // 从 root 栈尾部弹出 times 个 root（__gc_pop）；times 为 0 时不发射调用
        void gcPop(size_t times);
        // 发射显式的 __gc_collect 调用
        void gcCollect();
        // 该值是否被 lowering 标记为需要作为 GC root 跟踪的临时值
        bool shouldTrackAsGCRoot(const IR::IRValue* value) const;
        // 把 SSA 值写入 entry block 中新建的槽位并注册为 GC root，返回该槽位；
        // 用于保护调用参数/结果、数组元素等跨越分配点仍需存活的托管临时值
        llvm::AllocaInst* createRootedTemporary(
            llvm::Value* value,
            const fzlib::String& slotName,
            const IR::RuntimeTypeDescription& type);
        // 在 entry block 第一条指令（函数级 __gc_enter_scope 调用）之后创建 alloca，
        // 创建后恢复原插入点，返回槽位
        llvm::AllocaInst* createAlloca(llvm::Type* ty, llvm::Value* arraySize = nullptr, fzlib::String n = "");
        // 按类型 t 的 DataLayout 分配大小在 GC 堆上分配 payload（对象 header 由运行时添加）
        llvm::Value* createHeapAlloc(llvm::Type* t, llvm::Value* gcTy, llvm::Value* elemCount);
        // 返回参数 n 的 alloca 槽位；未找到时返回 nullptr
        llvm::Value* getParamAddress(fzlib::String n);

        // 按 Backend ABI 创建 LLVM 函数声明（参数/返回值为 Raw 或 Boxed 形式）
        void impl();
        // 生成函数体：创建所有基本块、进入函数级 GC scope、落地参数槽位并逐条发射指令
        void codegen();
    };
}

#endif /* !SAKURAE_FUNCTIONEMIITER_HPP */
