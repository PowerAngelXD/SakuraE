#ifndef SAKURAE_BACKEND_MODULE_HPP
#define SAKURAE_BACKEND_MODULE_HPP

#include "function.hpp"
#include "Compiler/IR/Semantic/type/type_info.hpp"
#include <map>
#include <memory>
#include <vector>

namespace sakuraE::IR {
    class Module;
    class IRContext;
    class IRStructType;

    class BackendModule {
    public:
        const Module* source = nullptr;
        IRContext* context = nullptr;
        fzlib::String ID;
        std::vector<std::unique_ptr<BackendFunction>> functions;
        std::vector<const BackendFunction*> imports;
        std::vector<BackendModule*> usingModules;
        std::map<fzlib::String, const BackendFunction*> symbols;
        std::map<StructDeclId, IRStructType*> structTypes;
        std::vector<const BackendFunction*> runtimeFunctions;

        const BackendFunction* lookup(const fzlib::String& name) const;
        void verify() const;
        fzlib::String toString() const;
    };
}

#endif
