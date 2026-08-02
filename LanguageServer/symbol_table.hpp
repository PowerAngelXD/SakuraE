#ifndef SAKURAE_SYMBOL_TABLE_HPP
#define SAKURAE_SYMBOL_TABLE_HPP

#include <string>
#include <vector>

#include "LanguageServer/lsp_types.hpp"

namespace sakurae::lsp {

    struct Scope {
        int id = 0;
        int parent = -1;
        Range range;
        std::vector<int> symbols;
    };

    struct SymbolIndex {
        std::vector<Scope> scopes;
        std::vector<Symbol> symbols;
        std::vector<Reference> references;

        int lookup(const std::string &name, int scope_id) const;
        std::vector<int> visibleSymbols(int scope_id) const;
    };

} // namespace sakurae::lsp

#endif
