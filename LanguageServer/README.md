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

## Build the Zed extension

The extension includes a minimal Rust WASM entry point because Zed requires
`extension.wasm` even when the extension only provides a grammar and language
server declaration. From the extension directory, install the WASM target
provided by your Rust toolchain and build the release artifact:

```bash
cd sakurae-zed
rustup target add wasm32-wasip1
cargo build --release --target wasm32-wasip1
cp target/wasm32-wasip1/release/sakurae_zed.wasm extension.wasm
```

Then install `sakurae-zed` as a development extension in Zed. For a WSL
remote project, build and install the extension from the WSL filesystem so
the remote extension host receives the Linux-side `extension.wasm` and can
register `sakurae-language-server` there.
