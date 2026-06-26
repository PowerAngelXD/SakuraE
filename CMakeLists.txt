cmake_minimum_required(VERSION 3.24)

project(SakuraE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAKURAE_ENABLE_ASAN "Enable AddressSanitizer for debug-style builds" ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

string(REPLACE "." ";" LLVM_PACKAGE_VERSION_LIST "${LLVM_PACKAGE_VERSION}")
list(GET LLVM_PACKAGE_VERSION_LIST 0 LLVM_MAJOR_VERSION)

find_program(
    LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config-${LLVM_MAJOR_VERSION}
    DOC "Path to the llvm-config executable"
    REQUIRED
)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs --system-libs all
    OUTPUT_VARIABLE LLVM_LINK_FLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

separate_arguments(LLVM_LINK_FLAGS NATIVE_COMMAND "${LLVM_LINK_FLAGS_RAW}")

message(STATUS "Using llvm-config executable: ${LLVM_CONFIG_EXECUTABLE}")
message(STATUS "LLVM link flags: ${LLVM_LINK_FLAGS_RAW}")

add_executable(
    ${PROJECT_NAME}
    main.cpp
    Compiler/Frontend/lexer.cpp
    Compiler/Frontend/parser.cpp
    Compiler/IR/generator.cpp
    Compiler/IR/type/type.cpp
    Compiler/IR/type/type_info.cpp
    Compiler/IR/value/constant.cpp
    Compiler/LLVMCodegen/LLVMCodegenerator.cpp
    Runtime/alloc.cpp
    Runtime/gc.cpp
    Runtime/print.cpp
    Runtime/raw_string.cpp
)

target_include_directories(
    ${PROJECT_NAME}
    SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        ${LLVM_DEFINITIONS}
)

target_compile_options(
    ${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wpedantic
        -Wimplicit-fallthrough
        -Wsequence-point
        -Wstringop-truncation
        -Wbool-compare
        -Wtautological-compare
        -Wshadow=global
        -Wpointer-arith
        -Wpointer-compare
        -Wcast-align
        -Wcast-qual
        -Wwrite-strings
        -Wdangling-else
        -Wlogical-op
        -Werror
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:Release>:-O3>
)

if(SAKURAE_ENABLE_ASAN)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE
            $<$<CONFIG:Debug,RelWithDebInfo>:-fsanitize=address>
    )

    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
            $<$<CONFIG:Debug,RelWithDebInfo>:-fsanitize=address>
    )
endif()

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        ${LLVM_LINK_FLAGS}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
