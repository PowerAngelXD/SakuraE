#ifndef SAKURAE_JSON_RPC_HPP
#define SAKURAE_JSON_RPC_HPP

#include <istream>
#include <ostream>

#include <nlohmann/json.hpp>

namespace sakurae::lsp {

    bool readMessage(std::istream &input, nlohmann::json &message);
    void writeMessage(std::ostream &output, const nlohmann::json &message);

} // namespace sakurae::lsp

#endif
