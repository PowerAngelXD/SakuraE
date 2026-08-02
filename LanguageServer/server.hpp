#ifndef SAKURAE_LANGUAGE_SERVER_HPP
#define SAKURAE_LANGUAGE_SERVER_HPP

#include <istream>
#include <ostream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "LanguageServer/analyzer.hpp"
#include "LanguageServer/document_store.hpp"

namespace sakurae::lsp {

    class Server {
      public:
        void run(std::istream &input, std::ostream &output);

      private:
        nlohmann::json handle(const nlohmann::json &request, std::ostream &output);
        void analyzeAndPublish(const std::string &uri, std::ostream &output);

        static Position readPosition(const nlohmann::json &value);
        static Range readRange(const nlohmann::json &value);
        static nlohmann::json toJson(Position position);
        static nlohmann::json toJson(Range range);
        static nlohmann::json toJson(const Diagnostic &diagnostic);

        DocumentStore documents;
        Analyzer analyzer;
        std::unordered_map<std::string, AnalysisResult> analyses;
        bool shutdownRequested = false;
    };

} // namespace sakurae::lsp

#endif
