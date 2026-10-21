#ifndef SAKURAE_IR_CONTEXT_HPP
#define SAKURAE_IR_CONTEXT_HPP

#include "Compiler/IR/Backend/type/type.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"

#include <Compiler/Error/error.hpp>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace sakuraE::IR {
    // 作用于整个 Program 的类型上下文：拥有并驻留全部非结构体的 storage 类型(IRType)，
    // 同时拥有语义类型(TypeInfo) 的驻留池 TypeInfoContext。
    // 由 Program 按值持有，所有返回的 IRType* 都是非拥有引用，生命周期与本 IRContext 相同。
    // 结构体布局(IRStructType) 不在此驻留，而由各模块的 NamingContext 持有（见 NamingContext::getStructTy）。
    // 构造时把自身设为当前线程的活动上下文，析构时恢复上一个，形成按栈嵌套的 current() 语义。
    class IRContext {
        // 定宽基础类型的单例，首次请求时惰性创建
        std::unique_ptr<IRVoidType> voidType;
        std::unique_ptr<IRIntegerType> boolType;
        std::unique_ptr<IRIntegerType> charType;
        std::unique_ptr<IRIntegerType> int32Type;
        std::unique_ptr<IRIntegerType> int64Type;
        std::unique_ptr<IRIntegerType> uint32Type;
        std::unique_ptr<IRIntegerType> uint64Type;
        std::unique_ptr<IRFloatType> float32Type;
        std::unique_ptr<IRFloatType> float64Type;
        std::unique_ptr<IRTypeInfoType> typeInfoType;
        std::unique_ptr<IRBlockType> blockType;

        // 带参数的类型驻留表：键即该类型的 canonical identity，同键始终返回同一实例。
        // 任意位宽有符号/无符号整数，以位宽为键
        std::map<unsigned, std::unique_ptr<IRIntegerType>> integerTypes;
        std::map<unsigned, std::unique_ptr<IRIntegerType>> uintegerTypes;
        // 指针 / 引用类型，以元素类型指针为键（元素类型本身已驻留，因此指针身份即结构身份）
        std::map<IRType*, std::unique_ptr<IRPointerType>> pointerTypes;
        std::map<IRType*, std::unique_ptr<IRRefType>> refTypes;
        // 数组类型，以 (元素类型, 长度) 为键
        std::map<std::pair<IRType*, uint64_t>, std::unique_ptr<IRArrayType>> arrayTypes;
        // 函数签名类型，以 (返回类型, 参数类型列表) 为键
        std::map<std::pair<IRType*, std::vector<IRType*>>, std::unique_ptr<IRFunctionType>> functionTypes;
        // 语义类型(TypeInfo) 驻留池，由本上下文独占拥有
        std::unique_ptr<TypeInfoContext> typeInfoPool_;

        // 构造本对象之前的活动上下文，析构时恢复
        IRContext* previousContext = nullptr;
        // 当前线程的活动上下文，供 IRType::getXxxTy 等静态工厂使用
        static thread_local IRContext* activeContext;

    public:
        IRContext();
        ~IRContext();

        // 禁止拷贝与移动：activeContext 与外部持有的 IRType* 都依赖对象地址稳定
        IRContext(const IRContext&) = delete;
        IRContext& operator=(const IRContext&) = delete;
        IRContext(IRContext&&) = delete;
        IRContext& operator=(IRContext&&) = delete;

        // 返回当前线程的活动 IRContext；没有活动上下文时抛出异常
        static IRContext& current();

        // 访问语义类型驻留池；两个名字是同一对象的别名访问器
        TypeInfoContext& getTypeInfoManager() const { return *typeInfoPool_; }
        TypeInfoContext& typeInfoPool() const { return *typeInfoPool_; }

        // 获取驻留的 storage 类型：同一参数总是返回同一实例，不存在时创建并由本上下文拥有
        IRType* getVoidTy();
        IRType* getBoolTy();
        IRType* getCharTy();
        IRType* getInt32Ty();
        IRType* getInt64Ty();
        IRType* getIntNTy(unsigned bitWidth);
        IRType* getUInt32Ty();
        IRType* getUInt64Ty();
        IRType* getUIntNTy(unsigned bitWidth);
        IRType* getFloat32Ty();
        IRType* getFloat64Ty();
        IRType* getTypeInfoTy();
        IRType* getPointerTo(IRType* elementType);
        IRType* getRefTo(IRType* elementType);
        IRType* getArrayTy(IRType* elementType, uint64_t numElements);
        IRType* getBlockTy();
        IRType* getFunctionTy(IRType* returnType, std::vector<IRType*> params);
    };
}

#endif
