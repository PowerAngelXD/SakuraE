<div align="center">
<img src="banner.png"/>

<h1> 
   🌸SakuraE🌸
   <br/>
   <img src="https://img.shields.io/badge/Version-dev0.0-red"/>
   <img src="https://img.shields.io/badge/Windows%2011-333333?style=flat&logo=quarto&logoColor=white&labelColor=0078D4"/>
   <img src="https://img.shields.io/badge/Arch%20Linux-333333?style=flat&logo=archlinux&logoColor=white&labelColor=1793D1"/>
</h1>


</div>

### Based on LLVM, A Compilable Programming Language

## Project Structure (Main)

```
SakuraE/
├── CMakeLists.txt              # CMake build configuration file
├── main.cpp                    # Main entry point of the compiler
├── demo.sak                    # Sample source file for testing
├── banner.png                  # Project logo/banner image
├── Compiler/                   # Core compiler components
│   ├── CodeGen/                # Code generation module (LLVM backend)
│   │   ├── generator.cpp       # Implementation of code generation
│   │   └── generator.hpp       # Header for code generation
│   ├── Error/                  # Error handling utilities
│   │   └── error.hpp           # Error definitions and handling
│   ├── Frontend/               # Frontend components (lexer, parser, AST)
│   │   ├── AST.hpp             # Abstract Syntax Tree definitions
│   │   ├── grammar.txt         # Grammar rules for parsing
│   │   ├── lexer.cpp           # Lexical analyzer implementation
│   │   ├── lexer.h             # Lexer header
│   │   ├── parser_base.hpp     # Base parser utilities and parser combinators
│   │   ├── parser.cpp          # Parser implementation
│   │   └── parser.hpp          # Parser header
│   ├── IR/                     # Intermediate Representation (IR) module
│   │   ├── generator.cpp       # IR generator implementation (AST visitor)
│   │   ├── generator.hpp       # IR generation utilities
│   │   ├── IR.hpp              # Core IR definitions
│   │   ├── struct/             # IR structural components
│   │   │   ├── block.hpp       # Basic block representation
│   │   │   ├── function.hpp    # Function representation with scope management
│   │   │   ├── instruction.hpp # Instruction definitions and OpKind enum
│   │   │   ├── module.hpp      # Module representation
│   │   │   ├── program.hpp     # Program-level IR
│   │   │   └── scope.hpp       # Scope management for symbols
│   │   ├── type/               # Type system
│   │   │   ├── type.cpp        # IRType implementation
│   │   │   ├── type.hpp        # IRType definitions (int, float, array, pointer, etc.)
│   │   │   ├── type_info.cpp   # TypeInfo implementation
│   │   │   └── type_info.hpp   # TypeInfo for frontend type representation
│   │   └── value/              # Value and constant systems
│   │       ├── constant.cpp    # Constant value implementation
│   │       ├── constant.hpp    # Constant value definitions
│   │       └── value.hpp       # Value representations
│   └── Utils/                  # Utility functions
│       └── Logger.hpp          # Logging utilities
├── includes/                   # External dependencies
│   ├── magic_enum.hpp          # Enum reflection library
│   └── String.hpp              # Custom string utilities
└── README.md                   # This file
```

### Explanation of Key Components
- **CMakeLists.txt**: Defines the build system using CMake, including dependencies on LLVM and compiler flags.
- **main.cpp**: The entry point that initializes and runs the compiler pipeline.
- **demo.sak**: A sample SakuraE source file used for testing the compiler.
- **banner.png**: Project logo/banner image displayed in the README.
- **Compiler/CodeGen/**: Handles the generation of target machine code from IR, interfacing with LLVM backends.
- **Compiler/Error/**: Provides error reporting and handling mechanisms throughout the compilation process.
- **Compiler/Frontend/**: Manages lexical analysis, parsing, and AST construction from source code:
  - **lexer.cpp/h**: Tokenizes source code into a stream of tokens.
  - **parser_base.hpp**: Provides parser combinator utilities including `TokenParser`, `ClosureParser`, `ConnectionParser`, `OptionsParser`, and `NullParser`.
  - **parser.cpp/hpp**: Implements recursive descent parsers for expressions, statements, and declarations.
  - **AST.hpp**: Defines the Abstract Syntax Tree node structure and tags.
- **Compiler/IR/**: Defines and manipulates the intermediate representation:
  - **generator.cpp/hpp**: Visitor-based IR generation from AST nodes with expression and statement visitors.
  - **IR.hpp**: Core IR type definitions and enumerations.
  - **struct/**: Contains IR structural components:
    - **block.hpp**: Basic blocks containing sequences of instructions.
    - **function.hpp**: Function representation with formal parameters and scope management.
    - **instruction.hpp**: Instruction definitions with OpKind enum (arithmetic, logic, control flow, etc.).
    - **module.hpp**: Module-level organization of functions.
    - **program.hpp**: Top-level program container managing multiple modules.
    - **scope.hpp**: Symbol table for variable and function lookups.
  - **type/**: Type system implementation:
    - **type.hpp**: IRType class hierarchy (IntegerType, FloatType, ArrayType, PointerType, FunctionType).
    - **type_info.hpp**: TypeInfo for representing frontend types before conversion to IRType.
  - **value/**: Value representations:
    - **value.hpp**: Base Value class for all IR values.
    - **constant.hpp**: Constant value definitions including literals and type constants.
- **Compiler/Utils/**: Contains shared utilities like logging for debugging and diagnostics.
- **includes/**: Third-party libraries and custom headers:
  - **magic_enum.hpp**: Compile-time enum reflection library.
  - **String.hpp**: Custom string utilities.

## Build

### Prerequisites
- **C++ Compiler**: GCC 13+ or Clang 16+ supporting C++23 (as specified in CMakeLists.txt).
- **CMake**: Version 3.24 or higher.
- **LLVM**: Version 16+ installed and configured (required for the project, as it uses LLVM libraries).

### Build Steps
1. **Clone the repository**:
   ```bash
   git clone https://github.com/powerangelxd/SakuraE.git
   cd SakuraE
   ```

2. **Create build directory**:
   ```bash
   mkdir build
   cd build
   ```

3. **Configure with CMake**:
   ```bash
   cmake -G Ninja ..
   ```
   This will detect LLVM and set up the build with C++23 standard and necessary flags using Ninja.

4. **Build the project**:
   ```bash
   ninja
   ```

5. **Run the compiler** (optional test):
   ```bash
   ./SakuraE ../demo.sak
   ```

If you encounter issues, ensure LLVM development libraries are installed (e.g., `llvm-dev` package) and `llvm-config` is available in PATH.

## IR Developing
For more detailed development specifications regarding the IR, please click the link below.

[IR README](Compiler/IR/README.md)

## Contributor

- [The Flysong](https://github.com/theflysong) : Contributor inspired by the Parser

## Sponsor

- [SendsTeam](https://github.com/SendsTeam) : Provide LLM Service during developing