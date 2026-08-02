#ifndef SAKURAE_ANALYZER_HPP
#define SAKURAE_ANALYZER_HPP

#include <string>
#include <vector>

#include "LanguageServer/document_store.hpp"
#include "LanguageServer/symbol_table.hpp"

namespace sakurae::lsp {

    const std::vector<std::string> &runtimeFunctionNames();

    struct AnalysisResult {
        std::vector<Diagnostic> diagnostics;
        SymbolIndex index;
    };

    class Analyzer {
      public:
        AnalysisResult analyze(const Document &document) const;
        int scopeAt(const AnalysisResult &result, Position position) const;
        const Symbol *symbolAt(const AnalysisResult &result, Position position) const;
        const Reference *referenceAt(const AnalysisResult &result, Position position) const;

      private:
        static bool contains(const Range &range, Position position);
    };

} // namespace sakurae::lsp

#endif
