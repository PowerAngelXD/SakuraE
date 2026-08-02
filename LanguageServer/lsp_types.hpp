#ifndef SAKURAE_LSP_TYPES_HPP
#define SAKURAE_LSP_TYPES_HPP

#include <string>
#include <vector>

namespace sakurae::lsp {

    struct Position {
        int line = 0;
        int character = 0;
    };

    struct Range {
        Position start;
        Position end;
    };

    enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

    struct Diagnostic {
        Range range;
        std::string message;
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        std::string source = "sakurae";
    };

    enum class SymbolKind {
        Function = 12,
        Variable = 13,
        Field = 8,
        Parameter = 26,
        Type = 22,
    };

    struct TypeInfo {
        std::string name = "unknown";
        bool known = false;
    };

    struct Symbol {
        std::string name;
        SymbolKind kind = SymbolKind::Variable;
        TypeInfo type;
        Range range;
        Range selection_range;
        std::string detail;
        int scope_id = 0;
        int parent_symbol = -1;
    };

    struct Reference {
        std::string name;
        Range range;
        int scope_id = 0;
        int symbol_index = -1;
    };

    struct CompletionItem {
        std::string label;
        int kind = 6;
        std::string detail;
    };

} // namespace sakurae::lsp

#endif
