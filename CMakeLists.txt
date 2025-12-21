cmake_minimum_required(VERSION 3.24)
project(SakuraE)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})

add_definitions(${LLVM_DEFINITIONS})

message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

string(REPLACE "." ";" LLVM_PACKAGE_VERSION_LIST "${LLVM_PACKAGE_VERSION}")
list(GET LLVM_PACKAGE_VERSION_LIST 0 LLVM_MAJOR_VERSION)

find_program(LLVM_CONFIG_EXECUTABLE
             NAMES llvm-config llvm-config-${LLVM_MAJOR_VERSION}
             DOC "Path to the llvm-config executable"
             REQUIRED
)
message(STATUS "Using llvm-config executable: ${LLVM_CONFIG_EXECUTABLE}")

execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs all
                OUTPUT_VARIABLE llvm_components
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE LLVM_LIBS_RESULT
)

string(REPLACE " " ";" llvm_components_list "${llvm_components}")

execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
                OUTPUT_VARIABLE llvm_libdir
                OUTPUT_STRIP_TRAILING_WHITESPACE
)

# print LLVM components
message(STATUS "LLVM libdir: ${llvm_libdir}")
message(STATUS "LLVM components: ${llvm_components}")

# -Wfloat-equal
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -pedantic -Wimplicit-fallthrough -Wsequence-point -Wswitch-unreachable -Wstringop-truncation -Wbool-compare -Wtautological-compare -Wshadow=global -Wpointer-arith -Wpointer-compare -Wcast-align -Wcast-qual -Wwrite-strings -Wdangling-else -Wlogical-op -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

file(GLOB SRC_FILES "includes/magic_enum.hpp" 
                    "main.cpp" 
                    "Compiler/Frontend/lexer.*"
                    "Compiler/Frontend/parser_base.hpp"
                    "Compiler/Frontend/parser.hpp"
                    "Compiler/Frontend/parser.cpp"
                    "Compiler/Error/error.hpp"
                    "Compiler/Utils/logger.hpp"
                    "Compiler/Frontend/AST.h"
                    "Compiler/IR/*.hpp")
        
add_executable(${CMAKE_PROJECT_NAME} ${SRC_FILES})

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${llvm_components_list})