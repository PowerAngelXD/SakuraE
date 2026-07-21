#include "LanguageServer/json_rpc.hpp"

#include <cctype>
#include <string>

namespace sakurae::lsp {

    bool readMessage(std::istream &input, nlohmann::json &message) {
        std::string line;
        std::size_t contentLength = 0;
        bool foundLength = false;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            const auto separator = line.find(':');
            if (separator == std::string::npos)
                continue;
            std::string name = line.substr(0, separator);
            std::string value = line.substr(separator + 1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(value.begin());
            if (name == "Content-Length") {
                try {
                    contentLength = std::stoull(value);
                    foundLength = true;
                } catch (...) {
                    return false;
                }
            }
        }
        if (!input || !foundLength)
            return false;

        std::string body(contentLength, '\0');
        input.read(body.data(), static_cast<std::streamsize>(contentLength));
        if (input.gcount() != static_cast<std::streamsize>(contentLength))
            return false;
        try {
            message = nlohmann::json::parse(body);
        } catch (...) {
            return false;
        }
        return true;
    }

    void writeMessage(std::ostream &output, const nlohmann::json &message) {
        const std::string body = message.dump();
        output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        output.flush();
    }

} // namespace sakurae::lsp
