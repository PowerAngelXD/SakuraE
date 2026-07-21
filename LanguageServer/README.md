# SakuraE Language Server

`sakurae-language-server` is a stdio JSON-RPC language server for SakuraE.
It reuses the compiler lexer and parser for diagnostics and builds a lightweight
single-document symbol index for completion, definition, hover, and document
symbols.

## Build

Install the Arch Linux dependencies:

```bash
sudo pacman -S cmake ninja llvm nlohmann-json
```

Build the server from the repository root:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target sakurae-language-server
```

The executable is `build/sakurae-language-server`.

## Zed development configuration

Install the `sakurae-zed` directory as a development extension. Configure the
server binary in Zed settings so the registered `sakurae-language-server` name
resolves to the local build:

```json
{
  "lsp": {
    "sakurae-language-server": {
      "binary": {
        "path": "/absolute/path/to/SakuraE/build/sakurae-language-server"
      }
    }
  }
}
```

The server currently analyzes one open document at a time. It does not yet
resolve modules or definitions across files.
