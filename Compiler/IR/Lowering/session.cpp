#include "session.hpp"

#include "Compiler/IR/Backend/context/context.hpp"
#include "Compiler/IR/Semantic/context/naming_context.hpp"
#include "Compiler/IR/Semantic/model/program.hpp"
#include <unordered_set>
#include <algorithm>
#include <functional>

namespace sakuraE::IR {
    namespace {
        ABIValueKind resultABI(IRType* type, const fzlib::String& name) {
            if (type->getIRTypeID() == IRTypeID::VoidTyID) return ABIValueKind::Void;
            if (name == "__gc_alloc" || name == "__alloc" || name == "__free" ||
                name == "__gc_register" || name == "__gc_register_value" ||
                name == "__gc_pop" || name == "__gc_get_struct_type" ||
                name == "__runtime_type_info_basic" || name == "__runtime_type_info_array" ||
                name == "__runtime_check_array_bounds" || name == "__runtime_array_bounds_error") {
                return ABIValueKind::Raw;
            }
            return ABIValueKind::Boxed;
        }

        ABIValueKind parameterABI(IRType* type, const fzlib::String& name) {
            if (((name == "create_string" || name == "__gc_register_value") && type->isPointer()) ||
                name == "__gc_get_struct_type") return ABIValueKind::Raw;
            return resultABI(type, name);
        }

        bool managedType(IRType* type) {
            return type->isArray() || (type->isPointer() &&
                static_cast<IRPointerType*>(type)->getElementType()->getIRTypeID() == IRTypeID::CharTyID);
        }

        bool rootValue(const IRValue* value, IRType* type) {
            if (!managedType(type)) return false;
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

        void addSymbol(std::map<fzlib::String, const BackendFunction*>& symbols,
                       const fzlib::String& name, const BackendFunction* function) {
            if (name.len() == 0) throw std::runtime_error("Backend function has an empty symbol name");
            const auto [it, inserted] = symbols.emplace(name, function);
            if (!inserted && it->second != function) {
                throw std::runtime_error(("Conflicting backend function symbol: " + name).c_str());
            }
        }

        template<typename Visitor>
        void visitValues(const Function& function, Visitor&& visitor) {
            std::unordered_set<const IRValue*> visited;
            std::function<void(const IRValue*)> visit = [&](const IRValue* value) {
                // Uninitialized local storage uses a null optional initializer operand.
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
            visit(&function);
            for (const auto* block : function.getBlocks()) visit(block);
        }
    }

    IRType* BackendFunction::storageType(const IRValue* value) const {
        const auto found = valueTypes.find(value);
        if (found == valueTypes.end() || !found->second) {
            throw std::runtime_error("Value has no lowered backend storage type");
        }
        return found->second;
    }

    bool BackendFunction::tracksRoot(const IRValue* value) const {
        return gcInfo.rootedValues.contains(value);
    }

    bool BackendFunction::rootsSlot(const IRValue* value) const {
        return gcInfo.rootedSlots.contains(value);
    }

    const BackendFunction* BackendModule::lookup(const fzlib::String& name) const {
        const auto found = symbols.find(name);
        return found == symbols.end() ? nullptr : found->second;
    }

    void LoweringSession::validateModule(const Module& module) const {
        const auto* program = module.getSourceProgram();
        if (!program || &program->getContext() != &irContext || structResolver != program ||
            std::find(program->getMods().begin(), program->getMods().end(), &module) == program->getMods().end()) {
            throw std::runtime_error("Module does not belong to this LoweringSession's Program");
        }
    }

    void BackendModule::verify() const {
        if (!source || !context || ID != source->id()) throw std::runtime_error("Invalid backend module identity");
        if (functions.size() != source->getFunctions().size()) throw std::runtime_error("Backend module function list is incomplete");
        std::unordered_set<const BackendFunction*> visible;
        for (const auto& function : functions) {
            if (!function || !visible.insert(function.get()).second || function->parent != this || !function->source ||
                function->source->getParent() != source) throw std::runtime_error("Invalid backend function ownership");
        }
        for (const auto* function : imports) {
            if (!function || !function->parent || function->parent == this || !visible.insert(function).second ||
                function->parent->context != context) throw std::runtime_error("Invalid or duplicate backend import");
        }
        std::map<fzlib::String, const BackendFunction*> expectedSymbols;
        for (const auto* function : visible) {
            if (!function->source || !function->signature || !function->returnType ||
                function->sourceName != function->source->getName() || function->linkageName.len() == 0 ||
                function->signature->getReturnType() != function->returnType ||
                function->storageType(function->source) != function->signature ||
                function->parameters.size() != function->source->getSemanticParamTypes().size() ||
                function->abi.parameters.size() != function->parameters.size() ||
                function->gcInfo.parameterRoots.size() != function->parameters.size()) {
                throw std::runtime_error("Invalid backend function signature or metadata");
            }
            addSymbol(expectedSymbols, function->sourceName, function);
            addSymbol(expectedSymbols, function->linkageName, function);
            if (function->abi.result != resultABI(function->returnType, function->source->getRawName())) {
                throw std::runtime_error("Invalid backend result ABI");
            }
            for (std::size_t i = 0; i < function->parameters.size(); ++i) {
                if (!function->parameters[i].second || function->parameters[i].first != function->source->getFormalParamNames()[i] ||
                    function->abi.parameters[i] != parameterABI(function->parameters[i].second, function->source->getRawName()) ||
                    function->gcInfo.parameterRoots[i] != managedType(function->parameters[i].second)) {
                    throw std::runtime_error("Invalid backend parameter metadata");
                }
            }
            visitValues(*function->source, [&](const IRValue* value) {
                auto* type = function->storageType(value);
                if (function->tracksRoot(value) != rootValue(value, type)) throw std::runtime_error("Invalid backend GC root metadata");
                if (const auto* instruction = dynamic_cast<const Instruction*>(value)) {
                    if (function->rootsSlot(value) != (instruction->getKind() == OpKind::create_alloca && managedType(type))) {
                        throw std::runtime_error("Invalid backend GC slot metadata");
                    }
                    if (instruction->getKind() == OpKind::_sizeof &&
                        (!function->typeOperands.contains(value) || !function->typeOperands.at(value))) {
                        throw std::runtime_error("Backend sizeof is missing its lowered type operand");
                    }
                }
                if (const auto* constant = dynamic_cast<const Constant*>(value);
                    constant && constant->getTypePayload() &&
                    (!function->typeOperands.contains(value) || !function->typeOperands.at(value))) {
                    throw std::runtime_error("Backend descriptor is missing its lowered type payload");
                }
            });
            for (const auto& [value, type] : function->typeOperands) {
                if (!type || !function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend type operand table");
            }
            for (const auto* value : function->gcInfo.rootedValues) {
                if (!function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend GC root table");
            }
            for (const auto* value : function->gcInfo.rootedSlots) {
                if (!function->valueTypes.contains(value)) throw std::runtime_error("Invalid backend GC slot table");
            }
        }
        if (symbols != expectedSymbols) throw std::runtime_error("Backend symbol table does not match visible functions");
        std::unordered_set<const BackendModule*> seenModules;
        for (const auto* module : usingModules) {
            if (!module || module->context != context || !seenModules.insert(module).second) {
                throw std::runtime_error("Invalid backend using module");
            }
        }
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

    fzlib::String BackendModule::toString() const {
        const auto abiName = [](ABIValueKind kind) {
            switch (kind) {
                case ABIValueKind::Void: return "void";
                case ABIValueKind::Raw: return "raw";
                case ABIValueKind::Boxed: return "boxed";
            }
            throw std::runtime_error("Invalid backend ABI value kind");
        };
        // Named layouts may be recursive through pointers; do not recursively expand them.
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
        fzlib::String result = "backend module " + ID + " {\n";
        for (const auto& [id, type] : structTypes) {
            result += "  struct " + id.module + ":" + std::to_string(id.localId) + " " + type->getName();
            result += type->isComplete() ? " complete\n" : " opaque\n";
            for (const auto& field : type->getFields()) result += "    " + field.name + ": " + typeName(field.type) + "\n";
        }
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
            result += " roots=" + std::to_string(function.gcInfo.rootedValues.size());
            result += " root-slots=" + std::to_string(function.gcInfo.rootedSlots.size()) + "\n";
        };
        for (const auto& function : functions) appendFunction(*function, false);
        for (const auto* function : imports) appendFunction(*function, true);
        for (const auto* module : usingModules) result += "  using " + module->ID + "\n";
        for (const auto* function : runtimeFunctions) result += "  runtime " + function->linkageName + "\n";
        return result + "}";
    }

    void LoweringSession::lowerFunctionContents(BackendFunction& function) {
        const auto& source = *function.source;
        function.sourceName = source.getName();
        function.linkage = function.parent->ID == "__runtime" ? Linkage::External : Linkage::Definition;
        function.linkageName = function.linkage == Linkage::External && source.getRawName().len() != 0
            ? source.getRawName() : source.getName();
        function.signature = dynamic_cast<IRFunctionType*>(lowerType(source.getSemanticType()));
        if (!function.signature) throw std::runtime_error("Backend function requires a function signature");
        function.returnType = lowerType(source.getReturnType());
        function.parameters.clear();
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
            function.gcInfo.parameterRoots.push_back(managedType(type));
        }
        function.abi.result = resultABI(function.returnType, source.getRawName());
        function.valueTypes.clear();
        function.typeOperands.clear();
        function.gcInfo.rootedValues.clear();
        function.gcInfo.rootedSlots.clear();
        visitValues(source, [&](const IRValue* value) {
            if (const auto* instruction = dynamic_cast<const Instruction*>(value)) {
                if (!instruction->getParent() || instruction->getParent()->getParent() != &source) {
                    throw std::runtime_error("Backend function references another function's instruction");
                }
                if (instruction->getKind() == OpKind::_sizeof) {
                    function.typeOperands.emplace(value, lowerType(instruction->getTypeOperand()));
                }
            }
            if (const auto* constant = dynamic_cast<const Constant*>(value)) {
                if (constant->getSemanticType()->getTypeID() == TypeID::TypeDescriptor && !constant->isNullHandle()) {
                    function.typeOperands.emplace(value, lowerType(constant->getTypePayload()));
                }
            }
            auto* type = lowerType(value->getSemanticType());
            function.valueTypes.emplace(value, type);
            if (rootValue(value, type)) function.gcInfo.rootedValues.insert(value);
            if (const auto* instruction = dynamic_cast<const Instruction*>(value);
                instruction && instruction->getKind() == OpKind::create_alloca && managedType(type)) {
                function.gcInfo.rootedSlots.insert(value);
            }
        });
    }

    void LoweringSession::refreshModule(const Module& source) {
        auto& module = *backendModules.at(&source);
        for (const auto* function : source.getFunctions()) {
            if (!function || function->getParent() != &source) throw std::runtime_error("Invalid function module ownership");
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
        for (const auto& [name, declaration] : source.structRegistry().declarations()) {
            const auto id = declaration->getDeclId();
            auto* type = lowerType(irContext.typeInfoPool().makeStructTypeID(id));
            module.structTypes.insert_or_assign(id, static_cast<IRStructType*>(type));
        }
    }

    void LoweringSession::populateModule(BackendModule& module) {
        std::map<fzlib::String, const BackendFunction*> symbols;
        std::vector<const BackendFunction*> imports;
        std::vector<const BackendFunction*> runtimeFunctions;
        std::vector<BackendModule*> usingModules;
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
        std::unordered_set<BackendModule*> seenModules;
        for (const auto* source : module.source->getUsingList()) {
            auto* imported = backendModules.at(source).get();
            if (seenModules.insert(imported).second) usingModules.push_back(imported);
        }
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

    BackendFunction* LoweringSession::lowerFunction(const Function& function) {
        const auto* parent = function.getParent();
        if (!parent || std::find(parent->getFunctions().begin(), parent->getFunctions().end(), &function) == parent->getFunctions().end()) {
            throw std::runtime_error("Function is not owned by its source module");
        }
        lowerModule(*parent);
        return functionMap.at(&function);
    }

    void LoweringSession::validateStruct(const StructDeclId& id) const {
        std::set<StructDeclId> visiting;
        std::set<StructDeclId> visited;
        std::function<void(const TypeInfo*)> visitType;
        std::function<void(const StructDeclId&)> visitStruct;

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
        visitStruct = [&](const StructDeclId& declarationId) {
            if (visiting.contains(declarationId)) {
                throw std::runtime_error("Struct layout contains a by-value recursive cycle");
            }
            if (visited.contains(declarationId)) return;
            const auto* declaration = structResolver->resolveStruct(declarationId);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");
            if (!declaration->isComplete()) return;
            visiting.insert(declarationId);
            for (const auto& field : declaration->getFields()) visitType(field.type);
            visiting.erase(declarationId);
            visited.insert(declarationId);
        };
        visitStruct(id);
    }

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
        // 本次转换未遇到未完成的结构体声明时，整张类型图已稳定，可以写入缓存；
        // 否则保留重新转换的机会，以便语义声明完成后在同一类型对象上补齐布局。
        if (!pending.opaqueStruct) {
            typeMap.insert(pending.results.begin(), pending.results.end());
        }
        return result;
    }

    IRType *LoweringSession::lowerTypeSafer(const TypeInfo *tyInfo, PendingLowering& pending) {
        if (!tyInfo) {
            throw std::runtime_error("cannot give a null typeinfo to lowering");
        }
        if (&tyInfo->getContext() != &irContext) {
            throw std::runtime_error("TypeInfo does not belong to this LoweringSession's IRContext");
        }
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
            const auto& structDecl = std::get<StructTypeInfo>(tyInfo->getComplexTypeInfo()).getDeclId();

            if (!structResolver) throw std::runtime_error("Struct lowering requires a StructDeclRegistry");
            const auto* declaration = structResolver->resolveStruct(structDecl);
            if (!declaration) throw std::runtime_error("Unknown semantic struct declaration");

            const auto* owner = structResolver->resolveNamingContext(structDecl);
            if (!owner) throw std::runtime_error("Semantic struct declaration has no NamingContext");
            auto* lowered = owner->getStructTy(irContext, structDecl);
            structMap.try_emplace(structDecl, lowered);
            if (!declaration->isComplete()) {
                pending.opaqueStruct = true;
                return lowered;
            }
            // 正在填充的结构体是递归回边，外层调用完成后其布局即完整
            if (fillingStructs.contains(structDecl)) return lowered;
            validateStruct(structDecl);

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
        pending.results.emplace(tyInfo, result);

        return result;
    }
}
