#ifndef SAKURAE_ATRI_CPP
#define SAKURAE_ATRI_CPP
#include "Compiler/IR/type/type_info.hpp"
#define DEBUG

#include <fstream>
#include <iterator>
#include <filesystem>
#include <vector>

#include "config/config.hpp"
#include "includes/String.hpp"
#include "commands.hpp"

namespace atri {
    class ATRI {
    public:
        ATRI()=default;
        static void parseCommand(fzlib::String command, std::vector<fzlib::String> args) {
            if (command == "help") cmds::cmdHelp(args);
            else if (command == "run") cmds::cmdRun(args);
            else if (command == "exit") cmds::cmdExit(args);
        }

        static void runCli() {
            std::cout << "Using gnuc" << __GNUC__ << "." <<  __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << std::endl;
            std::cout << "Build time: " << __DATE__ << ", " << __TIME__ << std::endl;
            std::cout << "Welcome to atrI, the CLI application for SakuraE  (OvO)=b" << std::endl;
            std::cout << "Type 'help' for help." << std::endl;

            while(true) {
                try {
#ifndef DEBUG
                    fzlib::String line;
                    std::cout << ">> ";
                    getline(std::cin, line);
                    auto list = line.split(' ');
                    std::vector<fzlib::String> args;
                    for (std::size_t i = 1; i < list.size(); i ++) {
                        args.push_back(list[i]);
                    }

                    parseCommand(list[0], args);
#else               
                    std::cout << "RUNNING DEBUG MODE, TEST PROGRAM: " << std::endl;
                    parseCommand("run", {"test2.sak", "-ast", "-sakir", "-llvmir"});
                    parseCommand("exit", {});
#endif
                } 
                catch (const std::runtime_error& e) {
                    std::cerr << e.what() << "\n";
#ifdef DEBUG
                    sakuraE::IR::TypeInfo::clearAll();
                    exit(1);
#endif
                } 
                catch (sakuraE::SakuraError& e) {
                    std::cerr << e.toString() << "\n";
#ifdef DEBUG
                    sakuraE::IR::TypeInfo::clearAll();
                    exit(1);
#endif
                } 
                catch (const std::exception& e) {
                    std::cerr << "OtherError: " << e.what() << "\n";
#ifdef DEBUG
                    sakuraE::IR::TypeInfo::clearAll();
                    exit(1);
#endif
                }
            }
        }
    };
}

#endif // !SAKURAE_ATRI_CPP