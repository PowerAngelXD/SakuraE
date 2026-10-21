#include "LLVMTypeMapper.hpp"

#include "Compiler/IR/Backend/type/type.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <stdexcept>
#include <vector>

namespace sakuraE::Codegen {
    namespace {
        // 正在按值展开的结构体链（栈上的单向链表，parent 指向外层结构体），
        // 用于检测按值递归的结构体布局；指针/引用不进入该链，因为它们不展开成员类型。
        struct StructMapping {
            const IR::IRStructType* type;
            const StructMapping* parent;
        };

        // 递归映射 storage 类型；activeStruct 为当前正在展开其字段的结构体链（顶层调用为 nullptr）。
        llvm::Type* mapType(IR::IRType* type, llvm::LLVMContext& context,
                            const StructMapping* activeStruct) {
            if (!type) throw std::invalid_argument("Cannot map a null storage type");

            switch (type->getIRTypeID()) {
                case IR::VoidTyID:
                    return llvm::Type::getVoidTy(context);
                // 整数族：bool/char 同样是 IRIntegerType（1 位/8 位），统一按位宽映射为 iN；
                // LLVM 整数不区分有无符号，符号性由各条指令自行选择。
                case IR::BoolTyID:
                case IR::CharTyID:
                case IR::Integer32TyID:
                case IR::Integer64TyID:
                case IR::IntegerNTyID:
                case IR::UInteger32TyID:
                case IR::UInteger64TyID:
                case IR::UIntegerNTyID: {
                    const auto width = static_cast<IR::IRIntegerType*>(type)->getBitWidth();
                    if (width < llvm::IntegerType::MIN_INT_BITS || width > llvm::IntegerType::MAX_INT_BITS) {
                        throw std::invalid_argument("Unsupported integer storage width");
                    }
                    return llvm::IntegerType::get(context, width);
                }
                // 浮点仅支持 32/64 位，分别对应 float/double
                case IR::Float32TyID:
                case IR::Float64TyID:
                case IR::FloatNTyID:
                    switch (static_cast<IR::IRFloatType*>(type)->getBitWidth()) {
                        case 32: return llvm::Type::getFloatTy(context);
                        case 64: return llvm::Type::getDoubleTy(context);
                        default: throw std::invalid_argument("Unsupported floating-point storage width");
                    }
                // 指针、引用、类型描述符句柄统一映射为不透明 ptr：不递归指针元素类型，
                // 因此经由指针/引用的递归结构体（如链表节点）不会造成无限递归
                case IR::PointerTyID:
                case IR::RefTyID:
                case IR::TypeInfoTyID:
                    return llvm::PointerType::getUnqual(context);
                // 定长数组按值映射为 [N x elem]，元素类型沿用当前结构体链以检测递归
                case IR::ArrayTyID: {
                    const auto* array = static_cast<IR::IRArrayType*>(type);
                    auto* element = mapType(array->getElementType(), context, activeStruct);
                    if (!llvm::ArrayType::isValidElementType(element)) {
                        throw std::invalid_argument("Invalid array element storage type");
                    }
                    return llvm::ArrayType::get(element, array->getNumElements());
                }
                // 基本块类型对应 LLVM label
                case IR::BlockTyID:
                    return llvm::Type::getLabelTy(context);
                // 函数类型：逐个映射返回值与参数并校验其是否为 LLVM 允许的返回/参数类型
                case IR::FunctionTyID: {
                    const auto* function = static_cast<IR::IRFunctionType*>(type);
                    auto* result = mapType(function->getReturnType(), context, activeStruct);
                    if (!llvm::FunctionType::isValidReturnType(result)) {
                        throw std::invalid_argument("Invalid function return storage type");
                    }
                    std::vector<llvm::Type*> params;
                    params.reserve(function->getParamTypes().size());
                    for (auto* param : function->getParamTypes()) {
                        auto* mapped = mapType(param, context, activeStruct);
                        if (!llvm::FunctionType::isValidArgumentType(mapped)) {
                            throw std::invalid_argument("Invalid function parameter storage type");
                        }
                        params.push_back(mapped);
                    }
                    return llvm::FunctionType::get(result, params, false);
                }
                // 结构体：名称由模块 ID 与结构体名组成，保证跨模块同名结构体不冲突；
                // 同一 LLVMContext 内按名称复用已创建的 llvm::StructType
                case IR::StructTyID: {
                    const auto* structure = static_cast<IR::IRStructType*>(type);
                    const auto name = "sakurae.struct." + structure->getModuleID() + "." + structure->getName();
                    auto* mapped = llvm::StructType::getTypeByName(context, name.c_str());
                    if (!mapped) mapped = llvm::StructType::create(context, name.c_str());
                    // 首次遇到时先创建 opaque 结构体；声明尚未完整（字段未知）时直接返回 opaque，
                    // 待之后结构体补全、再次映射时原地 setBody
                    if (!structure->isComplete()) return mapped;

                    // 在按值展开链中再次遇到自身说明布局无限大，只能通过指针/引用间接递归
                    for (auto* frame = activeStruct; frame; frame = frame->parent) {
                        if (frame->type == structure) {
                            throw std::invalid_argument("Recursive by-value struct storage type");
                        }
                    }
                    const StructMapping frame {structure, activeStruct};
                    // 仅当 LLVM 侧仍是 opaque 时才收集字段并设置 body（body 只能设置一次）
                    const bool finishBody = mapped->isOpaque();
                    std::vector<llvm::Type*> fields;
                    if (finishBody) fields.reserve(structure->getFields().size());
                    for (const auto& field : structure->getFields()) {
                        // 字段按值映射：嵌套结构体会在此处被递归补全
                        auto* element = mapType(field.type, context, &frame);
                        if (!llvm::StructType::isValidElementType(element)) {
                            throw std::invalid_argument("Invalid struct field storage type");
                        }
                        if (finishBody) fields.push_back(element);
                    }
                    // 即使 body 已存在也要重新遍历字段：自上次映射以来，嵌套的 opaque 结构体
                    // 可能已经补全，遍历时的递归 mapType 会顺带为其 setBody。
                    if (finishBody) mapped->setBody(fields, false);
                    return mapped;
                }
                case IR::StringTyID:
                    break; // 字符串的 storage 是 char 指针（见 getStringTy），不存在独立的 IR 类型。
            }
            throw std::invalid_argument("Unsupported storage type");
        }
    }

    // 对外入口：从空的结构体展开链开始映射
    llvm::Type* map(IR::IRType* type, llvm::LLVMContext& context) {
        return mapType(type, context, nullptr);
    }
}
