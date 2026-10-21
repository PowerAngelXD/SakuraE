#include "FunctionEmitter.hpp"

#include "LLVMCodegenerator.hpp"
#include "ModuleEmitter.hpp"
#include "Compiler/IR/Backend/model/function.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <vector>

namespace sakuraE::Codegen {
    // 只记录 Backend 函数与上下文；scope 以源函数的位置信息初始化，LLVM 函数延迟到 impl() 创建
    FunctionEmitter::FunctionEmitter(const IR::BackendFunction& source,
                                   bool isExternal,
                                   ModuleEmitter* p,
                                   LLVMCodeGenerator& codegen):
        backend(source), external(isExternal),
        scope(source.source->getInfo()), parent(p), codegenContext(codegen) {}

    // content 归 llvm::Module 所有，这里无需释放任何资源
    FunctionEmitter::~FunctionEmitter() = default;

    void FunctionEmitter::gcEnterScope() {
        // 进入函数或临时表达式作用域，后续注册的 root 会按栈序撤销
        auto fn = parent->lookup("__gc_enter_scope");
        codegenContext.builder->CreateCall(fn->content, {});
        gcScopeDepth ++;
    }

    // 离开最内层 root scope，运行时把 root 栈截断到进入时的深度；深度为 0 时忽略，避免多余的 leave
    void FunctionEmitter::gcLeaveScope() {
        if (gcScopeDepth == 0) return;
        auto fn = parent->lookup("__gc_leave_scope");
        codegenContext.builder->CreateCall(fn->content, {});
        gcScopeDepth --;
    }

    // ret 前调用：离开函数内所有尚未关闭的 scope（含函数级 scope），保证 root 栈与调用前一致。
    // ret 是终结指令，只结束当前控制流路径；gcScopeDepth 记录的是按发射顺序的词法深度，
    // 后续基本块（如 if 之后的另一个 return）仍处于同一深度，因此这里只发射调用而不修改计数
    void FunctionEmitter::gcLeaveAllScopes() {
        auto fn = parent->lookup("__gc_leave_scope");
        for (uint32_t depth = 0; depth < gcScopeDepth; ++depth) {
            codegenContext.builder->CreateCall(fn->content, {});
        }
    }

    llvm::Value* FunctionEmitter::gcAlloc(llvm::Value* size, llvm::Value* gcTy, llvm::Value* elemCount) {
        // 所有托管对象都通过统一入口分配，GC 可据此维护对象链表和类型信息
        auto fn = parent->lookup("__gc_alloc");
        if (!elemCount) {
            elemCount = codegenContext.builder->getInt64(0);
        }
        return codegenContext.builder->CreateCall(fn->content, {size, gcTy, elemCount});
    }

    // 常量大小版本：size 以目标 DataLayout 的 intptr 类型（对应 size_t）常量传入
    llvm::Value* FunctionEmitter::gcAlloc(int size, llvm::Value* gcTy, uint64_t elemCount) {
        auto fn = parent->lookup("__gc_alloc");
        auto sTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
        return codegenContext.builder->CreateCall(fn->content, {
            llvm::ConstantInt::get(sTy, size),
            gcTy,
            codegenContext.builder->getInt64(elemCount)
        });
    }

    void FunctionEmitter::gcRegisterRoot(llvm::Value* addr) {
        // addr 指向 LLVM alloca 槽位，运行时会在收集时读取槽位中的最新对象指针
        auto fn = parent->lookup("__gc_register");
        auto ptr = codegenContext.builder->CreateBitCast(addr, llvm::PointerType::getUnqual(*codegenContext.context));
        codegenContext.builder->CreateCall(fn->content, {ptr});
    }

    // addr 指向存放 RuntimeValue* 的槽位；运行时在注册时读取槽位中的值，
    // 并按其类型标签注册 String/Array/Struct/Pointer payload 字段的地址
    void FunctionEmitter::gcRegisterValueRoot(llvm::Value* addr) {
        auto fn = parent->lookup("__gc_register_value_slot");
        auto ptr = codegenContext.builder->CreateBitCast(addr,
            llvm::PointerType::getUnqual(*codegenContext.context));
        codegenContext.builder->CreateCall(fn->content, {ptr});
    }

    // 托管数组槽位存的是 payload 指针，可直接作为 root；其余托管值槽位存 RuntimeValue*，需按值注册
    void FunctionEmitter::gcRegisterManagedSlot(llvm::Value* addr, const IR::RuntimeTypeDescription& type) {
        if (type.representation == IR::ValueRepresentation::ManagedArray) {
            gcRegisterRoot(addr);
        }
        else {
            gcRegisterValueRoot(addr);
        }
    }

    // 从 root 栈尾部移除 times 个 root
    void FunctionEmitter::gcPop(size_t times) {
        if (times == 0) return;
        auto fn = parent->lookup("__gc_pop");
        codegenContext.builder->CreateCall(fn->content, {codegenContext.builder->getInt32(times)});
    }

    // 强制触发一次 GC 收集
    void FunctionEmitter::gcCollect() {
        auto fn = parent->lookup("__gc_collect");
        codegenContext.builder->CreateCall(fn->content, {});
    }

    // 由 lowering 阶段的 GCInfo.rootedValues 决定，codegen 不自行推断
    bool FunctionEmitter::shouldTrackAsGCRoot(const IR::IRValue* value) const {
        return backend.tracksRoot(value);
    }

    llvm::AllocaInst* FunctionEmitter::createRootedTemporary(
        llvm::Value* value,
        const fzlib::String& slotName,
        const IR::RuntimeTypeDescription& type) {
        // 将 SSA 值落到 entry block 的槽位，避免后续分配发生时值无法作为 root 被扫描
        auto* slot = createAlloca(value->getType(), nullptr, slotName);
        codegenContext.builder->CreateStore(value, slot);
        gcRegisterManagedSlot(slot, type);
        return slot;
    }

    // 所有 alloca 集中放在 entry block，以便 mem2reg (PromotePass) 提升，并保证槽位支配所有使用点
    llvm::AllocaInst* FunctionEmitter::createAlloca(llvm::Type* ty, llvm::Value* arraySize, fzlib::String n) {
        llvm::BasicBlock* currentBlock = codegenContext.builder->GetInsertBlock();
        llvm::BasicBlock::iterator currentPoint = codegenContext.builder->GetInsertPoint();
        // 插在 entry 块的首个可插入位置（所有已有指令之前）：不依赖 entry 中已存在任何指令。
        // alloca 不读取任何值，排在函数级 __gc_enter_scope 调用之前不影响语义
        codegenContext.builder->SetInsertPoint(entryBlock, entryBlock->getFirstInsertionPt());
        llvm::AllocaInst* alloca = codegenContext.builder->CreateAlloca(ty, arraySize, n.c_str());
        // 恢复调用前的插入点，调用方的指令流不受影响
        codegenContext.builder->SetInsertPoint(currentBlock, currentPoint);
        return alloca;
    }

    llvm::Value* FunctionEmitter::createHeapAlloc(llvm::Type* t, llvm::Value* gcTy, llvm::Value* elemCount) {
        // 数组 payload 不包含 header；header 由 Runtime::__gc_alloc 在 payload 前方创建
        size_t size = parent->content->getDataLayout().getTypeAllocSize(t);
        llvm::Type* sizeTy = parent->content->getDataLayout().getIntPtrType(*codegenContext.context);
        llvm::Value* sizeVal = llvm::ConstantInt::get(sizeTy, size);
        return gcAlloc(sizeVal, gcTy, elemCount);
    }

    // 未找到时返回 nullptr，由调用方处理
    llvm::Value* FunctionEmitter::getParamAddress(fzlib::String n) {
        if (paramAllocaMap.find(n) != paramAllocaMap.end()) {
            return paramAllocaMap[n];
        }
        return nullptr;
    }

    void FunctionEmitter::impl() {
        // 参数与返回值的 LLVM 类型由 lowering 给出的 ABI 决定：Raw 用 storage 类型映射，Boxed 为 ptr，Void 为 void
        std::vector<llvm::Type*> params;
        params.reserve(backend.parameters.size());
        for (std::size_t i = 0; i < backend.parameters.size(); ++i) {
            params.push_back(codegenContext.abiType(backend.parameters[i].second, backend.abi.parameters.at(i)));
        }

        auto* returnType = codegenContext.abiType(backend.returnType, backend.abi.result);
        auto* fnType = llvm::FunctionType::get(returnType, params, false);
        // 统一使用外部链接，使跨模块导入与 runtime 符号可由 JIT/链接器解析；
        // linkageName 对 runtime 函数是其原始符号名，否则为源函数名
        content = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
            backend.linkageName.c_str(), parent->content);
    }

    void FunctionEmitter::codegen() {
        // 设置当前函数，供 storageType()/runtimeType() 查询 Backend 元数据
        codegenContext.activeFunction = &backend;
        const auto* source = backend.source;
        const auto& irParams = backend.parameters;

        // 先为所有 IR 基本块创建 LLVM 基本块并 bind，使前向跳转（br/cond_br）能直接找到目标块
        for (auto block: source->getBlocks()) {
            llvm::BasicBlock* llvmBlock = llvm::BasicBlock::Create(*codegenContext.context, block->getName().c_str(), content);
            codegenContext.bind(block, llvmBlock);

            if (block->getName() == "entry") {
                entryBlock = llvmBlock;
            }
        }

        codegenContext.builder->SetInsertPoint(entryBlock);
        // 函数级 root scope 覆盖参数、局部变量和所有临时 root
        gcEnterScope();

        // 每个参数复制到 entry 中的 alloca 槽位，使参数与局部变量一样以“槽位”方式被访问
        std::size_t i = 0;
        for (auto& arg: content->args()) {
            arg.setName(irParams[i].first.c_str());

            llvm::AllocaInst* argAlloca = createAlloca(arg.getType(), nullptr, irParams[i].first);

            paramAllocaMap[irParams[i].first.c_str()] = argAlloca;

            codegenContext.builder->CreateStore(&arg, argAlloca);

            // 参数如果承载的是 GC 托管对象引用，需要在函数入口立即注册进 root stack
            if (backend.gcInfo.parameterRoots.at(i)) {
                gcRegisterManagedSlot(argAlloca, *backend.parameterRuntimeTypes.at(i));
            }

            // 登记到作用域表，源语言标识符可解析到该槽位
            scope.declare(irParams[i].first, argAlloca, nullptr);
            i ++;
        }
        // 按 IR 基本块顺序生成指令；instgen 内部会跳过已生成的值
        for (auto irBlock: source->getBlocks()) {
            codegenContext.builder->SetInsertPoint(llvm::cast<llvm::BasicBlock>(codegenContext.toLLVMValue(irBlock, this)));

            for (auto inst: irBlock->getInstructions()) {
                codegenContext.instgen(inst, this);
            }
        }

        /* 允许 main 函数自然结束：源语言要求 main 返回 i32，而 LLVM ABI 返回装箱后的
         * RuntimeValue*，因此为每个缺少终结指令的基本块补上 ret（装箱的 0）。
         * 与显式 ret 一样，返回前先离开所有仍打开的 GC root scope（此时通常只剩函数级 scope）。
         */
        if (backend.sourceName == "main") {
            for (auto& block: *content) {
                if (block.hasTerminator()) {
                    continue;
                }
                codegenContext.builder->SetInsertPoint(&block);
                gcLeaveAllScopes();
                auto* boxedZero = codegenContext.boxRaw(
                    codegenContext.builder->getInt32(0),
                    *backend.returnRuntimeType, this);
                codegenContext.builder->CreateRet(boxedZero);
            }
        }
    }
}
