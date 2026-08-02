# SakuraE 语言服务器

`sakurae-language-server` 是一个通过标准输入和标准输出通信的 JSON-RPC
语言服务器。它使用 SakuraE 编译器现有的词法分析器和语法分析器进行诊断，
并为当前打开的文档建立轻量级符号索引。

## 当前能力

语言服务器目前支持以下 LSP 功能：

- 语法和词法诊断，通过 `textDocument/publishDiagnostics` 发布；
- 代码补全，包括关键字、类型、运行时函数和当前作用域内的符号；
- 跳转到定义；
- 悬停信息；
- 当前文档的符号列表。

分析范围目前限制为单个打开的文档。语言服务器不会解析模块，也不会在多个
文件之间解析符号或定义。

## 工作方式

服务器使用 LSP 的 stdio 传输。启动后从标准输入读取带有
`Content-Length` 头的 JSON-RPC 消息，并将响应和通知写入标准输出。

文档打开或内容变更时，服务器会：

1. 使用 SakuraE lexer 将文本转换为 token；
2. 建立作用域、变量、参数和函数的符号索引；
3. 解析语句并收集语法错误；
4. 发布当前文档的诊断结果。

服务器不会把日志写入标准输出，因为标准输出属于 LSP 通信通道。

## Arch Linux 构建

安装依赖：

```bash
sudo pacman -S cmake ninja llvm nlohmann-json
```

从仓库根目录构建语言服务器：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target sakurae-language-server
```

生成的可执行文件为：

```text
build/sakurae-language-server
```

如果已经存在 `build` 目录，也可以直接执行：

```bash
cmake --build build --target sakurae-language-server
```

## 独立运行

语言服务器不是交互式命令行程序，不能通过直接输入 SakuraE 源码来测试。
它需要由 Zed 或其他 LSP 客户端通过 stdio 启动和通信。

可以使用已有的 LSP 测试目标验证服务器和分析器：

```bash
cmake --build build --target sakurae-language-server-tests
./build/sakurae-language-server-tests
```

## 在 Zed 中使用

### 开发扩展构建

Zed 扩展包含一个最小 Rust WASM 入口。即使扩展主要提供 grammar 和语言服务
器声明，Zed 仍要求扩展包含 `extension.wasm`。

准备 Rust WASM 目标：

```bash
rustup target add wasm32-wasip1
```

构建扩展：

```bash
cd sakurae-zed
cargo build --release --target wasm32-wasip1
```

如果需要手动生成扩展产物：

```bash
cp target/wasm32-wasip1/release/sakurae_zed.wasm extension.wasm
```

然后在 Zed 中执行 `Install Dev Extension`，选择 `sakurae-zed` 的 Windows
本地副本。Windows 版 Zed 不应直接使用 `\\wsl.localhost\...` 路径作为开发
扩展目录。

### 语言服务器路径

Rust 扩展注册的语言服务器名称是：

```text
sakurae-language-server
```

扩展启动语言服务器时按以下顺序查找可执行文件：

1. 在当前工作区的 PATH 中查找 `sakurae-language-server`；
2. 回退到工作区下的 `build/sakurae-language-server`。

因此，对于 WSL 中打开的项目，推荐先在 WSL 项目根目录构建：

```bash
cmake --build build --target sakurae-language-server
```

随后在 Zed 中重新安装开发扩展，并执行 `Restart Language Server`。

如果服务器放在其他位置，可以将它加入当前 Zed 工作区的 PATH，或者调整
扩展中的启动逻辑。服务器本身不由 Rust WASM 扩展自动下载。

### WSL 和 Windows 同步

如果扩展源码位于 WSL，而 Zed 运行在 Windows 侧，需要先将扩展同步到 Windows
目录，再从该目录安装开发扩展：

```bash
./sync-zed-extension.sh
```

脚本会删除目标目录中的旧副本，并复制新的 `extension.toml`、语言配置、
高亮查询和 Rust 扩展源码。脚本中的目标目录应与本机实际的 Windows Zed
开发扩展目录一致。

## 支持的 LSP 请求

初始化阶段声明的能力包括：

| LSP 方法 | 用途 |
| --- | --- |
| `initialize` | 协商服务器能力 |
| `textDocument/didOpen` | 打开文档并发布诊断 |
| `textDocument/didChange` | 更新文档并重新分析 |
| `textDocument/didClose` | 清理文档和诊断 |
| `textDocument/completion` | 返回补全项 |
| `textDocument/definition` | 返回符号定义位置 |
| `textDocument/hover` | 返回符号类型和说明 |
| `textDocument/documentSymbol` | 返回当前文档的符号列表 |
| `shutdown` / `exit` | 关闭服务器 |

补全触发字符目前配置为句点 `.`。符号解析使用当前文档的作用域索引，
运行时函数和内置类型作为内置项提供。

## 故障排查

### LSP 没有启动

确认当前文件使用的是 `SakuraE` 语言，而不是 `Unknown` 或其他语言。然后
重新执行：

```text
Restart Language Server
```

确认语言服务器二进制存在且可执行：

```bash
ls -l build/sakurae-language-server
```

Zed 日志中应出现类似内容：

```text
starting language server process
```

如果没有这条日志，优先检查扩展 manifest 中是否包含：

```toml
[language_servers.sakurae-language-server]
languages = ["SakuraE"]
```

### 出现 `Undefined symbol`

当前分析器只解析单个文档。跨文件定义不会被解析。运行时函数由分析器内置
名称表识别，不需要在当前文档中声明。

### 修改后没有生效

开发扩展不会因为 Git push 自动更新。修改后需要：

1. 重新构建语言服务器或 Rust 扩展；
2. 重新同步 Windows 侧扩展目录；
3. 重新执行 `Install Dev Extension`；
4. 重启 Zed 或重启语言服务器。

查看 Zed 日志时，重点搜索以下关键词：

```text
sakurae
language server
failed to load language
starting language server process
```

### grammar 编译失败

grammar 使用 `extension.toml` 中固定的 Git commit：

```toml
[grammars.sakurae]
repository = "https://github.com/PowerAngelXD/SakuraE"
rev = "<固定 commit>"
path = "sakurae-tree-sitter"
```

Windows 侧需要能够通过 Git fetch 访问 GitHub。如果使用代理，应同时配置
Windows Git，而不仅是 WSL 中的 Git：

```powershell
git config --global http.proxy http://127.0.0.1:7890
git config --global https.proxy http://127.0.0.1:7890
```

## 相关目录

```text
LanguageServer/
├── analyzer.cpp/.hpp       词法分析结果、符号索引和诊断
├── document_store.cpp/.hpp 打开文档的内存存储
├── json_rpc.cpp/.hpp       LSP 消息读写
├── lsp_types.hpp           LSP 数据结构
├── server.cpp/.hpp         请求分发和服务器主循环
├── symbol_table.cpp/.hpp   作用域和符号索引
├── main.cpp                stdio 入口
└── tests.cpp               分析器和协议测试
```
