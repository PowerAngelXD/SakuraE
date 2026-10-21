#ifndef SAKURAE_LLVM_TYPE_MAPPER_HPP
#define SAKURAE_LLVM_TYPE_MAPPER_HPP

// IRType（storage 类型）到 llvm::Type 的映射入口。
// 仅前向声明 LLVM/IR 类型，使包含方不必引入完整的 LLVM 头文件。
namespace llvm {
    class LLVMContext;
    class Type;
}

namespace sakuraE::IR {
    class IRType;
}

namespace sakuraE::Codegen {
    // 将 storage 类型(IRType) 映射为同一 LLVMContext 下的 llvm::Type。
    // 结构体按 "sakurae.struct.<模块ID>.<名称>" 命名并在 context 内复用；
    // 不完整的结构体返回 opaque 结构体，之后补全时原地 setBody。
    // 遇到空类型、非法位宽、按值递归结构体或不支持的类型时抛出 std::invalid_argument。
    llvm::Type* map(IR::IRType* type, llvm::LLVMContext& context);
}

#endif
