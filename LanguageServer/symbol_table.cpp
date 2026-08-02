#include "LanguageServer/symbol_table.hpp"

#include <algorithm>
#include <unordered_set>

namespace sakurae::lsp {

    int SymbolIndex::lookup(const std::string &name, int scope_id) const {
        for (int current = scope_id; current >= 0 && current < static_cast<int>(scopes.size());
             current = scopes[current].parent) {
            for (auto it = scopes[current].symbols.rbegin(); it != scopes[current].symbols.rend(); ++it) {
                if (*it >= 0 && *it < static_cast<int>(symbols.size()) && symbols[*it].name == name)
                    return *it;
            }
        }
        return -1;
    }

    std::vector<int> SymbolIndex::visibleSymbols(int scope_id) const {
        std::vector<int> result;
        std::unordered_set<std::string> names;
        for (int current = scope_id; current >= 0 && current < static_cast<int>(scopes.size());
             current = scopes[current].parent) {
            for (auto it = scopes[current].symbols.rbegin(); it != scopes[current].symbols.rend(); ++it) {
                if (*it < 0 || *it >= static_cast<int>(symbols.size()))
                    continue;
                if (names.insert(symbols[*it].name).second)
                    result.push_back(*it);
            }
        }
        return result;
    }

} // namespace sakurae::lsp
