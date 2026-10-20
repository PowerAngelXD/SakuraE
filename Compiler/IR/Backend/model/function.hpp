#ifndef SAKURAE_BACKEND_FUNCTION_HPP
#define SAKURAE_BACKEND_FUNCTION_HPP

#include "includes/String.hpp"
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sakuraE::IR {
    class Function;
    class IRValue;
    class Instruction;
    class IRType;
    class IRFunctionType;
    class BackendModule;

    enum class Linkage { Definition, External };
    enum class ABIValueKind { Void, Raw, Boxed };

    struct ABIInfo {
        ABIValueKind result = ABIValueKind::Void;
        std::vector<ABIValueKind> parameters;
    };

    struct GCInfo {
        std::vector<bool> parameterRoots;
        std::unordered_set<const IRValue*> rootedValues;
        std::unordered_set<const IRValue*> rootedSlots;
    };

    class BackendFunction {
    public:
        const Function* source = nullptr;
        BackendModule* parent = nullptr;
        fzlib::String sourceName;
        fzlib::String linkageName;
        Linkage linkage = Linkage::Definition;
        IRFunctionType* signature = nullptr;
        std::vector<std::pair<fzlib::String, IRType*>> parameters;
        IRType* returnType = nullptr;
        ABIInfo abi;
        GCInfo gcInfo;
        std::unordered_map<const IRValue*, IRType*> valueTypes;
        std::unordered_map<const IRValue*, IRType*> typeOperands;

        IRType* storageType(const IRValue* value) const;
        bool tracksRoot(const IRValue* value) const;
        bool rootsSlot(const IRValue* value) const;
    };
}

#endif
