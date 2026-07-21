#ifndef SAKURAE_DOCUMENT_STORE_HPP
#define SAKURAE_DOCUMENT_STORE_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "LanguageServer/lsp_types.hpp"

namespace sakurae::lsp {

    struct Document {
        std::string uri;
        int version = 0;
        std::string text;
        std::vector<std::size_t> line_starts;
    };

    class DocumentStore {
      public:
        void open(std::string uri, int version, std::string text);
        void change(const std::string &uri, int version, std::string text);
        void close(const std::string &uri);

        const Document *find(const std::string &uri) const;
        Document *find(const std::string &uri);

        Position positionAt(const Document &document, std::size_t offset) const;
        std::size_t offsetAt(const Document &document, Position position) const;
        Range tokenRange(const Document &document, int line, int column, std::size_t length) const;

      private:
        static void rebuildLineStarts(Document &document);
        std::unordered_map<std::string, Document> documents;
    };

} // namespace sakurae::lsp

#endif
