#include "rttype.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace sakuraE::runtime {
    TypeInfo::TypeInfo(
        RuntimeTypeKind type_kind,
        const char* type_name,
        const TypeInfo* element,
        std::uint64_t count,
        const void* type_layout)
        : kind(type_kind),
          name(type_name),
          element_type(element),
          element_count(count),
          layout(type_layout) {}

    namespace {
        constexpr std::size_t BASIC_TYPE_COUNT = 16;

        std::array<const TypeInfo*, BASIC_TYPE_COUNT> basic_types{};
        std::vector<std::unique_ptr<TypeInfo>> type_storage;
        std::list<std::string> name_storage;
        std::map<std::pair<const TypeInfo*, std::uint64_t>, const TypeInfo*> array_types;
        std::map<const TypeInfo*, const TypeInfo*> pointer_types;
        std::map<const TypeInfo*, const TypeInfo*> reference_types;
        std::map<std::pair<std::string, const void*>, const TypeInfo*> struct_types;

        const char* stable_name(std::string name) {
            name_storage.push_back(std::move(name));
            return name_storage.back().c_str();
        }

        const char* basic_name(RuntimeTypeKind kind) {
            switch (kind) {
                case RuntimeTypeKind::Void: return "void";
                case RuntimeTypeKind::I8: return "i8";
                case RuntimeTypeKind::I32: return "i32";
                case RuntimeTypeKind::I64: return "i64";
                case RuntimeTypeKind::U8: return "u8";
                case RuntimeTypeKind::U32: return "u32";
                case RuntimeTypeKind::U64: return "u64";
                case RuntimeTypeKind::F32: return "f32";
                case RuntimeTypeKind::F64: return "f64";
                case RuntimeTypeKind::Bool: return "bool";
                case RuntimeTypeKind::String: return "string";
                default: return "unknown";
            }
        }

        const TypeInfo* store(
            RuntimeTypeKind kind,
            const char* name,
            const TypeInfo* element = nullptr,
            std::uint64_t count = 0,
            const void* layout = nullptr) {
            type_storage.push_back(std::make_unique<TypeInfo>(kind, name, element, count, layout));
            return type_storage.back().get();
        }

        bool valid_basic_kind(std::uint8_t value) {
            return value <= static_cast<std::uint8_t>(RuntimeTypeKind::String);
        }
    }

    extern "C" const TypeInfo* __runtime_type_info_basic(std::uint8_t kind) {
        if (!valid_basic_kind(kind)) {
            std::fprintf(stderr, "[Runtime Error] Invalid basic runtime type kind: %u\n", kind);
            std::exit(1);
        }

        auto index = static_cast<std::size_t>(kind);
        if (!basic_types[index]) {
            auto type_kind = static_cast<RuntimeTypeKind>(kind);
            basic_types[index] = store(type_kind, basic_name(type_kind));
        }
        return basic_types[index];
    }

    extern "C" const TypeInfo* __runtime_type_info_pointer(const TypeInfo* element) {
        if (!element) return nullptr;

        auto it = pointer_types.find(element);
        if (it != pointer_types.end()) return it->second;

        auto* result = store(
            RuntimeTypeKind::Pointer,
            stable_name(std::string("ptr<") + element->name + ">"),
            element);
        pointer_types.emplace(element, result);
        return result;
    }

    extern "C" const TypeInfo* __runtime_type_info_reference(const TypeInfo* element) {
        if (!element) return nullptr;

        auto it = reference_types.find(element);
        if (it != reference_types.end()) return it->second;

        auto* result = store(
            RuntimeTypeKind::Reference,
            stable_name(std::string("ref<") + element->name + ">"),
            element);
        reference_types.emplace(element, result);
        return result;
    }

    extern "C" const TypeInfo* __runtime_type_info_array(
        const TypeInfo* element,
        std::uint64_t count) {
        if (!element) return nullptr;

        auto key = std::make_pair(element, count);
        auto it = array_types.find(key);
        if (it != array_types.end()) return it->second;

        auto* result = store(
            RuntimeTypeKind::Array,
            stable_name(std::string("[") + std::to_string(count) + "]" + element->name),
            element,
            count);
        array_types.emplace(key, result);
        return result;
    }

    extern "C" const TypeInfo* __runtime_type_info_struct(
        const char* name,
        const void* layout) {
        std::string type_name = name ? name : "struct";
        auto key = std::make_pair(type_name, layout);
        auto it = struct_types.find(key);
        if (it != struct_types.end()) return it->second;

        auto* result = store(RuntimeTypeKind::Struct, stable_name(type_name), nullptr, 0, layout);
        struct_types.emplace(std::move(key), result);
        return result;
    }
}
