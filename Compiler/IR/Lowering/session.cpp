#include "session.hpp"

#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include "Compiler/IR/Semantic/model/program.hpp"
#include <unordered_set>
#include <algorithm>
#include <functional>

namespace sakuraE::IR {
    // 文件内部辅助函数：ABI 判定、字段存储形式、GC root 判定、符号登记与 IRValue 遍历。
    // lowering（写入元数据）与 BackendModule::verify（复核元数据）共用这些规则，保证两边判定一致。
    namespace {
        // 返回值 ABI：void 返回 Void；运行时内部的 GC/分配/类型元数据/数组越界检查函数
        // 直接收发原始(Raw)本机值；其余函数（含用户函数与普通运行时库函数）统一按装箱(Boxed)的 RuntimeValue* 传递。
        ABIValueKind resultABI(IRType* type, const fzlib::String& name) {
            if (type->getIRTypeID() == IRTypeID::VoidTyID) return ABIValueKind::Void;
            if (name == "__gc_alloc" || name == "__alloc" || name == "__free" ||
                name == "__gc_register" || name == "__gc_register_value" ||
                name == "__gc_pop" || name == "__gc_get_struct_type" ||
                name == "__runtime_type_info_basic" || name == "__runtime_type_info_array" ||
                name == "__runtime_type_info_pointer" || name == "__runtime_type_info_reference" ||
                name == "__runtime_type_info_struct" ||
                name == "__runtime_check_array_bounds" || name == "__runtime_array_bounds_error") {
                return ABIValueKind::Raw;
            }
            return ABIValueKind::Boxed;
        }

        // 参数 ABI：create_string 与 __gc_register_value 的指针参数是原始字符/对象指针；
        // __gc_get_struct_type 与 __runtime_type_info_struct 的全部参数都是原始值；
        // 其余情况沿用返回值 ABI 的按函数名判定规则（运行时内部函数为 Raw，否则 Boxed）。
        ABIValueKind parameterABI(IRType* type, const fzlib::String& name) {
            if (((name == "create_string" || name == "__gc_register_value") && type->isPointer()) ||
                name == "__gc_get_struct_type" || name == "__runtime_type_info_struct") return ABIValueKind::Raw;
            return resultABI(type, name);
        }

        // 根据运行时类型元数据决定结构体字段在原始结构体布局中的存储形式：
        // 标量按值、string/指针为裸指针、引用为指针、嵌套结构体/数组按值内嵌。
        // void/函数/类型描述符不能作为字段存储，抛出异常。
        FieldStorageKind fieldStorage(const RuntimeTypeDescription& type) {
            switch (type.kind) {
                case RuntimeTypeKind::I8:
                case RuntimeTypeKind::I32:
                case RuntimeTypeKind::I64:
                case RuntimeTypeKind::U32:
                case RuntimeTypeKind::U64:
                case RuntimeTypeKind::F32:
                case RuntimeTypeKind::F64:
                case RuntimeTypeKind::Bool: return FieldStorageKind::Scalar;
                case RuntimeTypeKind::String:
                case RuntimeTypeKind::Pointer: return FieldStorageKind::Pointer;
                case RuntimeTypeKind::Reference: return FieldStorageKind::Reference;
                case RuntimeTypeKind::Struct: return FieldStorageKind::EmbeddedStruct;
                case RuntimeTypeKind::Array: return FieldStorageKind::EmbeddedArray;
                default: throw std::runtime_error("Unsupported backend struct field representation");
            }
        }

        // 判定一个 IRValue 是否需要作为 GC root 跟踪：只有托管类型（string/数组）才可能需要。
        // 非指令值（常量等）一律跟踪；指令中只有会产生新的托管对象引用的
        // constant/create_array/call/load 需要跟踪，其余指令（如 alloca、gmem 等地址类结果）不跟踪。
        bool rootValue(const IRValue* value, const RuntimeTypeDescription& type) {
            if (!type.managed()) return false;
            const auto* instruction = dynamic_cast<const Instruction*>(value);
            if (!instruction) return true;
            switch (instruction->getKind()) {
                case OpKind::constant:
                case OpKind::create_array:
                case OpKind::call:
                case OpKind::load: return true;
                default: return false;
            }
        }

        // 向符号表登记 name -> function：空名称视为错误；同名映射到不同函数视为符号冲突。
        // 同一函数重复登记（例如 sourceName 与 linkageName 相同）是允许的。
        void addSymbol(std::map<fzlib::String, const BackendFunction*>& symbols,
                       const fzlib::String& name, const BackendFunction* function) {
            if (name.len() == 0) throw std::runtime_error("Backend function has an empty symbol name");
            const auto [it, inserted] = symbols.emplace(name, function);
            if (!inserted && it->second != function) {
                throw std::runtime_error(("Conflicting backend function symbol: " + name).c_str());
            }
        }

        // 深度优先遍历函数内可达的全部 IRValue（函数本身、所有基本块、指令、操作数、常量数组元素），
        // 每个值只访问一次。遇到属于其他函数的基本块时抛出异常，防止跨函数引用混入 side table。
        template<typename Visitor>
        void visitValues(const Function& function, Visitor&& visitor) {
            std::unordered_set<const IRValue*> visited;
            std::function<void(const IRValue*)> visit = [&](const IRValue* value) {
                // 未初始化的局部存储使用空指针作为可选的初始化操作数，这里直接跳过。
                if (!value || !visited.insert(value).second) return;
                visitor(value);
                if (const auto* instruction = dynamic_cast<const Instruction*>(value)) {
                    for (const auto* operand : instruction->getOperands()) visit(operand);
                }
                else if (const auto* constant = dynamic_cast<const Constant*>(value)) {
                    if (const auto* array = constant->getArrayPayload()) {
                        for (const auto* element : array->getArray()) visit(element);
                    }
                }
                else if (const auto* block = dynamic_cast<const Block*>(value)) {
                    if (block->getParent() != &function) {
                        throw std::runtime_error("Backend function references another function's block");
                    }
                    for (const auto* operation : block->getInstructions()) visit(operation);
                }
            };
            // 先从函数本身出发，再显式遍历全部基本块，确保不被操作数引用的块及其指令也会被访问。
            visit(&function);
            for (const auto* block : function.getBlocks()) visit(block);
        }
    }

    // 查询 IRValue 的 storage 类型；缺失说明该值未经过 lowering，抛出异常而不是返回空指针。
    IRType* BackendFunction::storageType(const IRValue* value) const {
        const auto found = valueTypes.find(value);
        if (found == valueTypes.end() || !found->second) {
            throw std::runtime_error("Value has no lowered backend storage type");
        }
        return found->second;
    }

    // 查询 IRValue 的运行时类型元数据；缺失时抛出异常。
    const RuntimeTypeDescription& BackendFunction::runtimeType(const IRValue* value) const {
        const auto found = runtimeTypes.find(value);
        if (found == runtimeTypes.end() || !found->second) {
            throw std::runtime_error("Value has no lowered backend runtime description");
        }
        return *found->second;
    }

    // 查询 gmem 指令的字段访问描述；只有 gmem 指令会登记该表。
    const BackendFieldAccess& BackendFunction::fieldAccess(const Instruction* instruction) const {
        const auto found = fieldAccesses.find(instruction);
        if (found == fieldAccesses.end()) {
            throw std::runtime_error("Struct member access has no lowered backend field metadata");
        }
        return found->second;
    }

    // 该值是否需要作为 GC root 跟踪（临时值登记进 root stack）。
    bool BackendFunction::tracksRoot(const IRValue* value) const {
        return gcInfo.rootedValues.contains(value);
    }

    // 该 alloca 槽位是否承载托管对象引用，需要整个槽位作为 GC root 注册。
    bool BackendFunction::rootsSlot(const IRValue* value) const {
        return gcInfo.rootedSlots.contains(value);
    }

    // 按 sourceName 或 linkageName 查询模块可见的函数（含本模块定义、import 与 runtime 声明）；未找到返回空指针。
    const BackendFunction* BackendModule::lookup(const fzlib::String& name) const {
        const auto found = symbols.find(name);
        return found == symbols.end() ? nullptr : found->second;
    }

    // 模块必须来自本会话绑定的 Program：Program 的 IRContext 与本会话一致、
    // structResolver 就是该 Program、且模块在 Program 的模块列表中。
    // 否则其 TypeInfo 与结构体声明无法与本会话的缓存对应。
    void LoweringSession::validateModule(const Module& module) const {
        const auto* program = module.getSourceProgram();
        if (!program || &program->getContext() != &irContext || structResolver != program ||
            std::find(program->getMods().begin(), program->getMods().end(), &module) == program->getMods().end()) {
            throw std::runtime_error("Module does not belong to this LoweringSession's Program");
        }
    }

    // 结构性自检：复核 lowering 写入的全部元数据与源 IR、共享判定规则（resultABI/parameterABI/
    // rootValue/fieldStorage）一致。任何不一致都说明 lowering 或刷新流程有缺陷，直接抛出异常，
    // 避免把残缺的 Backend IR 交给 LLVM 代码生成。
    void BackendModule::verify() const {
        if (!source || !context || ID != source->id()) throw std::runtime_error("Invalid backend module identity");
        // 每个源函数都必须恰好有一个 BackendFunction
        if (functions.size() != source->getFunctions().size()) throw std::runtime_error("Backend module function list is incomplete");
        // visible：本模块可见的函数集合 = 自有函数 + import；同时用于检测重复
        std::unordered_set<const BackendFunction*> visible;
        for (const auto& function : functions) {
            if (!function || !visible.insert(function.get()).second || function->parent != this || !function->source ||
                function->source->getParent() != source) throw std::runtime_error("Invalid backend function ownership");
        }
        // import 必须来自同一 IRContext 下的其他模块，且不得与自有函数或其他 import 重复
        for (const auto* function : imports) {
            if (!function || !function->parent || function->parent == this || !visible.insert(function).second ||
                function->parent->context != context) throw std::runtime_error("Invalid or duplicate backend import");
        }
        // 按 visible 重新推导期望的符号表，最后与实际 symbols 比较
        std::map<fzlib::String, const BackendFunction*> expectedSymbols;
        for (const auto* function : visible) {
            // 签名、返回类型、参数列表及其平行数组（运行时类型、ABI、参数 root 标记）必须齐全且长度一致；
            // 函数自身作为 IRValue 的 storage 类型必须就是其函数签名
            if (!function->source || !function->signature || !function->returnType || !function->returnRuntimeType ||
                function->sourceName != function->source->getName() || function->linkageName.len() == 0 ||
                function->signature->getReturnType() != function->returnType ||
                function->storageType(function->source) != function->signature ||
                function->parameters.size() != function->source->getSemanticParamTypes().size() ||
                function->parameterRuntimeTypes.size() != function->parameters.size() ||
                function->abi.parameters.size() != function->parameters.size() ||
                function->gcInfo.parameterRoots.size() != function->parameters.size()) {
                throw std::runtime_error("Invalid backend function signature or metadata");
            }
            addSymbol(expectedSymbols, function->sourceName, function);
            addSymbol(expectedSymbols, function->linkageName, function);
            // ABI 按源函数的原始名称重新判定，必须与记录一致
            if (function->abi.result != resultABI(function->returnType, function->source->getRawName())) {
                throw std::runtime_error("Invalid backend result ABI");
            }
            // 逐个参数核对：名称与源函数形参一致、ABI 一致、运行时类型的 storage 与参数类型一致、
            // 托管类型参数必须标记为 GC root
            for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                if (!function->parameters[i].second || function->parameters[i].first != function->source->getFormalParamNames()[i] ||
                    function->abi.parameters[i] != parameterABI(function->parameters[i].second, function->source->getRawName()) ||
                    !function->parameterRuntimeTypes[i] ||
                    function->parameterRuntimeTypes[i]->storage != function->parameters[i].second ||
                    function->gcInfo.parameterRoots[i] != function->parameterRuntimeTypes[i]->managed()) {
                    throw std::runtime_error("Invalid backend parameter metadata");
                }
            }
            // 复核每个可达 IRValue 的 side table：storage 类型与运行时类型一致，GC 描述符合判定规则
            visitValues(*function->source, [&](const IRValue* value) {
                auto* type = function->storageType(value);
                const auto& runtimeType = function->runtimeType(value);
                if (runtimeType.storage != type) throw std::runtime_error("Backend runtime and storage types disagree");
                if (function->tracksRoot(value) != rootValue(value, runtimeType)) throw std::runtime_error("Invalid backend GC root metadata");
                if (const auto* instruction = dynamic_cast<const Instruction*>(value)) {
                    // 只有承载托管类型的 alloca 才登记为 root 槽位
                    if (function->rootsSlot(value) != (instruction->getKind() == OpKind::create_alloca && runtimeType.managed())) {
                        throw std::runtime_error("Invalid backend GC slot metadata");
                    }
                    // sizeof 的类型操作数必须已 lowering 为运行时类型元数据
                    if (instruction->getKind() == OpKind::_sizeof &&
                        (!function->typeOperands.contains(value) || !function->typeOperands.at(value))) {
                        throw std::runtime_error("Backend sizeof is missing its lowered type operand");
                    }
                    // gmem：先核对字段选择子（常量字符串）与记录的字段名一致，
                    // 再核对基址 storage 类型就是该布局、字段的 storage/运行时类型/存储形式与结果值一致
                    if (instruction->getKind() == OpKind::gmem) {
                        const auto& field = function->fieldAccess(instruction);
                        const auto* selector = instruction->getOperands().size() == 2
                            ? dynamic_cast<const Constant*>(instruction->arg(1)) : nullptr;
                        if (!selector || !selector->getSemanticType() ||
                            selector->getSemanticType()->getTypeID() != TypeID::String || selector->isNullHandle() ||
                            !field.layout || field.index >= field.layout->getFields().size() ||
                            field.layout->getFields()[field.index].name != selector->getContentValue<fzlib::String>()) {
                            throw std::runtime_error("Invalid backend struct member selector metadata");
                        }
                        if (instruction->getOperands().size() != 2 || !instruction->arg(0) ||
                            !field.layout || !field.layout->isComplete() || !field.type ||
                            field.index >= field.layout->getFields().size() ||
                            field.layout != function->storageType(instruction->arg(0)) ||
                            field.type != &runtimeType || field.type->storage != type ||
                            field.layout->getFields()[field.index].type != type ||
                            field.storage != fieldStorage(runtimeType)) {
                            throw std::runtime_error("Invalid backend struct member metadata");
                        }
                    }
                    // 逻辑非要求一个 bool 操作数且结果为 bool
                    if (instruction->getKind() == OpKind::lgc_not &&
                        (instruction->getOperands().size() != 1 || !instruction->arg(0) ||
                         runtimeType.kind != RuntimeTypeKind::Bool ||
                         function->runtimeType(instruction->arg(0)).kind != RuntimeTypeKind::Bool)) {
                        throw std::runtime_error("Invalid backend logical-not operands or result");
                    }
                }
                // 每个常量都要有常量种类；类型描述符常量还必须带有 lowering 后的运行时类型元数据
                if (dynamic_cast<const Constant*>(value)) {
                    const auto kind = function->constants.find(value);
                    if (kind == function->constants.end()) throw std::runtime_error("Backend constant kind is missing");
                    if (kind->second == ConstantKind::Descriptor &&
                        (!function->typeOperands.contains(value) || !function->typeOperands.at(value))) {
                        throw std::runtime_error("Backend descriptor is missing its lowered runtime description");
                    }
                }
            });
            // 反向检查各 side table 中不存在未经 lowering 的悬空条目
            for (const auto& [value, type] : function->typeOperands) {
                if (!type || !type->storage || !function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend type operand table");
            }
            for (const auto& [instruction, field] : function->fieldAccesses) {
                if (!instruction || !function->valueTypes.contains(instruction) || instruction->getKind() != OpKind::gmem) {
                    throw std::runtime_error("Invalid backend struct member table");
                }
            }
            for (const auto* value : function->gcInfo.rootedValues) {
                if (!function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend GC root table");
            }
            for (const auto* value : function->gcInfo.rootedSlots) {
                if (!function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend GC slot table");
            }
        }
        // 实际符号表必须与可见函数推导出的符号表完全一致
        if (symbols != expectedSymbols) throw std::runtime_error("Backend symbol table does not match visible functions");
        // using 模块列表不得为空指针、跨 IRContext 或重复
        std::unordered_set<const BackendModule*> seenModules;
        for (const auto* module : usingModules) {
            if (!module || module->context != context || !seenModules.insert(module).second) {
                throw std::runtime_error("Invalid backend using module");
            }
        }
        // 模块声明的每个结构体都要有布局；布局的完成状态与字段数必须与语义声明一致
        if (structTypes.size() != source->structRegistry().declarations().size()) {
            throw std::runtime_error("Backend struct declaration list is incomplete");
        }
        for (const auto& [id, type] : structTypes) {
            const auto* declaration = source->resolveStruct(id);
            if (!type || !declaration || declaration->isComplete() != type->isComplete() ||
                (declaration->isComplete() && declaration->getFields().size() != type->getFields().size())) {
                throw std::runtime_error("Invalid backend struct layout");
            }
        }
        // runtimeFunctions 必须恰好是可见函数中来自 __runtime 模块的那些，且都是外部声明
        std::unordered_set<const BackendFunction*> runtime;
        for (const auto* function : runtimeFunctions) {
            if (!visible.contains(function) || function->parent->ID != "__runtime" ||
                function->linkage != Linkage::External || !runtime.insert(function).second) {
                throw std::runtime_error("Invalid backend runtime declaration");
            }
        }
        for (const auto* function : visible) {
            if ((function->parent->ID == "__runtime") != runtime.contains(function)) {
                throw std::runtime_error("Backend runtime declaration list is incomplete");
            }
        }
    }

    // 生成 BackendModule 的可读文本转储（结构体布局、函数签名/ABI/GC root、各 side table 规模、
    // import/using/runtime 列表），用于调试与测试断言。
    fzlib::String BackendModule::toString() const {
        // ABI 种类的文本名
        const auto abiName = [](ABIValueKind kind) {
            switch (kind) {
                case ABIValueKind::Void: return "void";
                case ABIValueKind::Raw: return "raw";
                case ABIValueKind::Boxed: return "boxed";
            }
            throw std::runtime_error("Invalid backend ABI value kind");
        };
        // 具名结构体布局可能经由指针递归引用自身，因此结构体只打印名称，不递归展开字段。
        std::function<fzlib::String(IRType*)> typeName = [&](IRType* type) -> fzlib::String {
            if (type->isStruct()) return "struct " + static_cast<IRStructType*>(type)->getName();
            if (type->isPointer()) return typeName(static_cast<IRPointerType*>(type)->getElementType()) + "*";
            if (type->isRef()) return typeName(static_cast<IRRefType*>(type)->getElementType()) + "&";
            if (type->isArray()) {
                auto* array = static_cast<IRArrayType*>(type);
                return typeName(array->getElementType()) + "[" + std::to_string(array->getNumElements()) + "]";
            }
            if (type->getIRTypeID() == IRTypeID::FunctionTyID) return "function";
            return type->toString();
        };
        // 依次输出：结构体布局（含 complete/opaque 状态与字段）、本模块函数、import、using 模块、runtime 声明
        fzlib::String result = "backend module " + ID + " {\n";
        for (const auto& [id, type] : structTypes) {
            result += "  struct " + id.module + ":" + std::to_string(id.localId) + " " + type->getName();
            result += type->isComplete() ? " complete\n" : " opaque\n";
            for (const auto& field : type->getFields()) result += "    " + field.name + ": " + typeName(field.type) + "\n";
        }
        // 输出单个函数：链接名与链接方式、每个参数的 storage 类型/ABI/是否 root、返回 ABI 及 side table 条目数
        const auto appendFunction = [&](const BackendFunction& function, bool imported) {
            result += imported ? "  import " : "  function ";
            result += function.sourceName + " linkage=" + function.linkageName;
            result += function.linkage == Linkage::External ? " external(" : " definition(";
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (i) result += ", ";
                result += function.parameters[i].first + ": " + typeName(function.parameters[i].second) + " [" + abiName(function.abi.parameters[i]);
                if (function.gcInfo.parameterRoots[i]) result += ", root";
                result += "]";
            }
            result += ") -> " + typeName(function.returnType) + " [" + abiName(function.abi.result) + "]";
            result += " values=" + std::to_string(function.valueTypes.size());
            result += " type-operands=" + std::to_string(function.typeOperands.size());
            result += " field-accesses=" + std::to_string(function.fieldAccesses.size());
            result += " roots=" + std::to_string(function.gcInfo.rootedValues.size());
            result += " root-slots=" + std::to_string(function.gcInfo.rootedSlots.size()) + "\n";
        };
        for (const auto& function : functions) appendFunction(*function, false);
        for (const auto* function : imports) appendFunction(*function, true);
        for (const auto* module : usingModules) result += "  using " + module->ID + "\n";
        for (const auto* function : runtimeFunctions) result += "  runtime " + function->linkageName + "\n";
        return result + "}";
    }

    // null 句柄常量无论语义类型如何都记为 Null；其余按语义类型映射，复合常量只支持数组与类型描述符。
    ConstantKind LoweringSession::lowerConstantKind(const Constant& constant) const {
        if (constant.isNullHandle()) return ConstantKind::Null;
        switch (constant.getSemanticType()->getTypeID()) {
            case TypeID::Char: return ConstantKind::I8;
            case TypeID::Int32: return ConstantKind::I32;
            case TypeID::Int64: return ConstantKind::I64;
            case TypeID::UInt32: return ConstantKind::U32;
            case TypeID::UInt64: return ConstantKind::U64;
            case TypeID::Float32: return ConstantKind::F32;
            case TypeID::Float64: return ConstantKind::F64;
            case TypeID::Bool: return ConstantKind::Bool;
            case TypeID::String: return ConstantKind::String;
            case TypeID::Array: return ConstantKind::Array;
            case TypeID::TypeDescriptor: return ConstantKind::Descriptor;
            default: throw std::runtime_error("Unsupported semantic constant kind for backend lowering");
        }
    }

    // 运行时类型元数据驻留：同一基础 TypeInfo 只生成一个 RuntimeTypeDescription，后端可按指针比较。
    // storage 指向对应的 storage 类型(IRType)，representation 描述值在运行时的表示形式
    //（默认装箱的 RuntimeValue，数组为托管数组，引用为 Reference，结构体为句柄，void/函数不是值）。
    const RuntimeTypeDescription* LoweringSession::lowerRuntimeType(const TypeInfo* type) {
        // 先 lowering storage 类型（含结构体按需补全）；nullable 与其基础类型共享 storage 类型与运行时描述，
        // 因此后续以去除 nullable 修饰的基础类型为驻留键。
        auto* storage = lowerType(type);
        type = type->getBase();
        if (const auto found = runtimeTypeMap.find(type); found != runtimeTypeMap.end()) return found->second.get();
        auto description = std::make_unique<RuntimeTypeDescription>();
        description->storage = storage;
        description->representation = ValueRepresentation::Boxed;
        using Kind = RuntimeTypeKind;
        using Tag = RuntimeValueTag;
        switch (type->getTypeID()) {
            case TypeID::Void:
                description->kind = Kind::Void;
                description->representation = ValueRepresentation::NonValue;
                break;
            case TypeID::Char: description->kind = Kind::I8; description->tag = Tag::I8; break;
            case TypeID::Int32: description->kind = Kind::I32; description->tag = Tag::I32; break;
            case TypeID::Int64: description->kind = Kind::I64; description->tag = Tag::I64; break;
            case TypeID::UInt32: description->kind = Kind::U32; description->tag = Tag::U32; break;
            case TypeID::UInt64: description->kind = Kind::U64; description->tag = Tag::U64; break;
            case TypeID::Float32: description->kind = Kind::F32; description->tag = Tag::F32; break;
            case TypeID::Float64: description->kind = Kind::F64; description->tag = Tag::F64; break;
            case TypeID::Bool: description->kind = Kind::Bool; description->tag = Tag::Bool; break;
            case TypeID::String: description->kind = Kind::String; description->tag = Tag::String; break;
            // 指针、引用与数组递归描述元素类型；结构体不展开字段，因此递归不会因自引用结构体成环。
            case TypeID::Pointer:
                description->kind = Kind::Pointer;
                description->tag = Tag::Pointer;
                description->element = lowerRuntimeType(type->getPointeeType());
                break;
            case TypeID::Ref:
                description->kind = Kind::Reference;
                // 引用在运行时复用指针标签，但表示形式为 Reference
                description->tag = Tag::Pointer;
                description->representation = ValueRepresentation::Reference;
                description->element = lowerRuntimeType(type->getPointeeType());
                break;
            case TypeID::Array:
                description->kind = Kind::Array;
                description->tag = Tag::Array;
                description->representation = ValueRepresentation::ManagedArray;
                description->element = lowerRuntimeType(type->getElementType());
                description->count = std::get<ArrayTypeInfo>(type->getComplexTypeInfo()).length();
                break;
            case TypeID::Struct: {
                description->kind = Kind::Struct;
                description->tag = Tag::Struct;
                description->representation = ValueRepresentation::Handle;
                const auto& id = *type->getStructDeclId();
                // 当前运行时的结构体工厂是按名义区分的，没有字段图 API；
                // 因此只记录 "模块:局部ID:名称" 作为唯一身份，由后端按身份获取运行时结构体类型。
                description->identity = id.module + ":" + std::to_string(id.localId) + ":" +
                    static_cast<IRStructType*>(storage)->getName();
                break;
            }
            case TypeID::FunctionType:
                description->kind = Kind::Function;
                description->representation = ValueRepresentation::NonValue;
                break;
            case TypeID::TypeDescriptor:
                description->kind = Kind::Descriptor;
                description->tag = Tag::Descriptor;
                break;
            default: throw std::runtime_error("Unsupported runtime description kind");
        }
        // 只有构造完成后才登记；递归只处理组件类型（结构体不展开字段），不会再次登记当前类型，emplace 不会冲突
        const auto* result = description.get();
        runtimeTypeMap.emplace(type, std::move(description));
        return result;
    }

    // 解析 gmem 指令：操作数 0 为结构体基址（不能是 null 常量），操作数 1 为字段名常量字符串。
    // 在语义声明中按名称定位字段，再与 lowering 后的 storage 布局逐项交叉核对（名称、下标、storage 类型），
    // 保证后端按下标做 GEP 时与语义字段一一对应。
    BackendFieldAccess LoweringSession::lowerFieldAccess(const Instruction& instruction) {
        const auto& operands = instruction.getOperands();
        if (operands.size() != 2 || !operands[0] || !operands[1]) {
            throw std::runtime_error("Struct member access requires a base and a constant string field selector");
        }
        const auto* base = operands[0];
        if (const auto* constant = dynamic_cast<const Constant*>(base); constant && constant->isNullHandle()) {
            throw std::runtime_error("Struct member access cannot use a null struct base");
        }
        // 基址可以是 nullable 结构体，判定与声明解析都基于去除 nullable 修饰的基础类型
        const auto* sourceType = base->getSemanticType();
        if (!sourceType || !sourceType->getBase()->isStruct()) {
            throw std::runtime_error("Struct member access requires a struct base type");
        }
        const auto* selector = dynamic_cast<const Constant*>(operands[1]);
        if (!selector || !selector->getSemanticType() ||
            selector->getSemanticType()->getTypeID() != TypeID::String || selector->isNullHandle()) {
            throw std::runtime_error("Struct member selector must be a constant string payload");
        }
        const auto& name = selector->getContentValue<fzlib::String>();
        // 只有完整的结构体声明才有字段布局；opaque 结构体不能做成员访问
        const auto* declaration = structResolver ? structResolver->resolveStruct(*sourceType->getBase()->getStructDeclId()) : nullptr;
        if (!declaration || !declaration->isComplete()) {
            throw std::runtime_error("Struct member access requires a complete struct declaration");
        }
        const auto& fields = declaration->getFields();
        const auto found = std::find_if(fields.begin(), fields.end(), [&](const SemanticField& field) {
            return field.name == name;
        });
        if (found == fields.end()) {
            throw std::runtime_error(("Unknown struct member: " + name).c_str());
        }
        // 指令结果的语义类型必须与声明字段类型是同一个驻留 TypeInfo
        if (instruction.getSemanticType() != found->type) {
            throw std::runtime_error("Struct member result does not match the declared semantic field type");
        }
        // lowerType 会按需补全结构体布局；布局下标与语义声明字段顺序一致
        auto* layout = dynamic_cast<IRStructType*>(lowerType(sourceType));
        const auto index = static_cast<std::size_t>(found - fields.begin());
        const auto* type = lowerRuntimeType(found->type);
        if (!layout || !layout->isComplete() || index >= layout->getFields().size() ||
            layout->getFields()[index].name != name || layout->getFields()[index].type != type->storage ||
            lowerType(instruction.getSemanticType()) != type->storage) {
            throw std::runtime_error("Struct member storage does not match its lowered field layout");
        }
        return {layout, index, type, fieldStorage(*type)};
    }

    // （重新）lowering 单个函数的全部内容。BackendFunction 对象本身保持不变，各表先清空再重建，
    // 使已分发出去的 BackendFunction* 在重复 lowering 后仍然有效。
    void LoweringSession::lowerFunctionContents(BackendFunction& function) {
        const auto& source = *function.source;
        function.sourceName = source.getName();
        // __runtime 模块中的函数是运行时库提供的外部声明，其余模块的函数都是定义；
        // 外部声明优先使用原始名称（未经模块限定的源码名）作为链接名，与 C 运行时符号对应；否则使用 getName() 的名称。
        function.linkage = function.parent->ID == "__runtime" ? Linkage::External : Linkage::Definition;
        function.linkageName = function.linkage == Linkage::External && source.getRawName().len() != 0
            ? source.getRawName() : source.getName();
        function.signature = dynamic_cast<IRFunctionType*>(lowerType(source.getSemanticType()));
        if (!function.signature) throw std::runtime_error("Backend function requires a function signature");
        function.returnType = lowerType(source.getReturnType());
        function.returnRuntimeType = lowerRuntimeType(source.getReturnType());
        // 参数相关的平行数组：名称+storage 类型、ABI、运行时类型元数据、是否为 GC root（托管类型参数需在入口注册）
        function.parameters.clear();
        function.parameterRuntimeTypes.clear();
        function.abi.parameters.clear();
        function.gcInfo.parameterRoots.clear();
        const auto& names = source.getFormalParamNames();
        const auto& types = source.getSemanticParamTypes();
        if (names.size() != types.size()) throw std::runtime_error("Function parameter names do not match its signature");
        std::set<fzlib::String> parameterNames;
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (!parameterNames.insert(names[i]).second) throw std::runtime_error("Duplicate backend parameter name");
            auto* type = lowerType(types[i]);
            function.parameters.emplace_back(names[i], type);
            function.abi.parameters.push_back(parameterABI(type, source.getRawName()));
            auto* runtimeType = lowerRuntimeType(types[i]);
            function.parameterRuntimeTypes.push_back(runtimeType);
            function.gcInfo.parameterRoots.push_back(runtimeType->managed());
        }
        function.abi.result = resultABI(function.returnType, source.getRawName());
        // 清空 side table 后重新遍历函数内所有可达 IRValue 填充
        function.valueTypes.clear();
        function.runtimeTypes.clear();
        function.constants.clear();
        function.typeOperands.clear();
        function.fieldAccesses.clear();
        function.gcInfo.rootedValues.clear();
        function.gcInfo.rootedSlots.clear();
        visitValues(source, [&](const IRValue* value) {
            if (const auto* instruction = dynamic_cast<const Instruction*>(value)) {
                // 指令必须属于本函数的基本块，不允许跨函数引用
                if (!instruction->getParent() || instruction->getParent()->getParent() != &source) {
                    throw std::runtime_error("Backend function references another function's instruction");
                }
                // sizeof 的类型操作数记入 typeOperands side table
                if (instruction->getKind() == OpKind::_sizeof) {
                    function.typeOperands.emplace(value, lowerRuntimeType(instruction->getTypeOperand()));
                }
                // 成员访问在 lowering 期解析为字段布局与下标，后端不再按名称查找
                if (instruction->getKind() == OpKind::gmem) {
                    function.fieldAccesses.emplace(instruction, lowerFieldAccess(*instruction));
                }
                // 逻辑非只接受 bool，提前在 lowering 期拒绝非法 IR
                if (instruction->getKind() == OpKind::lgc_not) {
                    const auto& operands = instruction->getOperands();
                    if (operands.size() != 1 || !operands[0] || !operands[0]->getSemanticType() ||
                        operands[0]->getSemanticType()->getTypeID() != TypeID::Bool ||
                        instruction->getSemanticType()->getTypeID() != TypeID::Bool) {
                        throw std::runtime_error("Logical not requires one bool operand and a bool result");
                    }
                }
            }
            // 常量记录常量种类；类型描述符常量的载荷类型也 lowering 为运行时类型元数据并记入 typeOperands
            if (const auto* constant = dynamic_cast<const Constant*>(value)) {
                const auto kind = lowerConstantKind(*constant);
                function.constants.emplace(value, kind);
                if (kind == ConstantKind::Descriptor) {
                    function.typeOperands.emplace(value, lowerRuntimeType(constant->getTypePayload()));
                }
            }
            // 每个值都记录 storage 类型与运行时类型元数据，并按共享规则生成 GC 描述
            auto* type = lowerType(value->getSemanticType());
            function.valueTypes.emplace(value, type);
            const auto* runtimeType = lowerRuntimeType(value->getSemanticType());
            function.runtimeTypes.emplace(value, runtimeType);
            if (rootValue(value, *runtimeType)) function.gcInfo.rootedValues.insert(value);
            // 承载托管类型的 alloca 槽位整体作为 GC root 注册
            if (const auto* instruction = dynamic_cast<const Instruction*>(value);
                instruction && instruction->getKind() == OpKind::create_alloca && runtimeType->managed()) {
                function.gcInfo.rootedSlots.insert(value);
            }
        });
    }

    // 刷新单个 BackendModule：为新出现的源函数创建 BackendFunction（由模块拥有，functionMap 记录非拥有指针），
    // 已存在的函数原地重新 lowering；再 lowering 模块声明的全部结构体，使 structTypes 反映最新的完成状态。
    void LoweringSession::refreshModule(const Module& source) {
        auto& module = *backendModules.at(&source);
        for (const auto* function : source.getFunctions()) {
            if (!function || function->getParent() != &source) throw std::runtime_error("Invalid function module ownership");
            // 首次遇到的函数才新建，保证同一源函数在会话内始终对应同一个 BackendFunction
            auto found = functionMap.find(function);
            if (found == functionMap.end()) {
                auto lowered = std::make_unique<BackendFunction>();
                lowered->source = function;
                lowered->parent = &module;
                auto* result = lowered.get();
                module.functions.push_back(std::move(lowered));
                functionMap.emplace(function, result);
                found = functionMap.find(function);
            }
            lowerFunctionContents(*found->second);
        }
        // 通过结构体 TypeInfo 触发按需 lowering；opaque 声明得到未完成的布局，声明完成后再次刷新会原地补全。
        // insert_or_assign 覆盖旧条目，但同一 StructDeclId 的布局对象本身不变。
        for (const auto& [name, declaration] : source.structRegistry().declarations()) {
            const auto id = declaration->getDeclId();
            auto* type = lowerType(irContext.typeInfoPool().makeStructTypeID(id));
            module.structTypes.insert_or_assign(id, static_cast<IRStructType*>(type));
        }
    }

    // 计算模块的可见范围并整体替换旧结果（先在局部容器中构建，全部检查通过后再 swap，失败时不留下半成品）。
    void LoweringSession::populateModule(BackendModule& module) {
        std::map<fzlib::String, const BackendFunction*> symbols;
        std::vector<const BackendFunction*> imports;
        std::vector<const BackendFunction*> runtimeFunctions;
        std::vector<BackendModule*> usingModules;
        // 沿 using 列表做传递闭包遍历：每个模块只访问一次、每个函数只登记一次；
        // 非本模块的函数记为 import，来自 __runtime 的函数同时记入 runtime 声明列表。
        // 同名符号指向不同函数时 addSymbol 抛出符号冲突。
        std::unordered_set<const BackendModule*> visited;
        std::unordered_set<const BackendFunction*> seenFunctions;
        std::function<void(const BackendModule&)> visit = [&](const BackendModule& current) {
            if (!visited.insert(&current).second) return;
            for (const auto& function : current.functions) {
                if (!seenFunctions.insert(function.get()).second) continue;
                addSymbol(symbols, function->sourceName, function.get());
                addSymbol(symbols, function->linkageName, function.get());
                if (&current != &module) imports.push_back(function.get());
                if (current.ID == "__runtime") runtimeFunctions.push_back(function.get());
            }
            for (const auto* source : current.source->getUsingList()) visit(*backendModules.at(source));
        };
        visit(module);
        // usingModules 只记录直接 using 的模块，并去除重复项
        std::unordered_set<BackendModule*> seenModules;
        for (const auto* source : module.source->getUsingList()) {
            auto* imported = backendModules.at(source).get();
            if (seenModules.insert(imported).second) usingModules.push_back(imported);
        }
        // 调用指令名形如 "call.<被调函数名>"（见 IRGenerator 生成 call 指令处），
        // 去掉 5 个字符的 "call." 前缀后必须能在可见符号表中找到，否则说明调用目标不在模块作用域内。
        for (const auto& function : module.functions) {
            for (const auto* block : function->source->getBlocks()) {
                for (const auto* instruction : block->getInstructions()) {
                    if (instruction->getKind() != OpKind::call) continue;
                    const auto& name = instruction->getName();
                    if (name.len() < 5 || !symbols.contains(fzlib::String(name.c_str() + 5, name.len() - 5))) {
                        throw std::runtime_error("Backend call target is not in module scope");
                    }
                }
            }
        }
        module.symbols.swap(symbols);
        module.imports.swap(imports);
        module.usingModules.swap(usingModules);
        module.runtimeFunctions.swap(runtimeFunctions);
    }

    // lowering 入口：先收集 module 的传递 using 闭包并逐个校验归属，再分阶段处理：
    // 1) 为尚未 lowering 的模块创建 BackendModule（已有的复用，保持指针稳定）；
    // 2) 刷新所有模块的函数与结构体；3) 在全部函数就绪后构建符号表与 import（依赖其他模块的 linkageName）；
    // 4) 逐个 verify。分阶段是因为 populateModule 需要闭包内所有模块都已刷新完成。
    BackendModule* LoweringSession::lowerModule(const Module& module) {
        std::vector<const Module*> modules;
        std::unordered_set<const Module*> visited;
        std::function<void(const Module&)> visit = [&](const Module& source) {
            if (!visited.insert(&source).second) return;
            validateModule(source);
            modules.push_back(&source);
            for (const auto* imported : source.getUsingList()) {
                if (!imported) throw std::runtime_error("Module has a null import");
                visit(*imported);
            }
        };
        visit(module);
        for (const auto* source : modules) {
            if (backendModules.contains(source)) continue;
            auto lowered = std::make_unique<BackendModule>();
            lowered->source = source;
            lowered->context = &irContext;
            lowered->ID = source->id();
            backendModules.emplace(source, std::move(lowered));
        }
        for (const auto* source : modules) refreshModule(*source);
        for (const auto* source : modules) populateModule(*backendModules.at(source));
        for (const auto* source : modules) backendModules.at(source)->verify();
        return backendModules.at(&module).get();
    }

    // 函数必须确实位于其父模块的函数列表中；以整个父模块为单位 lowering，以便同时得到符号表与 import 信息。
    BackendFunction* LoweringSession::lowerFunction(const Function& function) {
        const auto* parent = function.getParent();
        if (!parent || std::find(parent->getFunctions().begin(), parent->getFunctions().end(), &function) == parent->getFunctions().end()) {
            throw std::runtime_error("Function is not owned by its source module");
        }
        lowerModule(*parent);
        return functionMap.at(&function);
    }

    // 结构体布局合法性检查（DFS）：指针/引用字段不需要被指向类型的完整布局，因此在此终止，允许经指针递归；
    // 数组元素与按值嵌套的结构体需要继续展开。visiting 中再次遇到同一声明即为按值递归环（大小无限）；
    // opaque 声明暂时跳过，待其完成后再检查。
    void LoweringSession::validateStruct(const StructDeclId& id) const {
        std::set<StructDeclId> visiting;
        std::set<StructDeclId> visited;
        std::function<void(const TypeInfo*)> visitType;
        std::function<void(const StructDeclId&)> visitStruct;

        // 字段类型检查：指针/引用直接终止；数组看元素；结构体按值展开；void 与函数类型没有 storage 表示
        visitType = [&](const TypeInfo* type) {
            if (!type) throw std::runtime_error("Struct field has no semantic type");
            if (type->isPointer() || type->isRef()) return;
            if (type->isArray()) {
                visitType(type->getElementType());
            }
            else if (type->isStruct()) {
                visitStruct(*type->getStructDeclId());
            }
            else if (type->isFunction() || type->getTypeID() == TypeID::Void) {
                throw std::runtime_error("Struct field requires a storage type");
            }
        };
        // 结构体检查：visiting 记录当前 DFS 路径，visited 记录已确认无环的声明以避免重复展开
        visitStruct = [&](const StructDeclId& declarationId) {
            if (visiting.contains(declarationId)) {
                throw std::runtime_error("Struct layout contains a by-value recursive cycle");
            }
            if (visited.contains(declarationId)) return;
            const auto* declaration = structResolver->resolveStruct(declarationId);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");
            // opaque 声明尚无字段，暂不参与环检测
            if (!declaration->isComplete()) return;
            visiting.insert(declarationId);
            for (const auto& field : declaration->getFields()) visitType(field.type);
            visiting.erase(declarationId);
            visited.insert(declarationId);
        };
        visitStruct(id);
    }

    // 基础类型映射：string 的 storage 类型为 char*，类型描述符映射为 IRContext 驻留的 IRTypeInfoType；
    // 各 case 中 return 之后的 break 不可达，仅为保持格式统一。
    IRType* LoweringSession::loweringBasicType(const TypeInfo* tyInfo) const {
        if (!tyInfo) {
            throw std::runtime_error("cannot give a null typeinfo to lowering");
        }

        switch (tyInfo->getTypeID()) {
            case TypeID::Int32: {
                return irContext.getInt32Ty();
                break;
            }
            case TypeID::Int64: {
                return irContext.getInt64Ty();
                break;
            }
            case TypeID::UInt32: {
                return irContext.getUInt32Ty();
                break;
            }
            case TypeID::UInt64: {
                return irContext.getUInt64Ty();
                break;
            }
            case TypeID::Float32: {
                return irContext.getFloat32Ty();
                break;
            }
            case TypeID::Float64: {
                return irContext.getFloat64Ty();
                break;
            }
            case TypeID::Bool: {
                return irContext.getBoolTy();
                break;
            }
            case TypeID::Char: {
                return irContext.getCharTy();
                break;
            }
            case TypeID::Void: {
                return irContext.getVoidTy();
                break;
            }
            case TypeID::String: {
                return irContext.getPointerTo(irContext.getCharTy());
                break;
            }
            case TypeID::TypeDescriptor: return irContext.getTypeInfoTy();
            default: throw std::runtime_error("method: 'loweringBasicType' only receive basic type info");
        }
    }

    IRType *LoweringSession::lowerType(const TypeInfo *tyInfo) {
        PendingLowering pending;
        auto* result = lowerTypeSafer(tyInfo, pending);
        // 状态机入口：本次转换未遇到未完成的结构体声明时，整张类型图已稳定，可以把 pending 中的结果全部写入缓存；
        // 否则整批结果都不缓存，保留重新转换的机会，以便语义声明完成后在同一 IRStructType 对象上原地补齐布局
        //（指向该结构体的指针/数组等 storage 类型由 IRContext 驻留，因此重新转换得到的仍是同一对象）。
        if (!pending.opaqueStruct) {
            typeMap.insert(pending.results.begin(), pending.results.end());
        }
        return result;
    }

    // 递归转换规则：数组/指针/引用/函数类型先转换组件再从 IRContext 获取驻留的复合 storage 类型；
    // nullable 包装与其基础类型共享 typeID 与组件，因此得到相同的 storage 类型；基础类型交给 loweringBasicType。
    IRType *LoweringSession::lowerTypeSafer(const TypeInfo *tyInfo, PendingLowering& pending) {
        if (!tyInfo) {
            throw std::runtime_error("cannot give a null typeinfo to lowering");
        }
        // TypeInfo 必须来自本会话的 IRContext，否则其驻留指针与本会话的缓存无法对应
        if (&tyInfo->getContext() != &irContext) {
            throw std::runtime_error("TypeInfo does not belong to this LoweringSession's IRContext");
        }
        // 只命中已稳定的缓存；本次调用内的中间结果在 pending 中，不参与查找
        if (const auto it = typeMap.find(tyInfo); it != typeMap.end()) return it->second;

        IRType* result = nullptr;

        if (tyInfo->isArray()) {
            const auto& arrayType = std::get<ArrayTypeInfo>(tyInfo->getComplexTypeInfo());
            result = irContext.getArrayTy(lowerTypeSafer(arrayType.getElementTy(), pending), arrayType.length());
        }
        else if (tyInfo->isPointer()) {
            const auto& pointeeType = std::get<PointerTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            result = irContext.getPointerTo(lowerTypeSafer(pointeeType, pending));
        }
        else if (tyInfo->isRef()) {
            const auto& refType = std::get<RefTypeInfo>(tyInfo->getComplexTypeInfo()).getElementTy();
            result = irContext.getRefTo(lowerTypeSafer(refType, pending));
        }
        else if (tyInfo->isStruct()) {
            // 结构体：按需 lowering，状态如下
            // - 未完成声明：返回 opaque 布局并标记 pending.opaqueStruct，阻止整批缓存；
            // - 正在填充（fillingStructs 中）：经指针递归回到自身的回边，直接返回同一布局对象；
            // - 已完成声明：先 validateStruct，再逐字段 lowering 并原地 complete 该布局。
            const auto& structDecl = std::get<StructTypeInfo>(tyInfo->getComplexTypeInfo()).getDeclId();

            if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
            const auto* declaration = structResolver->resolveStruct(structDecl);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");

            // storage 类型由声明所属的 NamingContext 拥有并按 StructDeclId 复用，本会话只记录非拥有指针
            const auto* owner = structResolver->resolveNamingContext(structDecl);
            if (!owner) throw std::runtime_error("Semantic struct declaration has no NamingContext");
            auto* lowered = owner->getStructTy(irContext, structDecl);
            structMap.try_emplace(structDecl, lowered);
            if (!declaration->isComplete()) {
                pending.opaqueStruct = true;
                return lowered;
            }
            // 正在填充的结构体是递归回边（必经指针/引用，按值递归已被 validateStruct 拒绝），外层调用完成后其布局即完整
            if (fillingStructs.contains(structDecl)) return lowered;
            validateStruct(structDecl);

            // 填充期间登记到 fillingStructs；异常时也要移除，避免该声明在后续调用中被永久误判为回边
            fillingStructs.insert(structDecl);
            try {
                std::vector<IRStructType::FieldInfo> fields;
                fields.reserve(declaration->getFields().size());
                for (const auto& field : declaration->getFields()) {
                    IRStructType::FieldInfo loweredField;
                    loweredField.name = field.name;
                    loweredField.type = lowerTypeSafer(field.type, pending);
                    loweredField.info = field.info;
                    fields.push_back(std::move(loweredField));
                }
                // 布局只能补全一次：此前调用若已补全（例如结果因其他 opaque 成员未缓存而重新转换），则保留原布局
                if (!lowered->isComplete()) lowered->complete(std::move(fields));
            }
            catch (...) {
                fillingStructs.erase(structDecl);
                throw;
            }
            fillingStructs.erase(structDecl);
            result = lowered;
        }
        else if (tyInfo->isFunction()) {
            // 函数类型：参数与返回类型逐个 lowering 后获取驻留的函数签名类型
            const auto& funcTy = std::get<FunctionTypeInfo>(tyInfo->getComplexTypeInfo());

            std::vector<IRType*> irArgs;
            for (const auto& arg : funcTy.getArgTypes()) {
                irArgs.push_back(lowerTypeSafer(arg, pending));
            }

            result = irContext.getFunctionTy(lowerTypeSafer(funcTy.getReturnType(), pending), irArgs);
        }
        else {
            result = loweringBasicType(tyInfo);
        }
        // 记入本次调用的待提交结果；是否写入 typeMap 由 lowerType 根据 opaqueStruct 决定
        pending.results.emplace(tyInfo, result);

        return result;
    }
}
