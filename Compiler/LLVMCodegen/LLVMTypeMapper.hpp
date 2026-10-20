#ifndef SAKURAE_LLVM_TYPE_MAPPER_HPP
#define SAKURAE_LLVM_TYPE_MAPPER_HPP

namespace llvm {
    class LLVMContext;
    class Type;
}

namespace sakuraE::IR {
    class IRType;
}

namespace sakuraE::Codegen {
    llvm::Type* map(IR::IRType* type, llvm::LLVMContext& context);
}

#endif
