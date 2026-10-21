#ifndef SAKURAE_MODULE_EMITTER_HPP
#define SAKURAE_MODULE_EMITTER_HPP

#include <cstdint>
#include <map>

// 前向声明：头文件只保存指针/引用，避免引入完整的 LLVM 与 Backend IR 头文件
namespace llvm {
    class Module;
    class Type;
    class Value;
}

namespace fzlib {
    class String;
}

namespace sakuraE::IR {
    class BackendFunction;
    class BackendModule;
}

namespace sakuraE::Codegen {
    class LLVMCodeGenerator;
    struct FunctionEmitter;

    // 单个 BackendModule 到 llvm::Module 的发射器。
    // impl() 创建 llvm::Module 并声明所有可见函数（本模块定义、导入函数与 runtime 函数），
    // codegen() 再为本模块定义的函数生成函数体并逐个校验；
    // 另外提供生成 GC 类型描述符（GCTypeInfo*）运行时调用的辅助方法。
    // 由 LLVMCodeGenerator 创建并 delete。
    struct ModuleEmitter {
        // 被发射的 Backend 模块（非拥有引用）
        const IR::BackendModule& backend;
        // 模块 ID（引用 backend.ID），同时作为 llvm::Module 的名称
        const fzlib::String& ID;
        // impl() 中 new 出的 llvm::Module；析构函数不释放它，
        // 所有权由使用方接管（如 JIT 用 unique_ptr 接管后把该指针置空）
        llvm::Module* content = nullptr;
        // BackendFunction -> 函数发射器（拥有，析构时 delete）；包含本模块函数与导入函数的声明
        std::map<const IR::BackendFunction*, FunctionEmitter*> functions;
        // 共享的代码生成上下文
        LLVMCodeGenerator& codegenContext;

        // 仅记录引用；llvm::Module 在 impl() 中才创建
        ModuleEmitter(const IR::BackendModule& source, LLVMCodeGenerator& codegen);
        // 释放所有 FunctionEmitter（不释放 content）
        ~ModuleEmitter();

        // GC 类型描述符辅助方法：在当前插入点发射对运行时 __gc_get_* 工厂的调用，返回 GCTypeInfo*。
        // 这些运行时函数通过 getOrInsertFunction 按需声明，而不是走 BackendModule 的符号表。
        // getAtomicGCType：无内部引用的原子对象描述符
        llvm::Value* getAtomicGCType();
        // getArrayGCType：数组布局描述符（元素是否为指针、元素字节大小、元素描述符、嵌入式数组长度）
        llvm::Value* getArrayGCType(bool isPtr, uint32_t memberSize, llvm::Value* memTy, uint64_t length = 0);
        // getRuntimeValueArrayGCType：元素为 RuntimeValue* 的托管（装箱）数组描述符
        llvm::Value* getRuntimeValueArrayGCType(uint32_t memberSize, uint64_t length);
        // llvmTy2GCType：根据 LLVM 类型推导描述符（数组按元素递归；指针视为长度未定的指针数组；其余为原子）
        llvm::Value* llvmTy2GCType(llvm::Type* ty);
        // 按源名或 linkage 名查找模块可见函数的发射器；不存在时抛出 std::runtime_error
        FunctionEmitter* lookup(const fzlib::String& name) const;

        // 创建 llvm::Module，为可见函数创建 FunctionEmitter 并生成 LLVM 函数声明
        void impl();
        // 生成本模块定义函数的函数体，并用 llvm::verifyFunction 逐个校验
        void codegen();
    };
}

#endif /* !SAKURAE_MODULE_EMITTER_HPP */
