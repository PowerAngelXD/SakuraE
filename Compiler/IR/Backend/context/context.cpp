#include "context.hpp"

#include <Compiler/IR/Backend/type/type.hpp>
#include <memory>
#include <stdexcept>

namespace sakuraE::IR {
    // 每个线程独立的活动上下文指针；为 nullptr 表示当前线程没有活动的 IRContext
    thread_local IRContext* IRContext::activeContext = nullptr;

    // 先用 *this 构造 TypeInfoContext（它需要回指所属 IRContext），记下旧的活动上下文后把自身设为活动上下文
    IRContext::IRContext():
        typeInfoPool_(new TypeInfoContext(*this)),
        previousContext(activeContext) {
        activeContext = this;
    }

    // 活动上下文构成一条按构造顺序的单链表（activeContext -> previousContext -> ...）。
    // 析构时把自身从链上摘除：自身是活动上下文就恢复上一个；否则（未按 LIFO 顺序销毁）
    // 找到以自身为 previousContext 的较新上下文并把它改接到自身的前驱，避免它日后恢复到已销毁的对象。
    // unique_ptr 成员随后释放全部驻留类型，外部持有的 IRType* 从此悬空
    IRContext::~IRContext() {
        if (activeContext == this) {
            activeContext = previousContext;
            return;
        }
        for (IRContext* newer = activeContext; newer; newer = newer->previousContext) {
            if (newer->previousContext == this) {
                newer->previousContext = previousContext;
                return;
            }
        }
    }

    // IRType 静态工厂的入口；没有活动上下文时抛出 runtime_error，而不是返回空引用
    IRContext& IRContext::current() {
        if (!activeContext) {
            throw std::runtime_error("No active IRContext");
        }
        return *activeContext;
    }

    // 以下单例 getter：首次调用时创建并缓存，之后复用同一实例。
    // 构造函数是私有的，因此只能用 new 并交给 unique_ptr 管理，而非 std::make_unique
    IRType* IRContext::getVoidTy() {
        if (!voidType) voidType = std::unique_ptr<IRVoidType>(new IRVoidType());
        return voidType.get();
    }

    IRType* IRContext::getBoolTy() {
        // bool 的 storage 为有符号 1 位整数
        if (!boolType) boolType = std::unique_ptr<IRIntegerType>(new IRIntegerType(1));
        return boolType.get();
    }

    IRType* IRContext::getCharTy() {
        // char 的 storage 为有符号 8 位整数
        if (!charType) charType = std::unique_ptr<IRIntegerType>(new IRIntegerType(8));
        return charType.get();
    }

    IRType* IRContext::getInt32Ty() {
        if (!int32Type) int32Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(32));
        return int32Type.get();
    }

    IRType* IRContext::getInt64Ty() {
        if (!int64Type) int64Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(64));
        return int64Type.get();
    }

    // 任意位宽有符号整数：1/8/32/64 位转发到对应单例（bool/char/i32/i64），保证同一 storage 类型只有一个实例；
    // 其余位宽按位宽查表，缺失时创建
    IRType* IRContext::getIntNTy(unsigned bitWidth) {
        switch (bitWidth) {
            case 1: return getBoolTy();
            case 8: return getCharTy();
            case 32: return getInt32Ty();
            case 64: return getInt64Ty();
            default: break;
        }
        auto it = integerTypes.find(bitWidth);
        if (it == integerTypes.end()) {
            it = integerTypes.emplace(bitWidth, std::unique_ptr<IRIntegerType>(new IRIntegerType(bitWidth))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getUInt32Ty() {
        if (!uint32Type) uint32Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(32, false));
        return uint32Type.get();
    }

    IRType* IRContext::getUInt64Ty() {
        if (!uint64Type) uint64Type = std::unique_ptr<IRIntegerType>(new IRIntegerType(64, false));
        return uint64Type.get();
    }

    // 任意位宽无符号整数：32/64 位转发到对应单例，其余位宽按位宽查表，缺失时创建
    IRType* IRContext::getUIntNTy(unsigned bitWidth) {
        switch (bitWidth) {
            case 32: return getUInt32Ty();
            case 64: return getUInt64Ty();
            default: break;
        }
        auto it = uintegerTypes.find(bitWidth);
        if (it == uintegerTypes.end()) {
            it = uintegerTypes.emplace(bitWidth, std::unique_ptr<IRIntegerType>(new IRIntegerType(bitWidth, false))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getFloat32Ty() {
        if (!float32Type) float32Type = std::unique_ptr<IRFloatType>(new IRFloatType(32));
        return float32Type.get();
    }

    IRType* IRContext::getFloat64Ty() {
        if (!float64Type) float64Type = std::unique_ptr<IRFloatType>(new IRFloatType(64));
        return float64Type.get();
    }

    IRType* IRContext::getTypeInfoTy() {
        if (!typeInfoType) typeInfoType = std::unique_ptr<IRTypeInfoType>(new IRTypeInfoType());
        return typeInfoType.get();
    }

    // 指针类型按元素类型驻留；元素类型本身已驻留，因此同一元素类型总是得到同一指针类型
    IRType* IRContext::getPointerTo(IRType* elementType) {
        auto it = pointerTypes.find(elementType);
        if (it == pointerTypes.end()) {
            it = pointerTypes.emplace(elementType, std::unique_ptr<IRPointerType>(new IRPointerType(elementType))).first;
        }
        return it->second.get();
    }

    // 引用类型与指针类型分表驻留，同一元素的 T* 与 T& 是不同实例
    IRType* IRContext::getRefTo(IRType* elementType) {
        auto it = refTypes.find(elementType);
        if (it == refTypes.end()) {
            it = refTypes.emplace(elementType, std::unique_ptr<IRRefType>(new IRRefType(elementType))).first;
        }
        return it->second.get();
    }

    // 数组类型以 (元素类型, 长度) 为键驻留
    IRType* IRContext::getArrayTy(IRType* elementType, uint64_t numElements) {
        const auto key = std::make_pair(elementType, numElements);
        auto it = arrayTypes.find(key);
        if (it == arrayTypes.end()) {
            it = arrayTypes.emplace(key, std::unique_ptr<IRArrayType>(new IRArrayType(elementType, numElements))).first;
        }
        return it->second.get();
    }

    IRType* IRContext::getBlockTy() {
        if (!blockType) blockType = std::unique_ptr<IRBlockType>(new IRBlockType());
        return blockType.get();
    }

    // 函数签名以 (返回类型, 参数列表) 为键驻留；键中保存一份参数列表副本，构造函数使用移走后的 params
    IRType* IRContext::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        const auto key = std::make_pair(returnType, params);
        auto it = functionTypes.find(key);
        if (it == functionTypes.end()) {
            it = functionTypes.emplace(key, std::unique_ptr<IRFunctionType>(new IRFunctionType(returnType, std::move(params)))).first;
        }
        return it->second.get();
    }
}
