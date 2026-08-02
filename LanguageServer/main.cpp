#include "LanguageServer/server.hpp"

#include <iostream>

int main() {
    sakurae::lsp::Server server;
    server.run(std::cin, std::cout);
    return 0;
}
