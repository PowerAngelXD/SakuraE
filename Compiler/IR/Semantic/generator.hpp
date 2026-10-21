#ifndef SAKURAE_GENERATOR_HPP
#define SAKURAE_GENERATOR_HPP

#include "Compiler/Error/error.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include "Compiler/IR/Semantic/model/instruction.hpp"
#include "Compiler/IR/Semantic/model/scope.hpp"
#include "Compiler/IR/Semantic/model/struct.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
#include "Compiler/IR/Semantic/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/String.hpp"
#include "includes/magic_enum.hpp"
#include "model/program.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/IR/Semantic/value/constant.hpp"
#include "Compiler/IR/Lowering/session.hpp"
#include "Compiler/Frontend/lexer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numbers>
#include <stdexcept>
#include <vector>



namespace sakuraE::IR {
    // 语义 IR 生成器：把源码经词法/语法分析得到的 AST 翻译为语义层 IR（Program/Module/Function/Block/Instruction）。
    // 职责包括：符号查找与作用域管理、语义类型(TypeInfo)的解析与检查（可赋值性、算术提升、nullable 规则）、
    // 函数名修饰与重载选择、控制流基本块的构建，以及禁止把 GC 托管对象的内部地址暴露为稳定指针/引用。
    // 此阶段只处理语义类型；storage 类型(IRType)与运行时类型元数据(RuntimeTypeInfo)由后续 lowering 产生。
    // 每个实例只能生成一次（见 startGenerate）。
    class IRGenerator {
        // 生成结果；拥有 IRContext、全部 Module 及其中的函数/块/指令。
        Program program;
        // 解析得到的顶层语句 AST，生成结束后保留，供调试输出（atrI 的 AST 显示）和测试使用。
        std::vector<NodePtr> parsedStatements;
        // 是否已调用过 startGenerate；用于拒绝在同一实例上重复生成。
        bool hasGenerated = false;

        // 当前 Program 的语义类型驻留池（与 typeInfoPool() 是同一对象）。
        TypeInfoContext& getTypeInfoContext() {
            return program.getContext().getTypeInfoManager();
        }

        // 取 IR 值的语义类型(TypeInfo)；仅为缩短调用写法。
        TypeInfo* semanticTypeOf(const IRValue* value) {
            return value->getSemanticType();
        }

        // 按名称解析标识符，找不到则抛出 "Unknown identifier"。
        // 先查当前函数作用域：它会从最内层块逐层向外查，并沿 parent 链查到本模块作用域；
        // 再查 Module::lookup，后者额外覆盖 usingList 中引入的模块（带 visited 集合防止循环引用）。
        // 返回的 Symbol 为非拥有引用，指向作用域符号表中的条目。
        Symbol<IRValue*>* lookup(fzlib::String n, PositionInfo info) {
            auto result = curFunc()->fnScope().lookup(n);
            if (!result) {
                result = curModule()->lookup(n);
            }

            if (!result) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                    "Unknown identifier: " + n,
                    info);
            }
            return result;
        }

        // 生成 load 指令，从左值地址 addr 读出值；结果语义类型与地址的语义类型相同
        // （语义 IR 中左值指令的语义类型即其所存值的类型）。
        // addr 必须是左值指令（create_alloca/param/indexing/gmem/deref 等），否则抛错。
        // 注意：错误文本沿用赋值场景的措辞，在纯读取场景下也会出现。
        IRValue* createLoad(IRValue* addr, PositionInfo info) {
            if (auto inst = dynamic_cast<Instruction*>(addr)) {
                if (!inst->isLValue()) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                        "An L-value is required as the left operand of an assignment.",
                        info);
                }
            // 这里不应对 Pointer 类型执行 Unwrap，否则会导致 LLVM IR 生成 store i32 i32，形成非法赋值

                return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::load,
                        addr->getSemanticType(),
                        {addr},
                        "load." + addr->getName());
            }
            throw SakuraError(OccurredTerm::IR_GENERATING,
                "An L-value is required as the left operand of an assignment.",
                info);
        }

        // 生成 store 指令，把 value 写入左值地址 addr。
        // 约束：addr 必须是左值指令；value 的语义类型必须可赋值给目标类型
        // （isAssignableTo：类型完全相同，或 T 隐式包装为 T?）。不做任何数值隐式转换。
        IRValue* createStore(IRValue* addr, IRValue* value, PositionInfo info) {
            if (auto inst = dynamic_cast<Instruction*>(addr)) {
                if (!inst->isLValue()) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "An L-value is required as the left operand of an assignment.",
                                    info);
                }

                if (!isAssignableTo(value->getSemanticType(), addr->getSemanticType())) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Cannot assign a value to a target with an incompatible type.",
                            info);
                }

                return curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::store,
                                                addr->getSemanticType(),
                                                {addr, value},
                                                "store." + addr->getName());
            }
            else {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "An L-value is required as the left operand of an assignment.",
                                    info);
            }
        }

        // 为当前函数声明一个形参：在当前块（即函数的 entry 块）中生成 param 指令，
        // 并把形参名绑定到函数作用域的最外层，使函数体内可按名称访问。
        // param 指令属于左值，后续读写通过 load/store 进行。info 参数当前未使用。
        IRValue* createParam(fzlib::String name, TypeInfo* semanticType, PositionInfo info) {
            auto param = curFunc()
                            ->curBlock()
                            ->createInstruction(
                                OpKind::param,
                                semanticType,
                                {},
                                "param." + name
                            );
            curFunc()->fnScope().declare(name, param, semanticType);

            return param;
        }

        // 声明局部变量：生成 create_alloca 指令并在当前作用域层登记名称 n。
        // 有初始值时先检查可赋值性；返回的地址即变量的左值。
        IRValue* createAlloca(fzlib::String n, TypeInfo* semanticType, IRValue* initVal, PositionInfo info) {
            if (initVal && !isAssignableTo(semanticTypeOf(initVal), semanticType)) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "Cannot declare a variable with an incompatible semantic type.",
                                info);
            }

            // create_alloca 的唯一操作数是初始值：
            // - 显式给出 initVal 时直接使用；
            // - 否则对基础标量类型（排除 String 与 TypeDescriptor）填入 Constant::getDefault 的零值；
            // - 其余类型（String、数组、结构体、指针、引用、nullable 等）填 nullptr，
            //   由 LLVM codegen 处理：数组在 GC 堆上分配，其他类型初始化为空指针。
            // Void 也属于 isBasic，会走 getDefault 并在那里报 "Unsupported semantic default initialization"。
            auto addr =  curFunc()
                ->curBlock()
                ->createInstruction(OpKind::create_alloca,
                                    semanticType,
                                    {initVal?initVal:(semanticType->isBasic() && semanticType->getTypeID() != String && semanticType->getTypeID() != TypeDescriptor ? Constant::getDefault(semanticType, info) : nullptr)},
                                    "create_alloca." + n);

            curFunc()->fnScope().declare(n, addr, semanticType);

            return addr;
        }

        // 判断某语义类型的值是否为 GC 托管的堆对象（string object 或数组）；
        // 引用类型按其所指类型递归判定。指针（含原生 char*）不算托管对象。
        bool isManagedHeapObjectType(const TypeInfo* ty) {
            if (!ty) {
                return false;
            }

            /* 这里明确区分语言层 string object 与原生 char*。
             * 只有 GC 托管对象本身，才属于“不允许暴露稳定内部地址”的对象。
             */
            if (ty->getTypeID() == TypeID::String || ty->isArray()) {
                return true;
            }

            if (ty->isRef()) {
                return isManagedHeapObjectType(ty->getPointeeType());
            }

            return false;
        }

        // 判断 value 是否是"指向 GC 托管对象内部"的左值：
        // 对托管对象做 indexing（数组/字符串元素）或 gmem（字段）得到的地址。
        // 编译器不会把这类派生的内部地址登记为 GC root（见 Runtime/GC-zh_cn.md），
        // 因此不能让它以语言层指针/引用的形式长期存活。
        bool isInteriorGCManagedLValue(IRValue* value) {
            auto* inst = dynamic_cast<Instruction*>(value);
            if (!inst || !inst->isLValue()) {
                return false;
            }

            // 只有基于另一个对象派生出的地址才可能是内部地址；
            // create_alloca/param/deref 等本身就是独立的存储槽位。
            switch (inst->getKind()) {
                case OpKind::indexing: {
                    return isManagedHeapObjectType(inst->arg(0)->getSemanticType());
                }
                case OpKind::gmem: {
                    /* 预留给后续 struct object：
                     * 一旦 gmem 的 base 成为 GC 托管对象，这里会统一拦截 field interior address。
                     */
                    return isManagedHeapObjectType(inst->arg(0)->getSemanticType());
                }
                default:
                    return false;
            }
        }

        // 供取地址（&，isRefOp=false）与取引用（ref，isRefOp=true）运算使用：
        // 若目标是 GC 托管对象的内部地址则抛错，错误文本按运算种类区分。
        void ensureStableAddressableLValue(IRValue* value, PositionInfo info, bool isRefOp) {
            if (!isInteriorGCManagedLValue(value)) {
                return;
            }

            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                isRefOp
                    ? "Interior address of a GC-managed object cannot be used as a stable language-level reference."
                    : "Interior address of a GC-managed object cannot be taken as a stable language-level pointer.",
                info
            );
        }

        // 从数组类型修饰中的维度表达式求出编译期长度。
        // 只接受整数字面量；表达式包装节点（只有单个子节点的层级）会被逐层剥开后递归求值。
        std::uint64_t getArrayDimension(NodePtr node) {
            if (node && node->getTag() == ASTTag::LiteralNode) {
                auto token = (*node)[ASTTag::Literal]->getToken();
                if (token.type != TokenType::INT_N) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Array dimension must be an integer literal.",
                        token.info
                    );
                }

                // 借助常量池按字面量规则确定整数的具体语义类型，再按该类型取值。
                // 有符号类型要求 > 0；无符号类型直接返回，此处未检查是否为 0。
                auto constant = Constant::getFromToken(token, getTypeInfoContext());
                switch (semanticTypeOf(constant)->getTypeID()) {
                    case Int32: {
                        auto value = constant->getContentValue<std::int32_t>();
                        if (value > 0) return static_cast<std::uint64_t>(value);
                        break;
                    }
                    case Int64: {
                        auto value = constant->getContentValue<std::int64_t>();
                        if (value > 0) return static_cast<std::uint64_t>(value);
                        break;
                    }
                    case UInt32:
                        return constant->getContentValue<std::uint32_t>();
                    case UInt64:
                        return constant->getContentValue<std::uint64_t>();
                    default:
                        break;
                }

                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "Array dimension must be greater than zero.",
                    token.info
                );
            }

            // 非字面量节点：若只有一个子节点，视为包装层继续向下找字面量。
            if (node) {
                auto children = node->getChildren();
                if (children.size() == 1) {
                    return getArrayDimension(children[0]);
                }
            }

            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Array dimension must be a compile-time integer literal.",
                node ? node->getPosInfo() : PositionInfo{0, 0, "Array dimension"}
            );
        }

        // 把 AST 中的类型修饰节点解析为语义类型(TypeInfo)。
        // 处理顺序：基础/数组类型 -> nullable 包装（仅结构体和数组允许）-> 引用或多级指针；
        // 引用与指针都不能再被 nullable 包装。最后用 Program::validateSemanticType 校验
        // 该类型属于本 Program 的 IRContext，且其中引用的结构体声明都可解析。
        // 所有 TypeInfo 都来自驻留池，返回值为非拥有引用。
        TypeInfo* getTypeInfoFromNode(sakuraE::NodePtr node) {
            TypeInfo* resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Void);

            // 外层 TypeModifierNode：先解析其中的基础类型或数组类型，再处理 nullable 标记。
            if (node->getTag() == ASTTag::TypeModifierNode) {
                if (node->hasNode(ASTTag::BasicTypeModifierNode)) {
                    resultTyInfo = getTypeInfoFromNode((*node)[ASTTag::BasicTypeModifierNode]);
                }
                else if (node->hasNode(ASTTag::ArrayTypeModifierNode)) {
                    resultTyInfo = getTypeInfoFromNode((*node)[ASTTag::ArrayTypeModifierNode]);
                }

                if (node->hasNode(ASTTag::NullableTag)) {
                    const auto nullableInfo = (*node)[ASTTag::NullableTag]->getPosInfo();
                    if (!resultTyInfo->isStruct() && !resultTyInfo->isArray()) {
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Only struct and array types can be nullable.",
                            nullableInfo
                        );
                    }
                    resultTyInfo = program.getContext().typeInfoPool().wrapTypeAsNullable(resultTyInfo, nullableInfo);
                }
            }

            // 基础类型关键字或结构体名：基础类型直接取驻留实例；
            // 标识符按结构体名在当前模块中查找，得到以 StructDeclId 标识的结构体语义类型。
            // 结构体名能在定义之前被引用，是因为 startGenerate 预先登记了所有结构体声明。
            if (node->getTag() == ASTTag::BasicTypeModifierNode) {
                auto kwNode = (*node)[ASTTag::Keyword];
                auto token = kwNode->getToken();

                switch (token.type) {
                    case TokenType::TYPE_I32:    {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32);
                        break;
                    }
                    case TokenType::TYPE_I64:    {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int64);
                        break;
                    }
                    case TokenType::TYPE_UI32:   {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt32);
                        break;
                    }
                    case TokenType::TYPE_UI64:   {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64);
                        break;
                    }
                    case TokenType::TYPE_F32:    {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float32);
                        break;
                    }
                    case TokenType::TYPE_F64:    {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float64);
                        break;
                    }
                    case TokenType::TYPE_CHAR:   {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Char);
                        break;
                    }
                    case TokenType::TYPE_BOOL:   {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool);
                        break;
                    }
                    case TokenType::TYPE_STRING: {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::String);
                        break;
                    }
                    case TokenType::TYPE_VOID: {
                        resultTyInfo = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Void);
                        break;
                    }
                    case TokenType::IDENTIFIER: {
                        auto* structDecl = curModule()->lookupStruct(token.content);
                        if (!structDecl) {
                            throw SakuraError(
                                OccurredTerm::IR_GENERATING,
                                "Unknown struct type: '" + token.content + "'",
                                token.info);
                        }
                        resultTyInfo = program.getContext().typeInfoPool().makeStructTypeID(
                            structDecl->getDeclId());
                        break;
                    }
                    default:
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Unsupported type modifier: '" + token.content + "'",
                            token.info);
                }

            }

            // 数组类型：维度从最内层开始包装，逆序遍历维度列表，
            // 使 T[a][b] 成为"长度为 a、元素为 T[b]"的数组。
            if (node->getTag() == ASTTag::ArrayTypeModifierNode) {
                auto headType = getTypeInfoFromNode((*node)[ASTTag::HeadExpr]);
                auto dims = (*node)[ASTTag::Exprs]->getChildren();

                TypeInfo* currentType = headType;
                for (auto it = dims.rbegin(); it != dims.rend(); it ++) {
                    currentType = program.getContext().typeInfoPool().makeArrayTypeID(
                        currentType,
                        getArrayDimension(*it)
                    );
                }

                resultTyInfo = currentType;
            }

            // 指针或引用：Op 表示单个引用修饰，Ops 表示一到多级指针修饰。
            // 二者都在 nullable 包装之后施加，因此若同时带有 nullable 标记即报错。
            if (node->hasNode(ASTTag::Op)) {
                // 判断是否为引用类型信息
                resultTyInfo = program.getContext().typeInfoPool().makeRefTypeID(resultTyInfo);

                if (node->hasNode(ASTTag::NullableTag)) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot wrap Ref Type as Nullable Type!",
                        (*node)[ASTTag::NullableTag]->getPosInfo()
                        );
                }

            }
            else if (node->hasNode(ASTTag::Ops)) {
                // 判断是否为指针类型信息
                auto ptrDepth = (*node)[ASTTag::Ops]->getChildren().size();
                for (std::size_t i = 0; i < ptrDepth; i ++)
                    resultTyInfo = program.getContext().typeInfoPool().makePointerTypeID(resultTyInfo);

                if (node->hasNode(ASTTag::NullableTag)) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot wrap Pointer Type as Nullable Type!",
                        (*node)[ASTTag::NullableTag]->getPosInfo()
                    );
                }
            }

            program.validateSemanticType(resultTyInfo, node->getPosInfo());
            return resultTyInfo;
        }

        // 把语义类型编码为函数名修饰中使用的稳定文本：nullable 加 "?"，数组加 "[N]"，
        // 指针加 "*"，引用加 "&"，结构体用"声明所属模块.结构体名"（来自 StructDeclId，避免跨模块重名冲突），
        // 基础类型用源语言关键字。函数类型等不支持的类型属于内部错误，抛 std::invalid_argument 而不是 SakuraError。
        fzlib::String mangleTypeName(const TypeInfo* type) {
            if (!type) throw std::invalid_argument("Missing semantic type");
            if (type->isNullable()) return mangleTypeName(type->getBase()) + "?";
            if (type->isStruct()) {
                const auto* declaration = program.resolveStruct(*type->getStructDeclId());
                if (!declaration) throw std::invalid_argument("Unknown semantic struct declaration");
                return type->getStructDeclId()->module + "." + declaration->getName();
            }
            if (type->isArray()) {
                const auto& array = std::get<ArrayTypeInfo>(type->getComplexTypeInfo());
                return mangleTypeName(array.getElementTy()) + "[" + std::to_string(array.length()) + "]";
            }
            if (type->isPointer()) return mangleTypeName(type->getPointeeType()) + "*";
            if (type->isRef()) return mangleTypeName(type->getPointeeType()) + "&";
            switch (type->getTypeID()) {
                case Int32: return "i32";
                case Int64: return "i64";
                case UInt32: return "ui32";
                case UInt64: return "ui64";
                case Float32: return "f32";
                case Float64: return "f64";
                case Bool: return "bool";
                case Char: return "char";
                case String: return "string";
                case Void: return "void";
                case TypeDescriptor: return "typeinfo";
                default: throw std::invalid_argument("Unsupported semantic signature type");
            }
        }

        // 函数名修饰：在原名后依次追加 "_<参数类型名>"，使同名不同参数的重载在模块作用域中拥有不同 IR 名称。
        // 无参函数保持原名（因此 main 仍然叫 "main"）。
        fzlib::String mangleFnName(fzlib::String name, const std::vector<TypeInfo*>& arguments) {
            for (const auto* type : arguments) name += "_" + mangleTypeName(type);
            return name;
        }

        // sizeof 只接受可参与算术提升的基础标量类型（见 semanticRank）以及指针类型；
        // string、数组、结构体、引用、nullable 等都会被拒绝。
        void validateSizeofType(const TypeInfo* ty, PositionInfo info) {
            if (semanticRank(ty) < 0 && !(ty && ty->isPointer())) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'sizeof' only supports char, i32, i64, ui32, ui64, bool, f32, f64, and pointer types.",
                    info
                );
            }
        }

        // 基础标量类型的提升等级：两个操作数取较高等级作为结果类型（类似 C 的常用算术转换，但更简化）。
        // 顺序为 bool < char < ui32 < i32 < ui64 < i64 < f32 < f64。
        // 返回 -1 表示不参与算术运算：空指针、nullable、复合类型，以及 String/Void/TypeDescriptor。
        static int semanticRank(const TypeInfo* type) {
            if (!type || type->isNullable() || !type->isBasic()) return -1;
            switch (type->getTypeID()) {
                case TypeID::Bool: return 1;
                case TypeID::Char: return 2;
                case TypeID::UInt32: return 3;
                case TypeID::Int32: return 4;
                case TypeID::UInt64: return 5;
                case TypeID::Int64: return 6;
                case TypeID::Float32: return 7;
                case TypeID::Float64: return 8;
                default: return -1;
            }
        }

        // 为 sizeof 静态推断表达式的语义类型：只遍历 AST，不生成任何 IR 指令，
        // 因此 sizeof 的操作数不会被求值。为保证这一点，会产生副作用或需要生成代码才能定型的表达式
        // （赋值、数组字面量、自增自减/取地址等前后缀运算、下标、调用、成员链）一律报错。
        // 推断规则与真正生成 IR 时保持一致：算术用 semanticRank 提升，比较/逻辑结果为 bool。
        TypeInfo* inferExprType(NodePtr node, PositionInfo info = {0, 0, "sizeof"}) {
            if (!node) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                  "Cannot infer the type of an empty expression.",
                                  info);
            }

            // 整体表达式：只允许加减表达式或逻辑二元表达式两种形态。
            if (node->getTag() == ASTTag::WholeExprNode) {
                if (node->hasNode(ASTTag::AddExprNode))
                    return inferExprType((*node)[ASTTag::AddExprNode], info);
                if (node->hasNode(ASTTag::BinaryExprNode))
                    return inferExprType((*node)[ASTTag::BinaryExprNode], info);
                if (node->hasNode(ASTTag::ArrayExprNode))
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                      "'sizeof' does not support array expressions.",
                                      node->getPosInfo());
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                  "'sizeof' does not support assignment expressions.",
                                  node->getPosInfo());
            }

            // 加减/乘除链：从左到右逐个按 semanticRank 提升，与 handleUnlogicalBinaryCalc 的规则相同。
            if (node->getTag() == ASTTag::AddExprNode ||
                node->getTag() == ASTTag::MulExprNode) {
                auto exprs = (*node)[ASTTag::Exprs]->getChildren();
                TypeInfo* result = inferExprType(exprs.front(), info);
                for (std::size_t i = 1; i < exprs.size(); ++i) {
                    auto rhs = inferExprType(exprs[i], info);
                    auto lhsRank = semanticRank(result);
                    auto rhsRank = semanticRank(rhs);
                    if (lhsRank < 0 || rhsRank < 0) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' expression contains an unsupported arithmetic type.",
                                          node->getPosInfo());
                    }
                    switch (std::max(lhsRank, rhsRank)) {
                        case 1: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool); break;
                        case 2: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Char); break;
                        case 3: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt32); break;
                        case 4: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32); break;
                        case 5: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64); break;
                        case 6: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int64); break;
                        case 7: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float32); break;
                        case 8: result = program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float64); break;
                        default:
                            throw SakuraError(OccurredTerm::IR_GENERATING,
                                              "Internal error: unhandled sizeof expression type.",
                                              node->getPosInfo());
                    }
                }
                return result;
            }

            // 比较与逻辑链：仅有单个操作数时类型透传；有运算符时仍递归检查每个操作数
            // （以便发现不支持的子表达式），结果固定为 bool。
            if (node->getTag() == ASTTag::LogicExprNode ||
                node->getTag() == ASTTag::BinaryExprNode) {
                auto exprs = (*node)[ASTTag::Exprs]->getChildren();
                TypeInfo* result = inferExprType(exprs.front(), info);
                if (node->hasNode(ASTTag::Ops)) {
                    for (auto expr : exprs)
                        inferExprType(expr, info);
                    return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool);
                }
                return result;
            }

            // 基本表达式：字面量取常量的语义类型；变量只允许单个裸标识符，取其声明的语义类型；
            // 内建可调用运算 sizeof 为 ui64、typeof 为 TypeDescriptor；括号表达式递归处理 HeadExpr。
            if (node->getTag() == ASTTag::PrimExprNode) {
                if (node->hasNode(ASTTag::Literal)) {
                    // Literal 子节点是一个 LiteralNode，需再取一层才能拿到字面量 token。
                    auto literal = Constant::getFromToken(
                        (*(*node)[ASTTag::Literal])[ASTTag::Literal]->getToken(), getTypeInfoContext());
                    return semanticTypeOf(literal);
                }
                if (node->hasNode(ASTTag::Identifier)) {
                    auto identifier = (*node)[ASTTag::Identifier];
                    if (identifier->hasNode(ASTTag::PreOp) ||
                        identifier->hasNode(ASTTag::Op)) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' does not evaluate side-effecting expressions.",
                                          identifier->getPosInfo());
                    }
                    auto chain = (*identifier)[ASTTag::Exprs]->getChildren();
                    if (chain.size() != 1) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' only supports a single variable expression.",
                                          identifier->getPosInfo());
                    }
                    auto atom = chain[0];
                    if (atom->hasNode(ASTTag::Ops) &&
                        !(*atom)[ASTTag::Ops]->getChildren().empty()) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' does not support indexed or called variables.",
                                          identifier->getPosInfo());
                    }
                    if (!atom->hasNode(ASTTag::Identifier)) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "'sizeof' could not resolve the variable identifier.",
                                          identifier->getPosInfo());
                    }
                    // 符号地址的语义类型即变量本身的类型。
                    return lookup((*atom)[ASTTag::Identifier]->getToken().content,
                                  identifier->getPosInfo())->address->getSemanticType();
                }
                if (node->hasNode(ASTTag::InnerCallableOpExprNode)) {
                    auto inner = (*node)[ASTTag::InnerCallableOpExprNode];
                    if (inner->hasNode(ASTTag::Sizeof))
                        return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64);
                    return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::TypeDescriptor);
                }
                return inferExprType((*node)[ASTTag::HeadExpr], info);
            }

            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "'sizeof' cannot infer the type of this expression.",
                              node->getPosInfo());
        }

        // 用于获取非逻辑二元运算（+ - * / %，以及自增自减、复合赋值）的结果类型：
        // 两侧都必须是可参与算术提升的基础标量类型，结果取 semanticRank 较高的一侧。
        // 不区分具体运算符，也不在此插入类型转换指令；转换由后端根据操作数类型处理。
        TypeInfo* handleUnlogicalBinaryCalc(IRValue* lhs, IRValue* rhs, PositionInfo info = {0, 0, "Normal Calc"}) {
            auto lTy = semanticTypeOf(lhs);
            auto rTy = semanticTypeOf(rhs);
            const int lRank = semanticRank(lTy);
            const int rRank = semanticRank(rTy);
            if (lRank < 0 || rRank < 0) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                        "Semantic types do not support '+', '-', '*', '%', and '/' operations",
                        info);
            }

            int resultRank = std::max(lRank, rRank);
            switch (resultRank) {
                case 1: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool);
                case 2: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Char);
                case 3: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt32);
                case 4: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32);
                case 5: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64);
                case 6: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int64);
                case 7: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float32);
                case 8: return program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Float64);
                default: break;
            }

            throw SakuraError(OccurredTerm::IR_GENERATING, "Internal error: unhandled type rank", info);
        }

        // 当前正在生成的函数与模块（由 Program/Module 的游标决定，均不做越界检查）。
        // 模块不会自动创建默认函数：新模块的函数游标为 -1，只有 buildFunction 之后 curFunc() 才有效。
        // 因此顶层只有函数/结构体定义是安全的；函数生成结束后游标仍停在该函数上。
        Function* curFunc() {
            return program.curMod()->curFunc();
        }

        Module* curModule() {
            return program.curMod();
        }
    public:
        // name 作为 Program 的名称；Program 构造时只创建运行时模块 __runtime，
        // 用户模块在 startGenerate 中创建（并自动 use 运行时模块）。
        IRGenerator(fzlib::String name): program(name) {}

        // 生成入口，每个实例只能调用一次（再次调用抛 std::logic_error）。
        // 流程：创建名为 moduleName 的模块 -> 词法分析并逐条解析顶层语句，保存到 parsedStatements
        // -> 预先登记所有结构体名（使结构体可以在定义前被引用、相互引用）-> 依次生成每条语句
        // -> 最后对每个结构体的语义类型做 validateSemanticType，确认字段类型均可解析。
        // 解析失败时直接抛出 parser 给出的错误对象。
        void startGenerate(const fzlib::String& source, fzlib::String moduleName);

        // 已解析的顶层语句 AST（startGenerate 之后有效），供 AST 调试输出与测试使用。
        const std::vector<NodePtr>& getParsedStatements() const {
            return parsedStatements;
        }

        // 生成结果；后续 lowering 与 LLVM codegen 都从这里取得 Program。
        Program& getProgram() {
            return program;
        }

        // 把 Program::toString() 输出的单行 IR 文本整理为带缩进的多行格式，仅用于调试显示。
        // 规则按字符扫描：'{' 换行并增加缩进，'}' 减少缩进并独占一行，';' 结束一条指令后换行，
        // '@'（基本块标签的前缀）若不紧跟在空格之后则另起一行。不识别字符串字面量中的这些字符。
        fzlib::String toFormatString() {
            fzlib::String raw = program.toString();
            fzlib::String result;
            int indent = 0;

            for (std::size_t i = 0; i < raw.len(); i++) {
                char c = raw[i];
                if (c == '{') {
                    result += " {\n";
                    indent++;
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                }
                else if (c == '}') {
                    result += "\n";
                    indent--;
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                    result += "}\n";
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                }
                else if (c == ';') {
                    result += ";\n";
                    for (int j = 0; j < indent * 2; j++) result += ' ';
                }
                else if (c == '@') {
                    if (result.len() > 0 && result[result.len() - 1] != ' ') {
                        result += "\n";
                        for (int j = 0; j < indent * 2; j++) result += ' ';
                    }
                    result += c;
                }
                else {
                    result += c;
                }
            }
            return result;
        }

        // --- Expression ---
        // 字面量 -> constant 指令。null 字面量没有自身类型，必须走带 expectedType 的
        // visitWholeExprNode 由 materializeNull 处理，这里直接报错。
        IRValue* visitLiteralNode(NodePtr node);
        // 把 null 物化为目标类型的空句柄常量；目标必须是 nullable 的结构体或数组类型。
        IRValue* materializeNull(TypeInfo* targetType, PositionInfo info);
        // 带期望类型的表达式入口：若表达式只是一个 null 字面量，则按 expectedType 物化 null；
        // 否则与不带期望类型的版本相同（expectedType 不参与其他表达式的推断）。
        IRValue* visitWholeExprNode(NodePtr node, TypeInfo* expectedType);
        // 下标运算：下标必须是整数；基值的引用会被剥开一层，可索引数组（得到元素类型）、
        // 指针（得到所指类型）和 string（得到 char）。结果为 indexing 左值指令。
        IRValue* visitIndexOpNode(IRValue* addr, NodePtr node);
        // 用已求值的实参生成 call 指令：检查被调值可调用、实参个数一致、每个实参可赋值给形参类型。
        // 例外：运行时的 print/println 接受任意装箱值，跳过实参类型检查（只要求实参有语义类型）。
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args);
        // 先以形参类型作为期望类型求值实参（以便实参可以写 null），再委托给上面的重载。
        IRValue* visitCallingOpNode(IRValue* addr, NodePtr node);
        // 标识符链中的单个原子：名称后若紧跟调用，则按名称与实参个数找候选函数并做重载选择——
        // 取需要隐式 nullable 包装的实参数最少者，并列则报二义；无匹配时退回按原始名查找运行时函数。
        // 否则按名称查符号得到地址，或处理括号内的嵌套标识符表达式；随后依次应用下标/调用运算。
        IRValue* visitAtomIdentifierNode(NodePtr node);
        // 完整标识符表达式：处理 a.b.c 成员链（每级生成 gmem 字段左值，并应用其后的下标/调用），
        // 再处理前缀运算（! ++ -- & ref *）与后缀运算（++ --）。
        // 没有前后缀运算时返回地址（左值），由调用方（如 visitPrimExprNode）决定是否 load。
        IRValue* visitIdentifierExprNode(NodePtr node);
        // 内建可调用运算：sizeof 静态推断类型、不求值操作数，生成携带类型操作数的 _sizeof（ui64）；
        // typeof 会求值唯一实参，生成 _typeof（TypeDescriptor）。两者都只接受一个实参。
        IRValue* visitInnerCallableExprNode(NodePtr node);
        // 基本表达式：字面量、内建可调用运算、标识符（若结果是左值则 load 出值）、括号表达式。
        IRValue* visitPrimExprNode(NodePtr node);
        // 乘除模链（左结合）；结果类型由 handleUnlogicalBinaryCalc 决定。
        IRValue* visitMulExprNode(NodePtr node);
        // 加减链（左结合）；结果类型由 handleUnlogicalBinaryCalc 决定。
        IRValue* visitAddExprNode(NodePtr node);
        // 比较链（< <= > >= == !=），结果为 bool；此处不检查操作数类型。
        IRValue* visitLogicExprNode(NodePtr node);
        // 逻辑 && / || 链，按短路语义展开为基本块：用临时 bool 变量保存结果（初值为 lhs），
        // 每个右操作数放在独立的 rhs 块中，由前一个块条件跳转进入或直接跳到汇合块，最后从临时变量 load 结果。
        IRValue* visitBinaryExprNode(NodePtr node);
        // 数组字面量：所有元素必须可赋值给首元素的类型；生成 create_array，类型为"首元素类型[元素个数]"。
        IRValue* visitArrayExprNode(NodePtr node);
        // 整体表达式分发：加减表达式 / 逻辑二元表达式 / 数组字面量 / 赋值表达式。
        IRValue* visitWholeExprNode(NodePtr node);
        // 类型修饰 -> TypeDescriptor 类型的 constant 指令，常量内容为解析出的 TypeInfo*。
        IRValue* visitTypeModifierNode(NodePtr node);
        // 赋值与复合赋值（= += -= *= /=）：右值以目标类型为期望类型求值；复合赋值展开为 load-运算-store。
        IRValue* visitAssignExprNode(NodePtr node);
        /* TODO：实现范围表达式
         * IRValue* visitRangeExprNode(NodePtr node);
         */

        // --- Statement ---
        // let 声明：至少要有类型或初始值之一；有显式类型时以其为准，否则取初始值的类型，最终调用 createAlloca。
        IRValue* visitDeclareStmtNode(NodePtr node);
        // 表达式语句：标识符表达式（如调用、自增）或赋值表达式。
        IRValue* visitExprStmtNode(NodePtr node);
        // if/else：先生成 then、else 与 merge 块，再回填条件跳转和各分支到 merge 的跳转。
        IRValue* visitIfStmtNode(NodePtr node);
        // while：prep（求条件）-> then（循环体）-> 回到 prep，条件为假跳到 merge；
        // 循环期间登记 continue 目标为 prep、break 目标为 merge。
        IRValue* visitWhileStmtNode(NodePtr node);
        // for：初始化声明放在当前块，并为其单独开一层作用域；cond -> body -> step -> cond，
        // 条件为假跳到 merge。continue 目标为 cond（不经过 step），break 目标为 merge。
        IRValue* visitForStmtNode(NodePtr node);
        // repeat N：用隐藏的 i32 计数变量（名称带 '$'，用户无法引用）从 0 计到 N，
        // 块结构为 prepare -> cond -> then -> step -> cond，结束跳到 merge。
        IRValue* visitRepeatStmtNode(NodePtr node);
        // match：对被匹配值 load 一次，各 case 依次比较相等；先生成全部 case/then 块，再统一回填跳转，
        // 未命中时进入 default（若有）或 merge。
        IRValue* visitMatchStmtNode(NodePtr node);
        // 代码块：新建名为 blockName 的基本块；beforeBlock != -1 时从该块插入一条跳到新块的 br。
        // 同时进入一层符号作用域并发出 enter/leave scope 标记指令。返回块的入口块；
        // 生成结束后游标停在块内最后生成的基本块上（调用方以此作为"出口块"回填跳转）。
        IRValue* visitBlockStmtNode(NodePtr node, fzlib::String blockName, long beforeBlock = -1);
        // 函数定义：解析形参与返回类型，修饰函数名并在模块中创建函数（自带 entry 块），
        // 在 entry 块中生成 param，函数体作为从 entry 跳入的新块生成；
        // 要求 main 返回 i32，且函数体内至少出现过一条 return。
        IRValue* visitFuncDefineStmtNode(NodePtr node);
        // 结构体定义：收集字段（禁止重名与 void 字段），默认值目前只允许 null，
        // 且只能用于 nullable 的结构体/数组字段；最后交给 Module::implStruct 完成声明。
        IRValue* visitStructDefineStmtNode(NodePtr node);
        // return：检查有无返回值与函数返回类型（void/非 void）是否一致、返回值是否可赋值给返回类型，
        // 并标记该函数已出现 return。
        IRValue* visitReturnStmtNode(NodePtr node);
        // break/continue：必须位于循环内，生成跳到当前最内层循环 break/continue 目标的 br。
        IRValue* visitBreakStmtNode(NodePtr node);
        IRValue* visitContinueStmtNode(NodePtr node);
        // 语句分发：剥开可能的 Stmt 包装节点后按标签分派到具体的 visit 方法。
        IRValue* visitStmt(NodePtr node);
    };
}

#endif /* !SAKURAE_GENERATOR_HPP */
