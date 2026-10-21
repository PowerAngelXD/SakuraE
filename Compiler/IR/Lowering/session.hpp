#ifndef SAKURAE_LOWERING_CONTEXT_HPP
#define SAKURAE_LOWERING_CONTEXT_HPP

#include <Compiler/IR/Semantic/type/type_info.hpp>
#include <Compiler/IR/Semantic/model/struct.hpp>
#include <Compiler/IR/Backend/type/type.hpp>
#include <Compiler/IR/Backend/model/module.hpp>
#include <unordered_map>
#include <stdexcept>
#include <set>

namespace sakuraE::IR {
    // LoweringSession：把语义层（TypeInfo / Function / Module）lowering 为 Backend IR 的会话对象。
    // - 绑定唯一的 IRContext 与 StructDeclRegistry（通常是 Program），只接受属于它们的 TypeInfo 与 Module；
    // - 拥有 lowering 产物 BackendModule（进而拥有 BackendFunction）以及 RuntimeTypeDescription；
    // - IRType 由 IRContext 驻留、结构体 IRStructType 由所属 NamingContext 拥有，本会话只持有非拥有指针；
    // - 多次调用是幂等的：同一源对象始终映射到同一个 Backend 对象，重复 lowering 只原地刷新内容，
    //   因此已交给后端的指针在会话生命周期内保持有效。
    class LoweringSession {
        // 非拥有引用：所有 storage 类型都从该上下文驻留获取
        IRContext& irContext;
        // 非拥有引用：用于解析 StructDeclId 到语义结构体声明及其所属 NamingContext；为空时无法 lowering 结构体
        const StructDeclRegistry* structResolver;
        // TypeInfo -> IRType 缓存；只缓存类型图已稳定（不含未完成结构体声明）的转换结果
        std::unordered_map<const TypeInfo*, IRType*> typeMap;
        // StructDeclId -> 结构体 storage 类型（非拥有，所有权在 NamingContext）；首次遇到该声明时登记
        std::map<StructDeclId, IRStructType*> structMap;
        // 正在填充字段的结构体集合；递归经指针回到自身时据此识别回边，避免无限递归
        std::set<StructDeclId> fillingStructs;
        // 源 Function -> BackendFunction（非拥有；对象由所属 BackendModule::functions 拥有）
        std::unordered_map<const Function*, BackendFunction*> functionMap;
        // 运行时类型元数据驻留表：以去掉 nullable 修饰后的基础 TypeInfo 为键，本会话拥有描述对象
        std::unordered_map<const TypeInfo*, std::unique_ptr<RuntimeTypeDescription>> runtimeTypeMap;
        // 源 Module -> BackendModule，本会话拥有
        std::unordered_map<const Module*, std::unique_ptr<BackendModule>> backendModules;
        // 为语义类型生成（或复用）运行时类型元数据 RuntimeTypeDescription；nullable 与其基础类型共享同一描述
        const RuntimeTypeDescription* lowerRuntimeType(const TypeInfo* type);
        // 把语义常量映射为后端常量种类（null 句柄优先于语义类型判定）
        ConstantKind lowerConstantKind(const Constant& constant) const;
        // 解析 gmem（结构体成员访问）指令，得到字段布局、下标、运行时类型与字段存储形式
        BackendFieldAccess lowerFieldAccess(const Instruction& instruction);

        // 校验 Module 属于本会话绑定的 Program 与 IRContext
        void validateModule(const Module& module) const;
        // （重新）lowering 单个函数：签名、ABI、GC 描述以及各 IRValue 的 side table
        void lowerFunctionContents(BackendFunction& function);
        // 刷新 BackendModule 的函数列表与结构体布局表；BackendFunction 已存在时原地更新
        void refreshModule(const Module& module);
        // 构建模块可见的符号表、import 列表、using 模块与 runtime 声明列表，并检查调用目标可见
        void populateModule(BackendModule& module);

        // 单次 lowerType 调用中产生、尚未确认可缓存的结果；
        // opaqueStruct 表示本次遍历遇到了未完成的结构体声明，此时整批结果都不写入 typeMap
        struct PendingLowering {
            std::unordered_map<const TypeInfo*, IRType*> results;
            bool opaqueStruct = false;
        };

        // 基础类型（整数/浮点/bool/char/void/string/类型描述符）的 storage 类型映射；复合类型抛出异常
        IRType* loweringBasicType(const TypeInfo* tyInfo) const;
        // lowerType 的递归实现：结果先记入 pending，由 lowerType 决定是否提交到缓存
        IRType* lowerTypeSafer(const TypeInfo* tyInfo, PendingLowering& pending);
        // 检查结构体布局可落地：拒绝按值递归的环，以及 void/函数类型字段
        void validateStruct(const StructDeclId& id) const;
    public:
        // resolver 可为空：此时只能 lowering 不含结构体的类型；lowerModule 要求它就是模块所属的 Program
        explicit LoweringSession(IRContext& ctx, const StructDeclRegistry* resolver = nullptr):
            irContext(ctx), structResolver(resolver) {}

        // 返回本会话绑定的 IRContext
        IRContext& context() const { return irContext; }

        // 把语义类型(TypeInfo) lowering 为 storage 类型(IRType)；结构体按需补全布局
        IRType* lowerType(const TypeInfo* tyInfo);
        // lowering 单个函数；实际会 lowering 其所属模块（及传递 import 的模块），返回该函数对应的 BackendFunction
        BackendFunction* lowerFunction(const Function& function);
        // lowering 模块及其传递 using 闭包，完成后对每个模块执行 verify；返回本会话拥有的 BackendModule
        BackendModule* lowerModule(const Module& module);
    };
}

#endif
