#include "LanguageServer/document_store.hpp"

#include <algorithm>

namespace sakurae::lsp {

    void DocumentStore::rebuildLineStarts(Document &document) {
        document.line_starts.clear();
        document.line_starts.push_back(0);
        for (std::size_t i = 0; i < document.text.size(); ++i) {
            if (document.text[i] == '\n')
                document.line_starts.push_back(i + 1);
        }
    }

    void DocumentStore::open(std::string uri, int version, std::string text) {
        Document document{std::move(uri), version, std::move(text), {}};
        rebuildLineStarts(document);
        documents[document.uri] = std::move(document);
    }

    void DocumentStore::change(const std::string &uri, int version, std::string text) {
        open(uri, version, std::move(text));
    }

    void DocumentStore::close(const std::string &uri) {
        documents.erase(uri);
    }

    const Document *DocumentStore::find(const std::string &uri) const {
        auto it = documents.find(uri);
        return it == documents.end() ? nullptr : &it->second;
    }

    Document *DocumentStore::find(const std::string &uri) {
        auto it = documents.find(uri);
        return it == documents.end() ? nullptr : &it->second;
    }

    Position DocumentStore::positionAt(const Document &document, std::size_t offset) const {
        offset = std::min(offset, document.text.size());
        auto it = std::upper_bound(document.line_starts.begin(), document.line_starts.end(), offset);
        const std::size_t line_index =
            it == document.line_starts.begin()
                ? 0
                : static_cast<std::size_t>(std::distance(document.line_starts.begin(), it) - 1);
        return {static_cast<int>(line_index), static_cast<int>(offset - document.line_starts[line_index])};
    }

    std::size_t DocumentStore::offsetAt(const Document &document, Position position) const {
        if (document.line_starts.empty())
            return 0;
        const int line = std::clamp(position.line, 0, static_cast<int>(document.line_starts.size() - 1));
        const std::size_t start = document.line_starts[static_cast<std::size_t>(line)];
        const std::size_t line_end = line + 1 < static_cast<int>(document.line_starts.size())
                                         ? document.line_starts[static_cast<std::size_t>(line + 1)]
                                         : document.text.size();
        const std::size_t character = std::max(position.character, 0);
        return std::min(start + character, line_end);
    }

    Range DocumentStore::tokenRange(const Document &document, int line, int column, std::size_t length) const {
        Position start{std::max(line - 1, 0), std::max(column - 1, 0)};
        const std::size_t start_offset = offsetAt(document, start);
        return {start, positionAt(document, start_offset + length)};
    }

} // namespace sakurae::lsp
