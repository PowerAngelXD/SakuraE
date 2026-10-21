#include "generator.hpp"
#include "Compiler/Error/error.hpp"
#include "Compiler/Frontend/AST.hpp"
#include "Compiler/Frontend/parser.hpp"
#include "Compiler/IR/Semantic/model/function.hpp"
#include "Compiler/IR/Semantic/model/instruction.hpp"
#include "Compiler/IR/Semantic/model/scope.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
#include "Compiler/IR/Semantic/value/array.hpp"
#include "Compiler/IR/Semantic/value/constant.hpp"
#include "Compiler/IR/Semantic/value/value.hpp"
#include "Compiler/Utils/Logger.hpp"
#include "includes/magic_enum.hpp"
#include <string>
#include <variant>

namespace sakuraE::IR {
    // 判断表达式节点是否就是一个 `null` 字面量。
    // 解析器会把字面量层层包在 WholeExpr/AddExpr/MulExpr/PrimExpr 等单子节点包装中，
    // 因此这里沿"只有一个子节点"的链递归向下，直到 LiteralNode 再检查 token 类型；
    // 任何一层出现多个子节点（即真正的运算表达式）都视为非 null 字面量。
    static bool isNullLiteralExpression(const NodePtr& node) {
        if (!node) return false;
        if (node->getTag() == ASTTag::LiteralNode && node->hasNode(ASTTag::Literal)) {
            return (*node)[ASTTag::Literal]->getToken().type == TokenType::NULL_LITERAL;
        }
        const auto children = node->getChildren();
        return children.size() == 1 && isNullLiteralExpression(children.front());
    }

    // 生成入口：把一份源码编译为一个新的 Semantic IR 模块。
    // 流程分为四遍：
    //   1. 词法 + 逐条语句解析，把 AST 缓存到 parsedStatements（生成器持有 AST 的生命周期）；
    //   2. 预声明所有结构体为 opaque 结构体，使后续类型解析可以前向引用/递归引用结构体；
    //   3. 按源码顺序访问每条语句生成 IR（结构体定义语句在此遍补全字段）；
    //   4. 所有结构体补全后，再对每个结构体的语义类型(TypeInfo)做整体递归校验。
    // 一个 IRGenerator 实例只允许生成一次，重复调用视为调用方逻辑错误。
    void IRGenerator::startGenerate(const fzlib::String& source, fzlib::String moduleName) {
        if (hasGenerated) {
            throw std::logic_error("IRGenerator supports one source generation per instance");
        }

        program.buildModule(moduleName, {1, 1, "Start of the whole program"});
        hasGenerated = true;
        parsedStatements.clear();

        // 解析失败时优先抛出解析器给出的 SakuraError；err 为空属于解析器内部错误。
        sakuraE::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        TokenIter current = tokens.cbegin();
        while (current->type != TokenType::_EOF_) {
            auto result = StatementParser::parse(current, tokens.cend());
            if (result.status == ParseStatus::FAILED) {
                if (!result.err) {
                    throw std::runtime_error("Error: Parse failed with NULL error object at token: ");
                }
                throw *result.err;
            }

            parsedStatements.push_back(result.val->genResource());
            current = result.end;
        }

        // 第二遍：仅登记结构体名字（opaque 结构体），字段留待 visitStructDefineStmtNode 补全。
        for (const auto& node : parsedStatements) {
            NodePtr statement = node->getTag() == ASTTag::Stmt ? (*node)[ASTTag::Stmt] : node;
            if (statement->getTag() != ASTTag::StructDefineStmtNode) {
                continue;
            }

            const auto nameToken = (*statement)[ASTTag::Identifier]->getToken();
            curModule()->declareStruct(nameToken.content, nameToken.info);
        }

        for (const auto& node : parsedStatements) {
            visitStmt(node);
        }
        // 第四遍：此时所有结构体都已补全，validateSemanticType 会递归检查字段类型是否
        // 都属于本 Program 的 TypeInfoContext、且引用的结构体声明都能解析。
        for (const auto& node : parsedStatements) {
            NodePtr statement = node->getTag() == ASTTag::Stmt ? (*node)[ASTTag::Stmt] : node;
            if (statement->getTag() == ASTTag::StructDefineStmtNode) {
                const auto token = (*statement)[ASTTag::Identifier]->getToken();
                const auto* declaration = curModule()->lookupStruct(token.content);
                auto* type = program.getContext().typeInfoPool().makeStructTypeID(declaration->getDeclId());
                program.validateSemanticType(type, token.info);
            }
        }
    }

    // 字面量 -> `constant` 指令：把 token 转为 Constant，并以其语义类型作为指令类型。
    // `null` 在这里没有目标类型可用，必须经由带 expectedType 的 visitWholeExprNode 处理，
    // 因此在此直接报错。
    IRValue* IRGenerator::visitLiteralNode(NodePtr node) {
        auto token = (*node)[ASTTag::Literal]->getToken();
        if (token.type == TokenType::NULL_LITERAL) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "null requires a target nullable struct type.",
                token.info);
        }

        auto literal = Constant::getFromToken(token, getTypeInfoContext());
        auto semanticType = semanticTypeOf(literal);

        return curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::constant,
                semanticType,
                {literal},
                "const." + literal->toString()
            );
    }

    // 为 `null` 字面量物化一个空句柄常量。
    // 约束：目标类型必须存在、必须是可空类型，且其底层类型只能是结构体或数组
    // （与 getTypeInfoFromNode 中"只有结构体/数组可空"的规则一致）。
    IRValue* IRGenerator::materializeNull(TypeInfo* targetType, PositionInfo info) {
        if (!targetType) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null requires a target type.", info);
        }
        if (!targetType->isNullable()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null can only initialize a nullable type.", info);
        }
        const auto* baseType = targetType->getBase();
        if (!baseType->isStruct() && !baseType->isArray()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                              "null can only initialize a nullable struct or array type.", info);
        }

        auto* constant = Constant::getNullHandle(targetType, info);
        return curFunc()->curBlock()->createInstruction(
            OpKind::constant,
            targetType,
            {constant},
            "null"
        );
    }

    // 带期望类型的整体表达式访问：仅用于给 `null` 字面量提供目标类型。
    // 非 null 表达式直接走普通 visitWholeExprNode，expectedType 被忽略（不做隐式转换）；
    // null 表达式则沿单子节点包装找到 LiteralNode，以其位置信息报错/物化。
    IRValue* IRGenerator::visitWholeExprNode(NodePtr node, TypeInfo* expectedType) {
        if (!isNullLiteralExpression(node)) return visitWholeExprNode(node);
        NodePtr current = node;
        while (current->getTag() != ASTTag::LiteralNode) {
            current = current->getChildren().front();
        }
        return materializeNull(expectedType,
                               (*current)[ASTTag::Literal]->getToken().info);
    }

    // 下标运算 `addr[index]`：addr 为被索引对象的地址（左值），返回 `indexing` 左值指令。
    // 下标只接受 i32/i64/ui32/ui64；先剥掉一层引用类型，然后按底层类型确定元素类型：
    // 数组 -> 元素类型，指针 -> 指向类型，string -> char；其余（或元素为 void）不可索引。
    IRValue* IRGenerator::visitIndexOpNode(IRValue* addr, NodePtr node) {
        auto* indexValue = visitAddExprNode((*node)[ASTTag::HeadExpr]);
        const auto* indexType = indexValue->getSemanticType();
        if (!indexType || (indexType->getTypeID() != Int32 && indexType->getTypeID() != Int64
            && indexType->getTypeID() != UInt32 && indexType->getTypeID() != UInt64)) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Array index must be an integer.", node->getPosInfo());
        }
        const auto* base = addr->getSemanticType();
        if (base && base->isRef()) base = base->getPointeeType();
        TypeInfo* element = nullptr;
        if (base && base->isArray()) element = base->getElementType();
        else if (base && base->isPointer()) element = base->getPointeeType();
        else if (base && base->getTypeID() == String) {
            element = program.getContext().typeInfoPool().makeBasicTypeID(Char);
        }
        if (!element || element->getTypeID() == Void) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Cannot index this semantic type.", node->getPosInfo());
        }
        // 结果是左值：后续可继续被 load/store，或被 isInteriorGCManagedLValue 检查。
        return curFunc()->curBlock()->createInstruction(
            OpKind::indexing, element, {addr, indexValue}, "indexing." + addr->getName());
    }

    // 调用的公共收尾：参数已求值完毕，校验签名后生成 `call` 指令。
    // addr 必须带函数类型；参数个数必须与签名一致；每个实参都需可赋值给形参类型。
    // 例外：运行时 print/println 接受任意装箱值，因此跳过实参类型检查，只要求有语义类型。
    // 注意 `call` 的操作数只有实参，被调函数靠指令名 "call.<函数名>" 传递给代码生成。
    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node, const std::vector<IRValue*>& args) {
        const auto* type = addr->getSemanticType();
        const auto* signature = type ? type->getFunctionTypeInfo() : nullptr;
        if (!signature) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Value is not semantically callable.", node->getPosInfo());
        }
        auto* function = dynamic_cast<Function*>(addr);
        const bool boxedPrint = function && (function->getRawName() == "print" || function->getRawName() == "println");
        if (signature->getArgCount() != args.size()) {
            throw SakuraError(OccurredTerm::IR_GENERATING, "Function argument count does not match its declaration.", node->getPosInfo());
        }
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (!args[index]->getSemanticType()
                || (!boxedPrint && !isAssignableTo(args[index]->getSemanticType(), signature->getArgTypes()[index]))) {
                throw SakuraError(OccurredTerm::IR_GENERATING, "Function argument has an incompatible semantic type.", node->getPosInfo());
            }
        }
        auto* result = signature->getReturnType();
        return curFunc()->curBlock()->createInstruction(
            OpKind::call, result, args, "call." + addr->getName());
    }

    // 对已求值得到的可调用值（例如调用结果）再进行调用：
    // 按签名为每个实参提供期望类型（以便 `null` 实参物化），超出形参个数的实参无期望类型；
    // 实参个数不匹配统一交由三参数版本 visitCallingOpNode 报错。
    IRValue* IRGenerator::visitCallingOpNode(IRValue* addr, NodePtr node) {
        std::vector<IRValue*> arguments;
        const auto* type = addr->getSemanticType();
        const auto* signature = type ? type->getFunctionTypeInfo() : nullptr;
        if (!signature) throw SakuraError(OccurredTerm::IR_GENERATING, "Value is not semantically callable.", node->getPosInfo());
        auto expressions = (*node)[ASTTag::Exprs]->getChildren();
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            arguments.push_back(visitWholeExprNode(expressions[index],
                index < signature->getArgCount() ? signature->getArgTypes()[index] : nullptr));
        }
        return visitCallingOpNode(addr, node, arguments);
    }

    // 访问原子标识符（成员访问链中的一环）：标识符本身、或括号内的标识符表达式，
    // 后接零个或多个下标/调用后缀。返回值通常是地址（左值），由调用方决定是否 load。
    IRValue* IRGenerator::visitAtomIdentifierNode(NodePtr node) {
        IRValue* currentAddr = nullptr;

        if (node->hasNode(ASTTag::Identifier)) {
            auto name = (*node)[ASTTag::Identifier]->getToken().content;
            auto ops = (*node)[ASTTag::Ops]->getChildren();

            // 情况一：`name(args)...` 形式的直接调用，需要在这里做重载解析。
            if (!ops.empty() && ops[0]->getTag() == ASTTag::CallingOpNode) {
                std::vector<IRValue*> argValues;
                auto argExprs = (*ops[0])[ASTTag::Exprs]->getChildren();
                // 候选函数：同名且参数个数相同的已改编函数（名字形如 name_<类型...>）。
                // 仅当候选唯一时才把它的形参类型作为实参的期望类型（用于物化 null）；
                // 多个候选时无法确定期望类型，实参按自身类型求值。
                auto candidates = curModule()->findFunctionCandidates(name, argExprs.size());
                const auto* expected = candidates.size() == 1 ? candidates[0]->getFunctionTypeInfo() : nullptr;
                for (std::size_t index = 0; index < argExprs.size(); ++index) {
                    argValues.push_back(visitWholeExprNode(argExprs[index],
                        expected ? expected->getArgTypes()[index] : nullptr));
                }
                // 重载打分：每个实参必须可赋值给形参；类型不完全相同（如非空 -> 可空）计 1 分，
                // 选总分最低者。同分出现两个则记为二义；之后若出现更低分则清除二义标记。
                // bestScore 初值为 实参数+1，保证任何匹配的候选都能胜出。
                Function* callee = nullptr;
                std::size_t bestScore = argValues.size() + 1;
                bool ambiguous = false;
                for (auto* candidate : candidates) {
                    const auto* signature = candidate->getFunctionTypeInfo();
                    if (!signature) continue;
                    std::size_t score = 0;
                    bool matches = true;
                    for (std::size_t index = 0; index < argValues.size(); ++index) {
                        auto* actual = argValues[index]->getSemanticType();
                        auto* required = signature->getArgTypes()[index];
                        if (!isAssignableTo(actual, required)) { matches = false; break; }
                        if (actual != required) ++score;
                    }
                    if (!matches) continue;
                    if (score < bestScore) { callee = candidate; bestScore = score; ambiguous = false; }
                    else if (score == bestScore) ambiguous = true;
                }
                if (ambiguous) throw SakuraError(OccurredTerm::IR_GENERATING, "Ambiguous semantic function overload.", node->getPosInfo());
                // 没有匹配的改编函数时，回退到按原始名查找运行时函数（如 print/println，
                // 它们接受任意装箱值，没有按参数类型改编的重载）。
                if (!callee) callee = curModule()->lookupRuntimeFunction(name);
                if (!callee) throw SakuraError(OccurredTerm::IR_GENERATING, "No compatible function overload: " + name, node->getPosInfo());
                currentAddr = visitCallingOpNode(callee, ops[0], argValues);

                // 第一个调用后缀已处理，剩余后缀依次作用于调用结果。
                for (size_t i = 1; i < ops.size(); ++i) {
                    if (ops[i]->getTag() == ASTTag::IndexOpNode)
                        currentAddr = visitIndexOpNode(currentAddr, ops[i]);
                    else
                        currentAddr = visitCallingOpNode(currentAddr, ops[i]);
                }
                return currentAddr;
            }
            // 情况二：普通变量引用，查找到其地址（作用域中登记的 alloca/param 等）。
            else {
                currentAddr = lookup(name, node->getPosInfo())->address;
            }
        }
        else if (node->hasNode(ASTTag::IdentifierExprNode)) {
            currentAddr = visitIdentifierExprNode((*node)[ASTTag::IdentifierExprNode]);
        }

        // 依次应用下标/调用后缀（直接调用分支已提前返回，不会走到这里）。
        if (node->hasNode(ASTTag::Ops)) {
            auto ops = (*node)[ASTTag::Ops]->getChildren();
            for (auto op : ops) {
                if (op->getTag() == ASTTag::IndexOpNode)
                    currentAddr = visitIndexOpNode(currentAddr, op);
                else if (op->getTag() == ASTTag::CallingOpNode)
                    currentAddr = visitCallingOpNode(currentAddr, op);
            }
        }

        return currentAddr;
    }

    // 访问完整的标识符表达式：`a.b[i].c(...)` 形式的成员访问链，外加可选的前缀/后缀运算。
    // 返回值约定：无运算时返回地址（左值），由 visitPrimExprNode 等调用方决定是否 load；
    // 有前缀/后缀运算时返回运算产生的值（或 store 指令）。
    IRValue* IRGenerator::visitIdentifierExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* resultAddr = visitAtomIdentifierNode(chain[0]);
        // 对链上后续每一环做字段访问：基对象（可空类型先取底层类型）必须是结构体，
        // 字段类型从结构体声明中查得，生成 `gmem` 左值指令（字段名作为字符串常量操作数）。
        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto name = (*chain[i])[ASTTag::Identifier]->getToken().content;

            auto* baseType = resultAddr->getSemanticType();
            if (!baseType || !baseType->getBase()->isStruct()) {
                throw SakuraError(OccurredTerm::IR_GENERATING, "Member access requires a struct type.", chain[i]->getPosInfo());
            }
            TypeInfo* resultSemanticTy = nullptr;
            if (const auto* decl = program.resolveStruct(*baseType->getBase()->getStructDeclId());
                decl && decl->find(name)) {
                resultSemanticTy = decl->find(name)->type;
            }

            if (!resultSemanticTy) throw SakuraError(OccurredTerm::IR_GENERATING, "Unknown struct member: " + name, chain[i]->getPosInfo());

            resultAddr = curFunc()
                ->curBlock()
                ->createInstruction(
                    OpKind::gmem,
                    resultSemanticTy,
                    {resultAddr, Constant::get(name, getTypeInfoContext(), (*chain[i])[ASTTag::Identifier]->getToken().info)},
                    "gmem." + name
                );

            // 字段访问后的下标/调用后缀；其他标签忽略。
            if (chain[i]->hasNode(ASTTag::Ops)) {
                for (auto op: (*chain[i])[ASTTag::Ops]->getChildren()) {
                    switch (op->getTag()) {
                        case ASTTag::IndexOpNode: {
                            resultAddr = visitIndexOpNode(resultAddr, op);
                            break;
                        }
                        case ASTTag::CallingOpNode: {
                            resultAddr = visitCallingOpNode(resultAddr, op);
                            break;
                        }
                        default: break;
                    }
                }
            }
        }

        IRValue* resultValue = resultAddr;
        // 前缀运算符。
        if (node->hasNode(ASTTag::PreOp)) {
            auto preOp = (*node)[ASTTag::PreOp]->getToken();
            switch (preOp.type) {
                // `!x`：要求操作数为 bool，先 load 再取反。
                case TokenType::LGC_NOT: {
                    const auto* operandType = resultAddr->getSemanticType();
                    if (!operandType || operandType->getTypeID() != TypeID::Bool) {
                        throw SakuraError(OccurredTerm::IR_GENERATING,
                                          "Logical not requires a bool operand.", preOp.info);
                    }
                    resultValue = createLoad(resultAddr, preOp.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::lgc_not,
                            program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                            {resultValue},
                            "lgc_not." + resultValue->getName()
                        );
                    break;
                }
                // `++x`：load -> +1 -> store，返回 store 指令（前缀自增不产生可继续使用的值）。
                // 结果类型按数值提升规则(handleUnlogicalBinaryCalc)计算；若提升后类型与变量不同，
                // createStore 会因类型不兼容报错。
                case TokenType::AINC: {
                    resultValue = createLoad(resultAddr, preOp.info);

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::add,
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1, getTypeInfoContext())),
                            {resultValue, Constant::get(1, getTypeInfoContext())},
                            "add"
                        );

                    return createStore(resultAddr, resultValue, preOp.info);
                    break;
                }
                // `--x`：与 `++x` 相同，只是使用减法。
                case TokenType::SDEC: {
                    resultValue = createLoad(resultAddr, preOp.info);
                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::sub,
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1, getTypeInfoContext())),
                            {resultValue, Constant::get(1, getTypeInfoContext())},
                            "sub"
                        );

                    return createStore(resultAddr, resultValue, preOp.info);
                    break;
                }
                // `&x`：取地址，得到指向 x 语义类型的指针。只允许对左值取地址，
                // 且禁止取得 GC 托管对象的内部地址（数组元素、字符串字符等）：
                // 编译器不会把派生的内部地址登记为 GC root，仅持有该地址无法让对象保持存活，
                // 对象被回收后地址即悬空，因此不能作为稳定的语言级指针。
                case TokenType::AND: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            ensureAddressable(resultAddr, node->getPosInfo(), false);
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    program.getContext().typeInfoPool().makePointerTypeID(resultAddr->getSemanticType()),
                                    {resultAddr},
                                    "gaddr." + resultAddr->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the address of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                // `ref x`：取引用，规则同 `&x`，但结果为引用类型。
                case TokenType::KEYWORD_REF: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            ensureAddressable(resultAddr, node->getPosInfo(), true);
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::gaddr,
                                    program.getContext().typeInfoPool().makeRefTypeID(resultAddr->getSemanticType()),
                                    {resultAddr},
                                    "gaddr.ref." + resultAddr->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the reference of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                // `*p`：解引用。p 必须是指针类型的左值：先 load 出指针值，
                // 再生成 `deref` 左值指令（类型为指向类型），因此 `*p = v` 可以继续赋值。
                // 不允许解引用 void 指针。
                case TokenType::MUL: {
                    if (auto inst = dynamic_cast<Instruction*>(resultAddr)) {
                        if (inst->isLValue()) {
                            if (!resultAddr->getSemanticType() || !resultAddr->getSemanticType()->isPointer()) {
                                throw SakuraError(
                                    OccurredTerm::IR_GENERATING,
                                    "Cannot deref a non pointer identifier.",
                                    node->getPosInfo()
                                );
                            }
                            auto load = createLoad(resultAddr, preOp.info);
                            auto* pointee = resultAddr->getSemanticType()->getPointeeType();
                            if (pointee->getTypeID() == Void) throw SakuraError(OccurredTerm::IR_GENERATING, "Cannot dereference void pointer.", node->getPosInfo());
                            return curFunc()
                                ->curBlock()
                                ->createInstruction(
                                    OpKind::deref,
                                    pointee,
                                    {load},
                                    "deref." + load->getName()
                                );
                        }
                    }
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot take the address of an rvalue",
                                    node->getPosInfo());
                    break;
                }
                default:
                    break;
            }
        }

        // 后缀运算符 `x++`/`x--`：同样 load -> 运算 -> store，但返回的是运算后的新值
        // （而非旧值），这与 C 语义不同。
        if (node->hasNode(ASTTag::Op)) {
            auto op = (*node)[ASTTag::Op]->getToken();
            IRValue* oldValue = nullptr;
            switch (op.type) {
                case TokenType::AINC: {
                    resultValue = createLoad(resultAddr, op.info);
                    oldValue = resultValue;

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::add,
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1, getTypeInfoContext())),
                            {resultValue, Constant::get(1, getTypeInfoContext())},
                            "add"
                        );

                    createStore(resultAddr, resultValue, op.info);
                    break;
                }
                case TokenType::SDEC: {
                    resultValue = createLoad(resultAddr, op.info);
                    oldValue = resultValue;

                    resultValue = curFunc()
                        ->curBlock()
                        ->createInstruction(
                            OpKind::sub,
                            handleUnlogicalBinaryCalc(resultAddr, Constant::get(1, getTypeInfoContext())),
                            {resultValue, Constant::get(1, getTypeInfoContext())},
                            "sub"
                        );

                    createStore(resultAddr, resultValue, op.info);
                    break;
                }
                default:
                    break;
            }
            return oldValue;
        }

        return resultValue;
    }

    // 内建可调用运算符 sizeof/typeof。
    IRValue* IRGenerator::visitInnerCallableExprNode(NodePtr node) {
        auto callingNode = (*node)[ASTTag::CallingOpNode];

        // sizeof 只做静态类型推导（inferExprType），不对实参求值，因此不产生副作用。
        // `_sizeof` 指令没有操作数，被测类型通过 type operand 附加，
        // 代码生成阶段按该类型的 storage 类型(IRType)在 DataLayout 中的分配大小求值，结果为 ui64。
        if (node->hasNode(ASTTag::Sizeof)) {
            auto exprs = (*callingNode)[ASTTag::Exprs]->getChildren();
            if (exprs.size() != 1) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'sizeof' operator requires exactly one argument!",
                    node->getPosInfo()
                );
            }

            auto argType = inferExprType(exprs.front(), exprs.front()->getPosInfo());
            validateSizeofType(argType, exprs.front()->getPosInfo());
            auto sizeofValue = curFunc()->
                curBlock()->
                createInstruction(
                    OpKind::_sizeof,
                    program.getContext().typeInfoPool().makeBasicTypeID(TypeID::UInt64),
                    {},
                    "sizeof"
                );
            static_cast<Instruction*>(sizeofValue)->setTypeOperand(argType);
            return sizeofValue;
        }

        // typeof 需要真正对实参求值（副作用会发生），结果为 TypeDescriptor（运行时类型元数据）。
        std::vector<IRValue*> args;
        for (auto argNode: (*callingNode)[ASTTag::Exprs]->getChildren()) {
            args.push_back(visitWholeExprNode(argNode));
        }

        if (node->hasNode(ASTTag::Typeof)) {
            if (args.size() != 1) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "'typeof' Operator only support one argument!",
                    node->getPosInfo()
                );
            }

            return curFunc()->
                curBlock()->
                createInstruction(
                    OpKind::_typeof,
                    program.getContext().typeInfoPool().makeBasicTypeID(TypeDescriptor),
                    args,
                    "typeof"
                );
        }
        else throw SakuraError(
            OccurredTerm::IR_GENERATING,
            "Unknown InnerCallable Operator",
            node->getPosInfo()
        );
    }

    // 基本表达式：字面量、sizeof/typeof、标识符表达式或括号表达式。
    // 标识符表达式若得到左值则在此 load 成右值——这是"地址 -> 值"转换的统一位置。
    IRValue* IRGenerator::visitPrimExprNode(NodePtr node) {
        if (node->hasNode(ASTTag::Literal)) {
            return visitLiteralNode((*node)[ASTTag::Literal]);
        }
        else if (node->hasNode(ASTTag::InnerCallableOpExprNode)) {
            return visitInnerCallableExprNode((*node)[ASTTag::InnerCallableOpExprNode]);
        }
        else if (node->hasNode(ASTTag::Identifier)) {
            auto result = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
            if (auto inst = dynamic_cast<Instruction*>(result)) {
                if (inst->isLValue()) return createLoad(result, node->getPosInfo());
            }
            return result;
        }
        else {
            return visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        }
    }

    // 乘除模表达式：左结合地依次处理 `*`、`/`、`%`，结果类型按数值提升规则计算。
    IRValue* IRGenerator::visitMulExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* lhs = visitPrimExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitPrimExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::MUL: {
                        lhs = curFunc()
                            ->curBlock()
                            ->createInstruction(OpKind::mul,
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "mul");
                        break;
                    }
                    case TokenType::DIV: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::div,
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "div");
                        break;
                    }
                    case TokenType::MOD: {
                        lhs = curFunc()
                                ->curBlock()
                                ->createInstruction(OpKind::mod,
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "mod");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    // 加减表达式：左结合地依次处理 `+`、`-`，结果类型按数值提升规则计算。
    IRValue* IRGenerator::visitAddExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* lhs = visitMulExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitMulExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::ADD: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::add,
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "add");
                        break;
                    }
                    case TokenType::SUB: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::sub,
                                                handleUnlogicalBinaryCalc(lhs, rhs),
                                                {lhs, rhs}, "sub");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    // 比较表达式：`< <= > >= == !=`，结果恒为 bool。
    // 注意此处不检查左右操作数的语义类型是否可比较。
    IRValue* IRGenerator::visitLogicExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* lhs = visitAddExprNode(chain[0]);

        if (node->hasNode(ASTTag::Ops)) {
            auto opChain = (*node)[ASTTag::Ops]->getChildren();

            for (std::size_t i = 1; i < chain.size(); i ++) {
                IRValue* rhs = visitAddExprNode(chain[i]);

                switch (opChain[i - 1]->getToken().type)
                {
                    case TokenType::LGC_LS_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_ls_than,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_ls_than");
                        break;
                    }
                    case TokenType::LGC_LSEQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_ls_than,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_ls_than");
                        break;
                    }
                    case TokenType::LGC_MR_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_mr_than,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_mr_than");
                        break;
                    }
                    case TokenType::LGC_MREQU_THAN: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_eq_mr_than,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_eq_mr_than");
                        break;
                    }
                    case TokenType::LGC_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_equal,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_equal");
                        break;
                    }
                    case TokenType::LGC_NOT_EQU: {
                        lhs = curFunc()
                                ->curBlock()
                            ->createInstruction(OpKind::lgc_not_equal,
                                                program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                                {lhs, rhs}, "lgc_not_equal");
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        return lhs;
    }

    // 逻辑表达式 `&&`/`||`：两者同优先级、从左到右结合，即 `a || b && c` 等价于 `(a || b) && c`。
    // 用一个 bool 临时变量 tbv.N（初值为 lhs）保存累积结果，每个右操作数对应一对块：
    //   tbv.N.rhs.i：求第 i 个操作数并写入临时变量；
    //   tbv.N.join.i：load 累积结果，作为下一步的判断点。
    // 不变量：进入第 i 步时，临时变量中保存的是前 i 个操作数从左到右的计算结果。
    //   &&：累积结果为真才进入 rhs 块，否则跳到 join 块（结果保持 false）；
    //   ||：累积结果为假才进入 rhs 块，否则跳到 join 块（结果保持 true）。
    // 最后一个 join 块即汇合点，后续代码继续生成在其中。
    IRValue* IRGenerator::visitBinaryExprNode(NodePtr node) {
        auto chain = (*node)[ASTTag::Exprs]->getChildren();
        IRValue* lhs = visitLogicExprNode(chain[0]);

        if (!node->hasNode(ASTTag::Ops)) {
            return lhs;
        }

        static int binaryID = 0;
        fzlib::String resultAddrName = "tbv." + std::to_string(binaryID);
        binaryID ++;
        IRValue* resultAddr = createAlloca(resultAddrName,
                                           program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Bool),
                                           lhs, node->getPosInfo());

        auto opChain = (*node)[ASTTag::Ops]->getChildren();
        // testBlockIndex：本步要挂条件跳转的块；第一步是 lhs 求值完成后的块，之后是上一步的 join 块。
        // cond：本步的条件值，只在 testBlock 中定义和使用，不跨块引用，从而满足支配关系。
        long tmpBlockIndex = curFunc()->cur();
        IRValue* cond = lhs;

        for (std::size_t i = 1; i < chain.size(); i ++) {
            const auto& opToken = opChain[i - 1]->getToken();

            bool isAnd = opToken.type == TokenType::LGC_AND;
            fzlib::String suffix = std::to_string(i);

            // buildBlock 会把游标移到新块，因此下标需在建块后立即记录
            IRValue* rhsBlock = curFunc()->buildBlock(resultAddrName + ".rhs." + suffix);
            long rhsBlockIndex = curFunc()->cur();
            IRValue* joinBlock = curFunc()->buildBlock(resultAddrName + ".join." + suffix);
            long joinBlockIndex = curFunc()->cur();

            // 在求右操作数之前挂条件跳转：testBlock 此时一定尚无终结指令
            if (isAnd) {
                curFunc()->block(tmpBlockIndex)->createCondBr(cond, rhsBlock, joinBlock);
            }
            else {
                curFunc()->block(tmpBlockIndex)->createCondBr(cond, joinBlock, rhsBlock);
            }

            // 右操作数可能自行建块（如嵌套短路），store 与 br 都必须写在其求值完成后的当前块
            curFunc()->moveCursor(rhsBlockIndex);
            IRValue* rhs = visitLogicExprNode(chain[i]);
            createStore(resultAddr, rhs, opToken.info);
            curFunc()->curBlock()->createBr(joinBlock);

            // join 块中重新 load 累积结果，作为下一步的条件
            curFunc()->moveCursor(joinBlockIndex);
            cond = curFunc()
                        ->curBlock()
                        ->createInstruction(OpKind::load,
                                            resultAddr->getSemanticType(),
                                            {resultAddr}, "load." + resultAddrName + "." + suffix);
            tmpBlockIndex = joinBlockIndex;
        }

        // 游标停在最后一个 join 块，cond 即整条链的结果
        return cond;
    }

    // 数组字面量 `[e0, e1, ...]`：以首元素的语义类型作为元素类型，
    // 其余元素必须可赋值给该类型；结果是长度为元素个数的数组类型的 `create_array` 指令。
    IRValue* IRGenerator::visitArrayExprNode(NodePtr node) {
        std::vector<IRValue*> rawArray;

        auto chain = (*node)[ASTTag::Exprs]->getChildren();

        IRValue* head = visitWholeExprNode(chain[0]);
        rawArray.push_back(head);

        for (std::size_t i = 1; i < chain.size(); i ++) {
            auto element = visitWholeExprNode(chain[i]);
            if (!isAssignableTo(element->getSemanticType(), head->getSemanticType())) {
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                "The types of elements in an array literal must be the same.",
                                node->getPosInfo());
            }
            rawArray.push_back(element);
        }

        IRArray* irArr = IRArray::createArray(rawArray, node->getPosInfo());
        Constant* arrConstant = Constant::get(irArr, irArr->getInfo());

        return curFunc()
                    ->curBlock()
                    ->createInstruction(OpKind::create_array,
                                         program.getContext().typeInfoPool().makeArrayTypeID(head->getSemanticType(), rawArray.size()),
                                         {arrConstant},
                                        "create-array");
    }

    // 赋值表达式：`=`、`+=`、`-=`、`*=`、`/=`。
    // 目标通过 visitIdentifierExprNode 得到地址（左值），右值以目标类型作为期望类型求值，
    // 使 `x = null` 能对可空目标物化空句柄。
    // 复合赋值为 load -> 运算 -> store；运算结果经数值提升后若与目标类型不同，createStore 会报类型不兼容。
    // 返回值为最终的 store 指令。
    IRValue* IRGenerator::visitAssignExprNode(NodePtr node) {
        IRValue* resultAddr = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        IRValue* value = visitWholeExprNode((*node)[ASTTag::HeadExpr], resultAddr->getSemanticType());
        auto op = (*node)[ASTTag::Op]->getToken();

        IRValue* resultValue = resultAddr;

        switch (op.type) {
            case TokenType::ASSIGN_OP: {
                resultValue = createStore(resultAddr, value, op.info);
                break;
            }
            case TokenType::ADD_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::add,
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "add"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::SUB_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::sub,
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "sub"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::MUL_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::mul,
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "mul"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            case TokenType::DIV_ASSIGN: {
                resultValue = createLoad(resultAddr, op.info);
                resultValue = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::div,
                        handleUnlogicalBinaryCalc(resultValue, value),
                        {resultValue, value},
                        "div"
                    );
                resultValue = createStore(resultAddr, resultValue, op.info);
                break;
            }
            default:
                break;
        }

        return resultValue;
    }

    // 整体表达式分发：加减表达式 / 逻辑二元表达式 / 数组字面量 / 赋值表达式。
    IRValue* IRGenerator::visitWholeExprNode(NodePtr node) {
        if (!node) return nullptr;

        if (node->hasNode(ASTTag::AddExprNode)) {
            return visitAddExprNode((*node)[ASTTag::AddExprNode]);
        }
        else if (node->hasNode(ASTTag::BinaryExprNode)) {
            return visitBinaryExprNode((*node)[ASTTag::BinaryExprNode]);
        }
        else if (node->hasNode(ASTTag::ArrayExprNode)) {
            return visitArrayExprNode((*node)[ASTTag::ArrayExprNode]);
        }
        else {
            return visitAssignExprNode((*node)[ASTTag::AssignExprNode]);
        }
    }

    // 类型修饰节点 -> 一条 TypeDescriptor 类型的 `constant` 指令，常量内容为解析得到的 TypeInfo*。
    // 调用方（如 visitDeclareStmtNode）会从该常量中取回 TypeInfo。
    IRValue* IRGenerator::visitTypeModifierNode(NodePtr node) {
        return curFunc()
            ->curBlock()
            ->createInstruction(OpKind::constant,
                                program.getContext().typeInfoPool().makeBasicTypeID(TypeDescriptor),
                                {Constant::get(getTypeInfoFromNode(node))},
                                "constant");
    }

    // 语句

    // 变量声明 `let name: T = init`：
    // - 有类型标注时先解析出声明的语义类型（visitTypeModifierNode 会额外生成一条 TypeDescriptor 常量指令，
    //   这里从其操作数中取回 TypeInfo*）；
    // - 初值以声明类型作为期望类型求值，使 `let x: T? = null` 可以物化空句柄；
    // - 类型与初值都缺失时无法确定变量类型，报错；
    // - 最终类型优先取声明类型，否则取初值的语义类型；兼容性检查与默认初值由 createAlloca 负责。
    IRValue* IRGenerator::visitDeclareStmtNode(NodePtr node) {
        auto identifier = (*node)[ASTTag::Identifier]->getToken();
        IRValue* typeInfoIRValue = nullptr;
        TypeInfo* declaredSemanticType = nullptr;

        if (node->hasNode(ASTTag::Type)) {
            typeInfoIRValue = visitTypeModifierNode((*node)[ASTTag::Type]);
            auto* inst = dynamic_cast<Instruction*>(typeInfoIRValue);
            auto* typeInfoConst = static_cast<Constant*>(inst->getOperands()[0]);
            declaredSemanticType = typeInfoConst->getContentValue<TypeInfo*>();
        }
        IRValue* initVal = nullptr;

        if (node->hasNode(ASTTag::AssignTerm)) {
            initVal = visitWholeExprNode(
                (*node)[ASTTag::AssignTerm], declaredSemanticType);
        }

        if (!initVal && !typeInfoIRValue) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "A let statement cannot be used to declare a identifier without a specified type or an initial value.",
                            node->getPosInfo());
        }

        TypeInfo* semanticType = declaredSemanticType
            ? declaredSemanticType
            : (initVal ? initVal->getSemanticType() : nullptr);
        return createAlloca(identifier.content, semanticType, initVal, node->getPosInfo());
    }

    // 表达式语句：标识符表达式（如调用、自增）或赋值表达式，结果值被丢弃。
    IRValue* IRGenerator::visitExprStmtNode(NodePtr node) {
        if (node->hasNode(ASTTag::IdentifierExprNode)) {
            return visitIdentifierExprNode((*node)[ASTTag::IdentifierExprNode]);
        }
        else
            return visitAssignExprNode((*node)[ASTTag::AssignExprNode]);
    }

    // 语句块：新建一个名为 blockName 的基本块（buildBlock 会把游标移到新块），并在其中生成所有语句。
    // beforeBlock != -1 时，从该块补一条 br 跳入新块（函数定义用它把 entry 块接到函数体）；
    // 其他调用方（if/while/for 等）自行连接控制流，传 -1。
    // 语句块同时开启一个新的词法作用域：符号表 enter/leave，并生成 enter_scope/leave_scope 指令，
    // 代码生成据此进入/离开 GC root 作用域。
    // 返回值是块的入口基本块；块内语句可能又建立新块，调用方需在调用后用 cur() 取得出口块。
    IRValue* IRGenerator::visitBlockStmtNode(NodePtr node, fzlib::String blockName, long beforeBlock) {
        IRValue* block = curFunc()->buildBlock(blockName);

        if (beforeBlock != -1) {
            curFunc()
                ->block(beforeBlock)
                ->createInstruction(OpKind::br,
                                    program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Void),
                                    {block},
                                    "br." + blockName);
        }

        curFunc()->fnScope().enter();
        curFunc()->curBlock()->createEnterScope();

        // 游标已在新块上（buildBlock 已移动），这里等价于空操作。
        curFunc()->moveCursor(curFunc()->cur());

        for (auto stmt: (*node)[ASTTag::Stmts]->getChildren()) {
            visitStmt(stmt);
        }


        // leave_scope 插在当前（出口）块的终结指令之前，保证 return/br 之前先离开 GC root 作用域。
        curFunc()->curBlock()->createLeaveScope();
        curFunc()->fnScope().leave();

        return block;
    }

    // if 语句：before 块中求条件 -> if.then /（可选）if.else -> if.merge。
    // 所有基本块先按顺序建好，再回头给 before/then 出口/else 出口补跳转指令；
    // 若某出口块已被 return/break 等终结，Block::createBr/createCondBr 会自动忽略新增跳转。
    IRValue* IRGenerator::visitIfStmtNode(NodePtr node) {
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        // 条件求值本身可能建块（如短路逻辑），因此 before 块取求值后的当前块。
        int beforeBlockIndex = curFunc()->cur();

        // if.then：条件为真时执行的基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "if.then");
        int thenExitBlockIndex = curFunc()->cur();
        //

        // if.else：条件为假时执行的基本块
        IRValue* elseBlock = nullptr;
        int elseExitBlockIndex = -1;

        if (node->hasNode(ASTTag::ElseStmtNode)) {
            auto block = (*(*node)[ASTTag::ElseStmtNode])[ASTTag::Block];
            elseBlock = visitBlockStmtNode(block, "if.else");
            elseExitBlockIndex = curFunc()->cur();
        }
        //

        // if.merge：条件分支汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("if.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        // before -> then 或 else/merge
        curFunc()
            ->block(beforeBlockIndex)
            ->createCondBr(cond, thenBlock, (elseBlock?elseBlock:mergeBlock));
        //

        // then -> merge
        curFunc()
            ->block(thenExitBlockIndex)
            ->createBr(mergeBlock);
        //

        // else -> merge
        if (elseBlock) {
            curFunc()
            ->block(elseExitBlockIndex)
            ->createBr(mergeBlock);
        }
        //

        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }


    // while 语句：before -> while.prep（求条件）-> while.then（循环体）/ while.merge。
    // 条件放在独立的 prep 块中，使每次循环都重新求值；循环体结束跳回 prep。
    IRValue* IRGenerator::visitWhileStmtNode(NodePtr node) {
        int beforeBlockIndex = curFunc()->cur();

        // while.prep：循环条件准备基本块
        IRValue* prepareBlock = curFunc()->buildBlock("while.prep");
        int prepareBlockIndex = curFunc()->cur();

        curFunc()
            ->block(beforeBlockIndex)
            ->createBr(prepareBlock);

        curFunc()->moveCursor(prepareBlockIndex);
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int prepareExitBlockIndex = curFunc()->cur();
        //

        // while.merge：循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("while.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        // 在生成循环体之前压入循环信息：continue 跳回 prep 重新求条件，break 跳到 merge。
        curFunc()->enterLoop(prepareBlock, mergeBlock);

        // while.then：循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "while.then");
        int thenExitBlockIndex = curFunc()->cur();
        //


        // then -> prep
        curFunc()
            ->block(thenExitBlockIndex)
            ->createBr(prepareBlock);
        //

        // prep -> merge 或 then
        curFunc()
            ->block(prepareExitBlockIndex)
            ->createCondBr(cond, thenBlock, mergeBlock);
        //
        curFunc()->leaveLoop();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    // for 语句：init（当前块）-> for.cond -> for.body -> for.step -> for.cond，条件为假时到 for.merge。
    // 外层额外开一个符号表作用域，使 init 中声明的循环变量只在 for 内可见。
    IRValue* IRGenerator::visitForStmtNode(NodePtr node) {
        curFunc()->fnScope().enter();

        // for 初始化（位于 beforeBlock 中）
        if (node->hasNode(ASTTag::DeclareStmtNode)) {
            visitDeclareStmtNode((*node)[ASTTag::DeclareStmtNode]);
        }
        int initExitIndex = curFunc()->cur();

        // for.cond：循环条件基本块
        IRValue* condBlock = curFunc()->buildBlock("for.cond");
        IRValue* cond = visitBinaryExprNode((*node)[ASTTag::Condition]);
        int condBlockExitIndex = curFunc()->cur();
        //

        // for.step：循环步进基本块
        IRValue* stepBlock = curFunc()->buildBlock("for.step");
        visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        int stepBlockExitIndex = curFunc()->cur();
        //

        // for.merge：循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("for.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        curFunc()->enterLoop(stepBlock, mergeBlock);

        // for.body：循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "for.body");
        int thenBlockExitIndex = curFunc()->cur();
        //


        // init -> cond
        curFunc()
            ->block(initExitIndex)
            ->createBr(condBlock);
        //

        // cond -> body 或 merge
        curFunc()
            ->block(condBlockExitIndex)
            ->createCondBr(cond, thenBlock, mergeBlock);
        //

        // body -> step
        curFunc()
            ->block(thenBlockExitIndex)
            ->createBr(stepBlock);
        //

        // step -> cond
        curFunc()
            ->block(stepBlockExitIndex)
            ->createBr(condBlock);
        //

        curFunc()->leaveLoop();
        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);
        return mergeBlock;
    }

    // repeat(n) 语句：执行循环体 n 次。
    // 隐藏计数器 $repeat_counter.N（i32，初值 0）与上限值在 repeat.prepare 块中建立/求值；
    // repeat.cond 中比较 counter < limit；repeat.then 为循环体；repeat.step 中 counter + 1 后跳回 cond。
    // 外层开一个符号表作用域，隐藏计数器只在本语句内可见。
    IRValue* IRGenerator::visitRepeatStmtNode(NodePtr node) {
        curFunc()->fnScope().enter();

        int beforeBlockIndex = curFunc()->cur();

        // repeat.prepare：重复循环准备基本块
        IRValue* prepareBlock = curFunc()->buildBlock("repeat.prepare");
        // 注意：repeat_counter 从未自增，所有隐藏计数器都同名，依赖各自的作用域隔离。
        static int repeat_counter = 1;
        IRValue* counter = createAlloca(
            "$repeat_counter." + std::to_string(repeat_counter),
            program.getContext().typeInfoPool().makeBasicTypeID(TypeID::Int32),
            Constant::get((int)0, getTypeInfoContext()), node->getPosInfo()
        );
        IRValue* limitValue = visitWholeExprNode((*node)[ASTTag::HeadExpr]);
        int prepareBlockExitIndex = curFunc()->cur();
        //

        // repeat.cond：重复循环条件基本块
        IRValue* condBlock = curFunc()->buildBlock("repeat.cond");
        // 在 cond 块中 load 计数器；step 块复用该值（cond 支配 step，跨块使用是安全的）。
        IRValue* counterLoadVal = createLoad(counter, node->getPosInfo());
        IRValue* condResult = curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::lgc_ls_than,
                program.getContext().typeInfoPool().makeBasicTypeID(Bool),
                {counterLoadVal, limitValue},
                "lgc_ls_than"
            );
        int condBlockExitIndex = curFunc()->cur();
        //

        // repeat.step：重复循环步进基本块
        IRValue* stepBlock = curFunc()->buildBlock("repeat.step");
        IRValue* stepValue = curFunc()
            ->curBlock()
            ->createInstruction(
                OpKind::add,
                program.getContext().typeInfoPool().makeBasicTypeID(Int32),
                {counterLoadVal, Constant::get((int)1, getTypeInfoContext())},
                "add"
            );
        createStore(counter, stepValue, node->getPosInfo());
        curFunc()
            ->curBlock()
            ->createBr(condBlock);
        //

        // repeat.merge：重复循环退出后的汇合基本块
        IRValue* mergeBlock = curFunc()->buildBlock("repeat.merge");
        int mergeBlockIndex = curFunc()->cur();
        //

        // 与 for 相同：循环信息在循环体生成之后才压栈，循环体内的 break/continue 看不到本循环；
        // continue 目标为 cond（会跳过 step）。
        curFunc()->enterLoop(stepBlock, mergeBlock);

        // repeat.then：重复循环体基本块
        IRValue* thenBlock = visitBlockStmtNode((*node)[ASTTag::Block], "repeat.then");
        int thenBlockExitIndex = curFunc()->cur();
        //

        // before -> prepare
        curFunc()
            ->block(beforeBlockIndex)
            ->createBr(prepareBlock);
        //

        // cond -> then 或 merge
        curFunc()
            ->block(condBlockExitIndex)
            ->createCondBr(condResult, thenBlock, mergeBlock);
        //

        // then -> step
        curFunc()
            ->block(thenBlockExitIndex)
            ->createBr(stepBlock);
        //

        // prepare -> cond
        curFunc()
            ->block(prepareBlockExitIndex)
            ->createBr(condBlock);
        //

        curFunc()->leaveLoop();
        curFunc()->fnScope().leave();
        curFunc()->moveCursor(mergeBlockIndex);

        return mergeBlock;
    }

    // match 语句：被匹配值在 before 块中只 load 一次，随后按顺序逐个比较：
    //   before -> case0 -(相等)-> then0 -> merge
    //               \-(不等)-> case1 -> ... -> 最后一个 case 不等时 -> default -> merge
    // 约束：必须有 default，且 default 必须是最后一个分支。
    // 各 case 块先生成（并记录出口块、比较结果、then 块），循环结束后再统一补条件跳转。
    IRValue* IRGenerator::visitMatchStmtNode(NodePtr node) {
        IRValue* identifier = visitIdentifierExprNode((*node)[ASTTag::Identifier]);
        IRValue* idenValue = createLoad(identifier, node->getPosInfo());

        std::vector<IRValue*> caseBlocks;
        std::vector<std::tuple<int, IRValue*, IRValue*>> caseBlockPairs;
        IRValue* defaultThenBlock = nullptr;

        std::vector<NodePtr> cases = (*node)[ASTTag::Cases]->getChildren();

        int beforeBlockIndex = curFunc()->cur();

        // 先建立汇合块，使各分支体生成时就能跳到它。
        IRValue* mergeBlock = curFunc()->buildBlock("match.merge");
        int mergeBlockExitIndex = curFunc()->cur();

        bool hasDefault = false;
        for (std::size_t i = 0; i < cases.size(); i ++) {
            // 静态计数器保证不同 match 语句的块名互不冲突。
            static int matchCaseIndex = 0;

            auto cs = cases[i];
            // default 分支：只生成分支体并跳到 merge，其入口块作为最后一个 case 的"不等"目标。
            if (cs->hasNode(ASTTag::Default)) {
                if (i != cases.size() - 1) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Cannot put default block to the middle of the cases.",
                        cs->getPosInfo()
                    );
                }
                hasDefault = true;
                defaultThenBlock = visitBlockStmtNode((*cs)[ASTTag::Block], "match.default");
                curFunc()
                    ->curBlock()
                    ->createBr(mergeBlock);
            }
            // 普通 case：新建 case 块，在其中求目标值并做相等比较，然后生成 then 块。
            else if (cs->hasNode(ASTTag::HeadExpr)) {
                IRValue* caseBlock = curFunc()->buildBlock("match.case." + std::to_string(matchCaseIndex));
                IRValue* targetValue = visitWholeExprNode((*cs)[ASTTag::HeadExpr]);
                IRValue* condResult = curFunc()
                    ->curBlock()
                    ->createInstruction(
                        OpKind::lgc_equal,
                        program.getContext().typeInfoPool().makeBasicTypeID(Bool),
                        {idenValue, targetValue},
                        "lgc_equal"
                    );
                int caseBlockExitIndex = curFunc()->cur();
                caseBlocks.push_back(caseBlock);

                IRValue* thenBlock = visitBlockStmtNode((*cs)[ASTTag::Block], "match.then." + std::to_string(matchCaseIndex));
                int thenBlockExitIndex = curFunc()->cur();

                // 条件跳转需要下一个 case 块，此时尚未建立，故先记录，稍后统一连接。
                caseBlockPairs.emplace_back(caseBlockExitIndex, condResult, thenBlock);

                curFunc()
                    ->block(thenBlockExitIndex)
                    ->createBr(mergeBlock);
            }
            matchCaseIndex ++;
        }

        if (!hasDefault)
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "Match Statement must have 'default' case.",
                node->getPosInfo()
            );

        // 补全每个 case 的条件跳转：相等进入对应 then 块，不等进入下一个 case 块；
        // 由于 default 必为最后一个分支，caseBlockPairs 的最后一项下标恰为 cases.size() - 2，
        // 它的"不等"目标是 default 块。
        for (std::size_t i = 0; i < caseBlockPairs.size(); i ++) {
            int caseBlockExitIndex = std::get<0>(caseBlockPairs[i]);
            IRValue* condResult = std::get<1>(caseBlockPairs[i]);
            IRValue* thenBlock = std::get<2>(caseBlockPairs[i]);

            if (i != cases.size() - 2)
                curFunc()
                    ->block(caseBlockExitIndex)
                    ->createCondBr(condResult, thenBlock, caseBlocks[i + 1]);
            else
                curFunc()
                    ->block(caseBlockExitIndex)
                    ->createCondBr(condResult, thenBlock, defaultThenBlock);
        }

        // before 块跳入第一个 case；只有 default 时直接跳入 default。
        if (caseBlockPairs.empty()) {
            curFunc()
                ->block(beforeBlockIndex)
                ->createBr(defaultThenBlock);
        }
        else {
            curFunc()
                ->block(beforeBlockIndex)
                ->createBr(caseBlocks[0]);
        }

        curFunc()->moveCursor(mergeBlockExitIndex);

        return mergeBlock;
    }

    // 函数定义：
    // 1. 解析返回类型与形参（类型列表与名字列表是 Args 的两个子节点，按下标一一对应）；
    // 2. 用形参语义类型改编函数名（name_<类型>...），以支持重载；无参函数保持原名；
    // 3. buildFunction 创建函数及其 entry 块，并把模块游标切到该函数；
    // 4. 在 entry 块中为每个形参生成 `param` 指令并登记到函数作用域；
    // 5. 函数体作为语句块生成，entry 块以 br 跳入函数体块。
    // 约束：main 必须返回 i32；函数体中至少要出现一条 return 语句。
    IRValue* IRGenerator::visitFuncDefineStmtNode(NodePtr node) {
        auto fnName = (*node)[ASTTag::Identifier]->getToken().content;
        TypeInfo* semanticReturnType = getTypeInfoFromNode((*node)[ASTTag::Type]);
        FormalParamsDefine params;
        std::vector<TypeInfo*> paramSemanticTypes;

        if (node->hasNode(ASTTag::Args)) {
            auto typeList = (*node)[ASTTag::Args]->getChildren()[0];
            auto nameList = (*node)[ASTTag::Args]->getChildren()[1];
            for (std::size_t i = 0; i < typeList->getChildren().size(); i ++) {
                auto tyInfo = getTypeInfoFromNode(typeList->getChildren()[i]);
                paramSemanticTypes.push_back(tyInfo);
                fzlib::String argName = nameList->getChildren()[i]->getToken().content;

                params.emplace_back(std::move(argName), tyInfo);
            }
        }

        fnName = mangleFnName(fnName, paramSemanticTypes);
        IRValue* fn = curModule()->buildFunction(fnName, semanticReturnType, params, node->getPosInfo());
        long initBlockIndex = curFunc()->cur();

        if (node->hasNode(ASTTag::Args)) {
            for (std::size_t i = 0; i < params.size(); ++i) {
                auto& arg = params[i];
                createParam(arg.first, arg.second, node->getPosInfo());
            }
        }

        // 这里比较的是改编后的名字：只有无参的 main 才会命中此检查。
        if (fnName == "main" && semanticReturnType->getTypeID() != Int32) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "'main' function must return 'i32' value.",
                (*node)[ASTTag::Type]->getPosInfo()
            );
        }

        visitBlockStmtNode((*node)[ASTTag::Block], "fn." + fnName, initBlockIndex);

        // 返回检查器只记录"函数中出现过 return"，并不验证所有控制流路径都有返回。
        if (!curFunc()->getReturnChecker()) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A Function must have a return statement",
                node->getPosInfo()
            );
        }
        return fn;
    };

    // 结构体定义：补全在 startGenerate 第二遍中预声明的 opaque 结构体。
    // 逐个收集字段（名字、语义类型、位置），校验：
    // - 字段名不可重复；
    // - 字段默认值目前只支持 `null`，且字段必须是可空的结构体/数组类型（默认值为空句柄常量）；
    // - 字段类型不可为 void。
    // 字段类型可以引用尚未补全的结构体（含自身），因为它们已作为 opaque 结构体登记；
    // 结构体整体的递归合法性在 startGenerate 第四遍中校验。不产生任何指令，返回 nullptr。
    IRValue* IRGenerator::visitStructDefineStmtNode(NodePtr node) {
        const auto name = (*node)[ASTTag::Identifier]->getToken();
        std::vector<SemanticField> members;
        std::map<fzlib::String, Constant*> defaultValues;
        std::map<fzlib::String, bool> memberNames;
        if (node->hasNode(ASTTag::Members)) {
            for (auto& member: (*node)[ASTTag::Members]->getChildren()) {
                const auto memberToken = (*member)[ASTTag::Identifier]->getToken();
                if (!memberNames.emplace(memberToken.content, true).second) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "Duplicate struct member: '" + memberToken.content + "'",
                        memberToken.info
                    );
                }

                SemanticField info;
                info.name = memberToken.content;
                info.type = getTypeInfoFromNode((*member)[ASTTag::TypeModifierNode]);
                info.info = memberToken.info;

                Constant* defaultValue = nullptr;

                if (member->hasNode(ASTTag::WholeExprNode)) {
                    auto defaultExpr = (*member)[ASTTag::WholeExprNode];
                    if (!isNullLiteralExpression(defaultExpr)) {
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "Struct member defaults must be literal values.",
                            defaultExpr->getPosInfo());
                    }
                    const auto* baseType = info.type->getBase();
                    if (!info.type->isNullable() ||
                        (!baseType->isStruct() && !baseType->isArray())) {
                        throw SakuraError(
                            OccurredTerm::IR_GENERATING,
                            "null can only default-initialize a nullable struct or array member.",
                            defaultExpr->getPosInfo());
                    }
                    defaultValue = Constant::getNullHandle(info.type, defaultExpr->getPosInfo());
                }

                if (info.type->getBase()->isBasic() && info.type->getBase()->getTypeID() == TypeID::Void) {
                    throw SakuraError(
                        OccurredTerm::IR_GENERATING,
                        "A struct member cannot have type 'void'.",
                        memberToken.info
                    );
                }

                members.push_back(info);
                if (defaultValue) {
                    defaultValues.emplace(info.name, defaultValue);
                }
            }
        }

        // 由 NamingContext 把字段与默认值写入已声明的结构体声明，完成补全。
        curModule()->implStruct(name.content, std::move(members), std::move(defaultValues), name.info);
        return nullptr;
    }

    // return 语句：
    // - 无返回值时函数必须返回 void；有返回值时函数不能是 void；
    // - 返回值以函数返回类型作为期望类型求值（支持 `return null`），且必须可赋值给返回类型；
    // - 生成 ret 并标记函数已出现 return（供 visitFuncDefineStmtNode 检查）。
    IRValue* IRGenerator::visitReturnStmtNode(NodePtr node) {
        if (!node->hasNode(ASTTag::HeadExpr)) {
            if (curFunc()->getSemanticReturnType()->getTypeID() != Void) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "A non-void function must return a value of its declared return type.",
                    node->getPosInfo()
                );
            }
        }
        else if (node->hasNode(ASTTag::HeadExpr) && curFunc()->getSemanticReturnType()->getTypeID() == Void) {
            throw SakuraError(
                OccurredTerm::IR_GENERATING,
                "A function with a void return type cannot return a value of any type.",
                (*node)[ASTTag::HeadExpr]->getPosInfo()
            );
        }

        if (node->hasNode(ASTTag::HeadExpr)) {
            IRValue* retValue = nullptr;
            retValue = visitWholeExprNode(
                (*node)[ASTTag::HeadExpr],
                curFunc()->getSemanticReturnType());
            if (!isAssignableTo(retValue->getSemanticType(),
                                curFunc()->getSemanticReturnType())) {
                throw SakuraError(
                    OccurredTerm::IR_GENERATING,
                    "The semantic type of the returned value is incompatible with the function return type.",
                    (*node)[ASTTag::HeadExpr]->getPosInfo());
            }
            curFunc()->setReturnChecker(true);
            return curFunc()
                        ->curBlock()
                        ->createReturn(retValue);
        }
        else {
            curFunc()->setReturnChecker(true);
            return curFunc()
                        ->curBlock()
                        ->createReturn();
        }
    }

    // break：跳到最内层循环的 break 目标（循环的 merge 块）；不在循环中则报错。
    IRValue* IRGenerator::visitBreakStmtNode(NodePtr node) {
        if (curFunc()->isLookEmpty()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Break Statement used out of loop",
                            node->getPosInfo());
        }

        IRValue* target = curFunc()->getLoopTop().breakTarget;
        return curFunc()->curBlock()->createBr(target);
    }

    // continue：跳到最内层循环的 continue 目标（条件块）；不在循环中则报错。
    IRValue* IRGenerator::visitContinueStmtNode(NodePtr node) {
        if (curFunc()->isLookEmpty()) {
            throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Continue Statement used out of loop",
                            node->getPosInfo());
        }

        IRValue* target = curFunc()->getLoopTop().continueTarget;
        return curFunc()->curBlock()->createBr(target);
    }

    // 语句分发：剥掉可选的 Stmt 包装节点后，按语句标签调用对应的 visit 函数；
    // 未知语句标签报错。
    IRValue* IRGenerator::visitStmt(NodePtr node) {
        NodePtr stmt;
        if (node->getTag() == ASTTag::Stmt)
            stmt = (*node)[ASTTag::Stmt];
        else
            stmt = node;

        if (stmt->getTag() == ASTTag::DeclareStmtNode) {
            return visitDeclareStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ExprStmtNode) {
            return visitExprStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::IfStmtNode) {
            return visitIfStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::WhileStmtNode) {
            return visitWhileStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ForStmtNode) {
            return visitForStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::BlockStmtNode) {
            return visitBlockStmtNode(stmt, "blockStmt");
        }
        else if (stmt->getTag() == ASTTag::FuncDefineStmtNode) {
            return visitFuncDefineStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::StructDefineStmtNode) {
            return visitStructDefineStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ReturnStmtNode) {
            return visitReturnStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::BreakStmtNode) {
            return visitBreakStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::RepeatStmtNode) {
            return visitRepeatStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::MatchStmtNode) {
            return visitMatchStmtNode(stmt);
        }
        else if (stmt->getTag() == ASTTag::ContinueStmtNode) {
            return visitContinueStmtNode(stmt);
        }

        throw SakuraError(OccurredTerm::IR_GENERATING,
                            "Unknown Statement to generate",
                            node->getPosInfo());
    }
}
