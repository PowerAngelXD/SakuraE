#include "type.hpp"
#include "Compiler/IR/Backend/context/context.hpp"
#include <Compiler/Error/error.hpp>
#include <stdexcept>

namespace sakuraE::IR {
    // 只剥一层指针；非指针类型原样返回
    IRType* IRType::unwrapPointer() {
        if (!isPointer()) return this;
        else {
            auto ptr = static_cast<IRPointerType*>(this);
            return ptr->getElementType();
        }
    }

    // 求"底层" storage 类型：沿指针链向下剥，但把 char* 视作字符串 storage 保留下来
    IRType* IRType::getStorageType() {
        if (!isPointer()) return this;
        else {
            auto result = this;
            // 返回 true 表示停止剥离：当前已不是指针，或者是 char*
            auto checker = [&]() -> bool {
                if (result->isPointer()) {
                    auto ptr = static_cast<IRPointerType*>(result);
                    if (ptr->elementType->getIRTypeID() == CharTyID) return true;
                    else return false;
                }
                else return true;
            };

            while (!checker()) {
                result = static_cast<IRPointerType*>(result)->getElementType();
            }

            return result;
        }
    }

    // 结构相等判定。由于除结构体外的类型都在 IRContext 中驻留，同一上下文内多数情况第一条指针比较即可命中；
    // 逐项比较用于跨实例的兜底判定。
    bool IRType::isEqual(IRType* ty) {
        if (this == ty) return true;
        if (!ty) return false;
        if (irTypeID != ty->irTypeID) return false;

        // 种类相同后再按种类比较附加信息
        switch (irTypeID) {
            // 定宽标量与 void：ID 相同即相等
            case Integer32TyID:
            case Integer64TyID:
            case UInteger32TyID:
            case UInteger64TyID:
            case CharTyID:
            case BoolTyID:
            case TypeInfoTyID:
            case Float32TyID:
            case Float64TyID:
            case VoidTyID:
                return true;
            // 任意位宽整数：还需比较位宽
            case IntegerNTyID:
            case UIntegerNTyID: {
                auto lInt = static_cast<IRIntegerType*>(this);
                auto rInt = static_cast<IRIntegerType*>(ty);
                return lInt->getBitWidth() == rInt->getBitWidth();
            }
            // 数组：长度与元素类型都要相等
            case ArrayTyID: {
                auto lArr = static_cast<IRArrayType*>(this);
                auto rArr = static_cast<IRArrayType*>(ty);
                return lArr->getNumElements() == rArr->getNumElements() &&
                    lArr->getElementType()->isEqual(rArr->getElementType());
            }
            // 指针：递归比较元素类型
            case PointerTyID: {
                auto lPtr = static_cast<IRPointerType*>(this);
                auto rPtr = static_cast<IRPointerType*>(ty);
                return lPtr->getElementType()->isEqual(rPtr->getElementType());
            }
            // 函数：参数个数、逐个参数类型以及返回类型都要相等
            case FunctionTyID: {
                auto lFn = static_cast<IRFunctionType*>(this);
                auto rFn = static_cast<IRFunctionType*>(ty);
                auto& lParams = lFn->paramsType;
                auto& rParams = rFn->paramsType;
                if (lParams.size() != rParams.size()) return false;
                else {
                    for (std::size_t i = 0; i < lParams.size(); i ++) {
                        if (!lParams[i]->isEqual(rParams[i])) return false;
                    }
                }
                return lFn->returnType->isEqual(rFn->returnType);
            }
            // 结构体按名义比较：只看所属模块与名字（canonical identity），不比较字段，
            // 因此 opaque 与已补全的同一结构体视为相等，递归结构体也不会无限递归
            case StructTyID: {
                auto lStruct = static_cast<IRStructType*>(this);
                auto rStruct = static_cast<IRStructType*>(ty);
                return lStruct->parentModID == rStruct->parentModID &&
                    lStruct->name == rStruct->name;
            }
            // 其余种类（Ref、FloatN、Block 等）一律视为不等；注意 Ref 即使元素相同也会返回 false，
            // 只能依赖开头的指针身份比较判等
            default: return false;
        }
    }

    // 以下静态工厂均转发到当前活动的 IRContext，返回驻留的唯一实例
    IRType* IRType::getVoidTy() {
        return IRContext::current().getVoidTy();
    }

    IRType* IRType::getBoolTy() {
        return IRContext::current().getBoolTy();
    }

    IRType* IRType::getCharTy() {
        return IRContext::current().getCharTy();
    }

    IRType* IRType::getInt32Ty() {
        return IRContext::current().getInt32Ty();
    }

    IRType* IRType::getInt64Ty() {
        return IRContext::current().getInt64Ty();
    }

    IRType* IRType::getIntNTy(unsigned bitWidth) {
        return IRContext::current().getIntNTy(bitWidth);
    }

    IRType* IRType::getUInt32Ty() {
        return IRContext::current().getUInt32Ty();
    }
    IRType* IRType::getUInt64Ty() {
        return IRContext::current().getUInt64Ty();
    }
    IRType* IRType::getUIntNTy(unsigned bitWidth) {
        return IRContext::current().getUIntNTy(bitWidth);
    }

    IRType* IRType::getTypeInfoTy() {
        return IRContext::current().getTypeInfoTy();
    }

    // 字符串没有独立的 IRType，其 storage 为 char*
    IRType* IRType::getStringTy() {
        return IRContext::current().getPointerTo(IRContext::current().getCharTy());
    }

    IRType* IRType::getFloat32Ty() {
        return IRContext::current().getFloat32Ty();
    }

    IRType* IRType::getFloat64Ty() {
        return IRContext::current().getFloat64Ty();
    }

    IRType* IRType::getPointerTo(IRType* elementType) {
        return IRContext::current().getPointerTo(elementType);
    }

    IRType* IRType::getRefTo(IRType* elementType) {
        return IRContext::current().getRefTo(elementType);
    }

    IRType* IRType::getArrayTy(IRType* elementType, uint64_t numElements) {
        return IRContext::current().getArrayTy(elementType, numElements);
    }

    IRType* IRType::getBlockTy() {
        return IRContext::current().getBlockTy();
    }

    IRType* IRType::getFunctionTy(IRType* returnType, std::vector<IRType*> params) {
        return IRContext::current().getFunctionTy(returnType, std::move(params));
    }

    // 转换为字符串：用于 IR 文本输出与调试

    fzlib::String IRVoidType::toString() {
        return "void";
    }

    fzlib::String IRFloatType::toString() {
        // 任意位宽浮点不打印具体位宽
        if (bitWidth == 32) return "f32";
        else if (bitWidth == 64) return "f64";
        else return "fN";
    }

    // 8 位整数打印为 char；bool 打印为 i1。无符号整数同样打印为 iN，不区分符号
    fzlib::String IRIntegerType::toString() {
        if (bitWidth == 8) return "char";
        else return "i" + std::to_string(bitWidth) ;
    }

    fzlib::String IRTypeInfoType::toString() {
        return "tinfo";
    }

    fzlib::String IRPointerType::toString() {
        return elementType->toString() + "*";
    }

    fzlib::String IRRefType::toString() {
        return elementType->toString() + "&";
    }

    fzlib::String IRArrayType::toString() {
        return "" + elementType->toString() + "[" + std::to_string(numElements) + "]";
    }

    fzlib::String IRBlockType::toString() {
        return "irblock";
    }

    // 格式为 "fn->返回类型," 后接每个参数类型加 "|"
    fzlib::String IRFunctionType::toString() {
        fzlib::String result = "fn->" + returnType->toString() + ",";
        for (auto type: paramsType) {
            result += type->toString() + "|";
        }

        return result;
    }

    // 只打印字段类型，并会递归展开字段类型。lowering 已拒绝按值递归的结构体，
    // 但经由指针自引用的结构体（如 Node { next: Node* }）在此会经 IRPointerType::toString 无限递归；
    // BackendModule::toString 因此自行打印结构体名而不调用本方法展开布局
    fzlib::String IRStructType::toString() {
        fzlib::String result = "struct {";
        for (std::size_t i = 0; i < fields.size(); i ++) {
            result += fields[i].type->toString();
            // 最后一个字段后不加分隔符
            if (i == fields.size() - 1);
            else result += ", ";
        }
        result += "}";
        return result;
    }

    const fzlib::String& IRStructType::getName() const { return name; }

    std::size_t IRStructType::getCount() const { return fields.size(); }

    const std::vector<IRStructType::FieldInfo>& IRStructType::getFields() const {
        return fields;
    }

    bool IRStructType::isComplete() const {
        return isCompleteType;
    }

    // 通过 fieldIndices 找到下标后返回字段副本；opaque 结构体的索引为空，总是返回 std::nullopt
    std::optional<IRStructType::FieldInfo> IRStructType::findMember(
        const fzlib::String& target) const {
        const auto it = fieldIndices.find(target);
        if (it == fieldIndices.end()) return std::nullopt;
        return fields[it->second];
    }

    // 补全 opaque 结构体。先在局部容器中完成全部校验与索引构建，全部通过后再 swap 进成员，
    // 保证任何异常都不会让对象处于"半补全"状态（强异常安全）
    void IRStructType::complete(std::vector<IRStructType::FieldInfo> fs) {
        // 布局只允许确定一次，避免已被引用的布局被改写
        if (isCompleteType) throw std::logic_error("Cannot complete an already completed struct type");
        std::map<fzlib::String, std::size_t> indices;
        for (std::size_t index = 0; index < fs.size(); ++index) {
            // 每个字段必须已有 lowering 得到的 storage 类型
            if (!fs[index].type) throw std::invalid_argument("Struct field has no storage type");
            // emplace 失败说明字段名重复
            if (!indices.emplace(fs[index].name, index).second) {
                throw std::invalid_argument("Duplicate struct field name");
            }
        }
        fields.swap(fs);
        fieldIndices.swap(indices);
        isCompleteType = true;
    }

}
