#ifndef SAKURAE_BACKEND_FUNCTION_HPP
#define SAKURAE_BACKEND_FUNCTION_HPP

#include "includes/String.hpp"
#include <cstddef>
#include <cstdint>
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
    class IRStructType;
    class BackendModule;

    enum class Linkage { Definition, External };
    enum class ABIValueKind { Void, Raw, Boxed };

    enum class RuntimeDescriptionKind {
        Void, I8, I32, I64, U32, U64, F32, F64, Bool, String,
        Pointer, Reference, Array, Struct, Function, Descriptor
    };
    enum class RuntimeValueTag : std::uint8_t {
        I8 = 0, I32 = 1, I64 = 2, U32 = 4, U64 = 5, F32 = 6,
        F64 = 7, Bool = 8, String = 9, Array = 10, Struct = 11,
        Pointer = 12, Descriptor = 13, None = 255
    };
    enum class ValueRepresentation { Boxed, ManagedArray, Reference, Handle, NonValue };
    enum class ConstantKind { Null, I8, I32, I64, U32, U64, F32, F64, Bool, String, Array, Descriptor };

    // Session-owned metadata; all edges and BackendFunction references are non-owning.
    struct RuntimeTypeDescription {
        RuntimeDescriptionKind kind = RuntimeDescriptionKind::Void;
        RuntimeValueTag tag = RuntimeValueTag::None;
        ValueRepresentation representation = ValueRepresentation::NonValue;
        IRType* storage = nullptr;
        const RuntimeTypeDescription* element = nullptr;
        std::uint64_t count = 0;
        fzlib::String identity;

        bool managed() const {
            return kind == RuntimeDescriptionKind::String || kind == RuntimeDescriptionKind::Array;
        }
    };

    // Fields live in the raw struct layout, never in RuntimeValue wrapper slots.
    enum class FieldStorageKind { Scalar, Pointer, Reference, EmbeddedStruct, EmbeddedArray };

    struct BackendFieldAccess {
        IRStructType* layout = nullptr;
        std::size_t index = 0;
        const RuntimeTypeDescription* type = nullptr;
        FieldStorageKind storage = FieldStorageKind::Scalar;
    };

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
        std::unordered_map<const IRValue*, const RuntimeTypeDescription*> runtimeTypes;
        std::unordered_map<const IRValue*, const RuntimeTypeDescription*> typeOperands;
        std::unordered_map<const IRValue*, ConstantKind> constants;
        std::unordered_map<const Instruction*, BackendFieldAccess> fieldAccesses;
        std::vector<const RuntimeTypeDescription*> parameterRuntimeTypes;
        const RuntimeTypeDescription* returnRuntimeType = nullptr;

        IRType* storageType(const IRValue* value) const;
        const RuntimeTypeDescription& runtimeType(const IRValue* value) const;
        const BackendFieldAccess& fieldAccess(const Instruction* instruction) const;
        bool tracksRoot(const IRValue* value) const;
        bool rootsSlot(const IRValue* value) const;
    };
}

#endif
