#ifndef SAKURAE_FUNCTION_HPP
#define SAKURAE_FUNCTION_HPP

#include <stack>
#include <utility>

#include "Compiler/Error/error.hpp"
#include "block.hpp"
#include "scope.hpp"

namespace sakuraE::IR {
    // 形参定义列表：(形参名, 语义类型) 有序对。
    using FormalParamsDefine = std::vector<std::pair<fzlib::String, TypeInfo*>>;
    class Module;

    // SakuraE 函数：语义 IR 中的函数定义/声明，同时是可调用值(CallableValue)。
    // 签名以函数语义类型（FunctionTypeInfo，含返回类型与形参类型）保存在 CallableValue 中，
    // 形参名单独保存在 formalParamNames，二者按下标一一对应。
    // Function 拥有其全部 Block（析构时 delete）；parent 为所属 Module 的非拥有引用。
    class Function: public CallableValue {
        // 原始名称（未经模块限定/改名的源码名），仅部分构造函数设置。
        fzlib::String rawName;
        std::vector<fzlib::String> formalParamNames;
        // 函数体最外层作用域，记录名称到 IRValue 的绑定。
        Scope<IRValue*> funcScope;

        PositionInfo createInfo;

        std::vector<Block*> blocks;
        // 当前代码块（插入点）在 blocks 中的索引；-1 表示尚未创建任何块。
        long cursor = -1;

        // break 和 continue 管理器：记录当前循环的 continue 目标块与 break 目标块。
        struct LoopInfo {
            IRValue* continueTarget;
            IRValue* breakTarget;
        };

        // 返回值检查器：标记函数体是否已生成 return，供生成器判断是否需要补发返回。
        bool hasReturn = false;

        // 循环嵌套栈，栈顶为最内层循环。
        std::stack<LoopInfo> loopStack;

        Module* parent = nullptr;
        // 块与控制流指令使用的语义类型（void）。需要从签名的返回类型取得 TypeInfo 上下文，
        // 因此必须先建立签名才能创建块。
        TypeInfo* controlType() const {
            if (!getReturnType()) throw std::logic_error("Function must have a semantic signature before building blocks");
            return getReturnType()->getTypeContext().makeBasicTypeID(TypeID::Void);
        }
    public:
        // 以返回类型构造无形参函数。
        Function(fzlib::String n, TypeInfo* retType, PositionInfo info):
            Function(std::move(n), retType, {}, std::move(info)) {}

        // 仅用于预声明，定义时通过 setFuncDefineInfo 建立语义签名。
        Function(fzlib::String n, PositionInfo info):
            CallableValue(nullptr, std::move(n)), funcScope(info), createInfo(info) {}

        // 以完整签名构造：委托 setFuncDefineInfo 校验并建立函数语义类型。
        Function(fzlib::String n, TypeInfo* retType, FormalParamsDefine params, PositionInfo info):
            CallableValue(nullptr, std::move(n)), funcScope(info), createInfo(info) {
            setFuncDefineInfo(std::move(params), retType);
        }

        // 额外记录原始名称 raw 的构造函数。
        Function(fzlib::String n, fzlib::String raw, TypeInfo* retType,
                 FormalParamsDefine params, PositionInfo info):
            Function(std::move(n), retType, std::move(params), std::move(info)) {
            rawName = std::move(raw);
        }

        // 释放本函数拥有的全部基本块（块再释放其指令）。
        ~Function() {
            for (auto blk: blocks) {
                delete blk;
            }
        }

        // 进入循环：压入 continue 目标 c 与 break 目标 b。
        void enterLoop(IRValue* c, IRValue* b) {
            loopStack.push({c, b});
        }

        // 离开循环：弹出最内层循环信息；栈为空时忽略。
        void leaveLoop() {
            if (!loopStack.empty()) loopStack.pop();
        }

        // 是否不在任何循环内（用于检查 break/continue 是否合法）。
        bool isLookEmpty() { return loopStack.empty(); }

        // 返回最内层循环信息；调用前需确认栈非空。
        LoopInfo& getLoopTop() { return loopStack.top(); }

        // 设置所属模块（非拥有引用）。
        void setParent(Module* mod) {
            parent = mod;
        }

        // 返回原始名称；未设置时为空字符串。
        const fzlib::String& getRawName() const { return rawName; }

        // 设置函数定义信息：拆分形参名与形参类型，并通过返回类型所属的类型上下文
        // 创建函数语义类型作为签名。返回类型不能为空。
        void setFuncDefineInfo(FormalParamsDefine params, TypeInfo* retType) {
            if (!retType) throw std::invalid_argument("Function requires a semantic return type");
            std::vector<TypeInfo*> paramTypes;
            std::vector<fzlib::String> paramNames;
            for (const auto& param : params) {
                paramNames.push_back(param.first);
                paramTypes.push_back(param.second);
            }
            // 类型工厂会验证空组件及 context 归属；验证成功后再更新定义，保证失败时不留下半更新状态。
            auto* signature = retType->getTypeContext().makeFunctionTypeID(
                retType, std::move(paramTypes));
            formalParamNames = std::move(paramNames);
            CallableValue::setFunctionType(signature);
        }

        // 直接替换签名；要求为函数类型且形参数量与已有形参名数量一致。
        void setFunctionType(TypeInfo* signature) {
            if (!signature || !signature->isFunction()
                || signature->getFunctionTypeInfo()->getArgCount() != formalParamNames.size()) {
                throw std::invalid_argument("Function signature must match its named parameters");
            }
            CallableValue::setFunctionType(signature);
        }

        // 返回值检查标记的设置与获取。
        void setReturnChecker(bool state) {
            hasReturn = state;
        }

        bool getReturnChecker() {
            return hasReturn;
        }

        // 返回所属模块（非拥有引用）。
        Module* getParent() const {
            return parent;
        }

        // 返回签名中的返回类型；尚未建立签名（仅预声明）时返回 nullptr。
        TypeInfo* getReturnType() const {
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getReturnType() : nullptr;
        }

        // getReturnType 的语义命名别名。
        TypeInfo* getSemanticReturnType() const {
            return getReturnType();
        }

        // 返回签名中的形参类型列表；无签名时返回静态空列表，避免返回悬空引用。
        const std::vector<TypeInfo*>& getSemanticParamTypes() const {
            static const std::vector<TypeInfo*> empty;
            const auto* signature = getFunctionTypeInfo();
            return signature ? signature->getArgTypes() : empty;
        }

        // getSemanticParamTypes 的别名。
        const std::vector<TypeInfo*>& getParamsOnlyType() const {
            return getSemanticParamTypes();
        }

        // 以给定指令创建新基本块并追加到函数末尾，同时把游标移动到新块。
        IRValue* buildBlock(fzlib::String id, std::vector<Instruction*> ops) {
            Block* block = new Block(id, controlType(), std::move(ops));
            block->setName(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        // 创建空基本块并追加到末尾，游标移动到新块。
        IRValue* buildBlock(fzlib::String id) {
            Block* block = new Block(id, controlType());
            block->setName(id);
            block->setParent(this);
            blocks.push_back(block);
            cursor = blocks.size() - 1;

            return block;
        }

        // 返回游标指向的当前块（不做越界检查，需先创建块）。
        Block* curBlock() {
            return blocks[cursor];
        }

        // 游标重置到第一个块。
        void reset() {
            cursor = 0;
        }

        // 返回全部块（只读）。
        const std::vector<Block*>& getBlocks() const {
            return blocks;
        }

        // 按下标取块（不做越界检查）。
        Block* block(int index) {
            return blocks[index];
        }

        // 移动游标到指定块；越界时报告 IR 生成错误。返回自身以便链式调用。
        Function& moveCursor(long target) {
            if (target >= 0 && target < static_cast<long>(blocks.size())) {
                cursor = target;
            }
            else
                throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Move cursor to a unknown place",
                                    createInfo);
            return *this;
        }

        // 返回函数作用域。
        Scope<IRValue*>& fnScope() {
            return funcScope;
        }

        // 返回形参名列表。
        const std::vector<fzlib::String>& getFormalParamNames() const {
            return formalParamNames;
        }

        // 将形参名与签名中的形参类型重新组合为 (名称, 类型) 列表。
        FormalParamsDefine getFormalParams() const {
            FormalParamsDefine result;
            const auto& types = getSemanticParamTypes();
            for (std::size_t i = 0; i < formalParamNames.size(); ++i) {
                result.emplace_back(formalParamNames[i], types.at(i));
            }
            return result;
        }

        // 返回游标的可写引用。
        long& cur() {
            return cursor;
        }

        // 按下标取块，同 block()。
        Block* operator[] (int index) {
            return block(index);
        }

        // 返回函数声明位置。
        const PositionInfo& getInfo() const {
            return createInfo;
        }

        // 生成调试用文本表示："func 名称(形参: 类型, ...) -> 返回类型 {块...}"；
        // 未建立签名时返回类型显示为 "<undeclared>"。
        fzlib::String toString() {
            fzlib::String result = "func " + name + "(";
            const auto formalParams = getFormalParams();
            for (std::size_t i = 0; i < formalParams.size(); i ++) {
                auto arg = formalParams[i];
                if (i == formalParams.size() - 1)
                    result += arg.first + ": " + arg.second->toString();
                else
                    result += arg.first + ": " + arg.second->toString() + ", ";
            }
            result += ") -> " + (getReturnType() ? getReturnType()->toString() : "<undeclared>") + " {";

            for (auto block: blocks) {
                result += block->toString();
            }

            result += "}";
            return result;
        }
    };
}

#endif /* !SAKURAE_FUNCTION_HPP */
