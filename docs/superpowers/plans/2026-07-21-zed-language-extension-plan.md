# SakuraE Zed Language Extension Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完善 SakuraE 的 Zed 语言扩展，并提供基于现有 C++ 前端的基础完整 LSP：诊断、补全、跳转定义、悬停、文档符号和基础类型推断。

**Architecture:** Zed 通过 stdio 启动独立的 `sakurae-language-server` C++ 可执行程序。语言服务器维护打开文档，调用现有 Lexer/Parser 构建 AST 索引和作用域符号表，再由 LSP handler 将结果转换成 JSON-RPC 响应。Tree-sitter 查询继续只负责编辑器侧的高亮、折叠和符号展示。

**Tech Stack:** C++23、现有 SakuraE Lexer/Parser/AST、`nlohmann/json`、LSP JSON-RPC over stdio、Zed `extension.toml`。

---

## 文件边界

- Create: `LanguageServer/json_rpc.hpp`：Content-Length framing 和 JSON-RPC 消息读写。
- Create: `LanguageServer/lsp_types.hpp`：LSP 请求、响应和基础数据结构的最小 C++ 表示。
- Create: `LanguageServer/document_store.hpp/.cpp`：URI、文本、版本和行偏移管理。
- Create: `LanguageServer/symbol_table.hpp/.cpp`：作用域、声明、引用、函数签名和类型信息。
- Create: `LanguageServer/analyzer.hpp/.cpp`：Lexer/Parser 调用、诊断、符号索引、类型推断。
- Create: `LanguageServer/server.hpp/.cpp`：initialize、shutdown、文档同步和各功能 handler。
- Create: `LanguageServer/main.cpp`：语言服务器入口，使用 stdio 主循环。
- Create: `LanguageServer/tests.cpp`：无外部测试框架的 analyzer/server 单元测试入口。
- Modify: `Compiler/Error/error.hpp`：公开错误类别、消息和位置访问器。
- Modify: `Compiler/Frontend/AST.hpp`：公开节点子项和安全的 token 访问能力，保留现有接口。
- Modify: `CMakeLists.txt`：增加 `sakurae-language-server` target、JSON 依赖和测试 target。
- Modify: `sakurae-zed/extension.toml`：声明 language server、语言配置和自动缩进规则。
- Modify: `sakurae-zed/grammars/sakurae.toml`：保持 grammar 名称与仓库中的 Tree-sitter grammar 一致。
- Modify: `sakurae-zed/queries/sakurae/highlights.scm`：按实际 AST 节点修正关键字、类型、函数、变量、调用和标点高亮。
- Modify: `sakurae-zed/queries/sakurae/folds.scm`：覆盖 block、函数、条件、循环等可折叠节点。
- Modify: `sakurae-zed/queries/sakurae/symbols.scm`：覆盖函数、变量和嵌套定义的符号导航。
- Create: `sakurae-zed/languages/sakurae/config.toml`：若当前 Zed manifest schema 需要，将语言级自动缩进配置单独放置。

### Task 1: 建立可复用的前端位置信息接口

**Files:**
- Modify: `Compiler/Error/error.hpp`
- Modify: `Compiler/Frontend/AST.hpp`
- Test: `LanguageServer/tests.cpp`

- [ ] **Step 1: 写失败测试**

测试应验证 `SakuraError` 能返回 term、message、line、column，并验证 AST 节点能枚举带 tag 的子节点和读取 token 内容。

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build --target sakurae-language-server-tests`
Expected: FAIL，因为接口和测试 target 尚不存在。

- [ ] **Step 3: 实现最小接口**

为 `SakuraError` 增加 `term()`、`message()`、`position()` 访问器；为 `Node` 增加只读 `children()`，返回 `vector<pair<ASTTag, NodePtr>>`，并将现有 `getToken()` 改为在非 token 节点上返回可诊断的安全结果或新增 `tryGetToken()`。不改变现有 `toString()` 和 `toFormatString()` 行为。

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build --target sakurae-language-server-tests && ./build/sakurae-language-server-tests`
Expected: PASS。

- [ ] **Step 5: 提交本任务**

```bash
git add Compiler/Error/error.hpp Compiler/Frontend/AST.hpp LanguageServer/tests.cpp
git commit -m "refactor: expose frontend source information"
```

### Task 2: 完成 Tree-sitter 查询和 Zed 语言配置

**Files:**
- Modify: `sakurae-zed/queries/sakurae/highlights.scm`
- Modify: `sakurae-zed/queries/sakurae/folds.scm`
- Modify: `sakurae-zed/queries/sakurae/symbols.scm`
- Modify: `sakurae-zed/extension.toml`
- Modify: `sakurae-zed/grammars/sakurae.toml`
- Create or modify: `sakurae-zed/languages/sakurae/config.toml`

- [ ] **Step 1: 根据 `node-types.json` 建立查询测试样例**

使用 `sakurae-tree-sitter/test.sak` 和 `test/` 中的现有语法样例，覆盖函数、声明、if/else、while、for、return、调用、数组、类型和注释。

- [ ] **Step 2: 运行 Tree-sitter 查询验证**

Run: `cd sakurae-tree-sitter && tree-sitter test`
Expected: grammar tests pass; any query node not present in `node-types.json` is removed or corrected。

- [ ] **Step 3: 修正高亮查询**

使用明确的 keyword token、builtin type token 和 `identifier_expr` 子节点，避免把函数定义名和普通变量重复标记；增加 `void`、`const`、`range`、`ref`、`struct`、`impl`、`repeat`、`match`、`default` 等当前 lexer 已支持的词法项，只有 grammar 已支持的词才加入查询。

- [ ] **Step 4: 扩展折叠和符号查询**

折叠覆盖 `block_stmt` 以及带 body 的 `if_stmt`、`while_stmt`、`for_stmt`、`func_define_stmt`；符号覆盖函数定义和声明，使用字段级节点范围，不创建无法定位的伪符号。

- [ ] **Step 5: 配置自动缩进**

为 `{`、`}`、函数和控制流 body 配置增加/减少一层缩进；保持项目既有两空格格式约定，不引入格式化器。若 Zed 当前 schema 不支持该项，记录实际支持范围并使用 Zed 的标准语言配置字段。

- [ ] **Step 6: 提交本任务**

```bash
git add sakurae-zed
git commit -m "feat(zed): complete SakuraE language queries"
```

### Task 3: 实现文档存储和源位置转换

**Files:**
- Create: `LanguageServer/document_store.hpp`
- Create: `LanguageServer/document_store.cpp`
- Test: `LanguageServer/tests.cpp`

- [ ] **Step 1: 写失败测试**

测试 URI 到文档记录、版本更新、UTF-8 文本的行起始偏移、LSP zero-based position 到 lexer one-based position 的转换，以及越界位置的钳制。

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build --target sakurae-language-server-tests`
Expected: FAIL。

- [ ] **Step 3: 实现 `DocumentStore`**

保存 `uri`、`version`、`text`、每行起始 byte offset；提供 `open`、`change`、`close`、`find`、`positionAt` 和 `rangeForOffsets`。LSP 行列使用 UTF-16 code units 时，至少正确处理 ASCII，并对非 ASCII 文本按 UTF-8 code point 转换，避免把 byte offset 直接当 column。

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build --target sakurae-language-server-tests && ./build/sakurae-language-server-tests`
Expected: PASS。

- [ ] **Step 5: 提交本任务**

```bash
git add LanguageServer/document_store.* LanguageServer/tests.cpp
git commit -m "feat(lsp): add document position tracking"
```

### Task 4: 构建作用域符号表和基础类型推断

**Files:**
- Create: `LanguageServer/symbol_table.hpp`
- Create: `LanguageServer/symbol_table.cpp`
- Create: `LanguageServer/analyzer.hpp`
- Create: `LanguageServer/analyzer.cpp`
- Test: `LanguageServer/tests.cpp`

- [ ] **Step 1: 写失败测试**

测试 SakuraE 源码中全局函数、函数参数、局部 `let`、嵌套 block 变量、重复定义、未定义引用、函数调用和显式/推断类型。

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build --target sakurae-language-server-tests`
Expected: FAIL。

- [ ] **Step 3: 实现符号数据结构**

定义 `SymbolKind { Function, Variable, Parameter }`、`TypeInfo`、`Symbol`、`Scope` 和 `Reference`。每个符号保存名称、kind、声明 range、类型、容器 scope 和可显示签名；scope 保存父 scope、子 scope 和名称到符号的映射。

- [ ] **Step 4: 实现 analyzer**

对文档执行 Lexer 和 Parser；收集 lexer/parser 错误为诊断；遍历 AST 建立 scope 和符号；按最近 enclosing scope 解析引用；对 literal、声明初始化表达式、赋值和简单二元表达式执行基础类型推断。遇到无法推断的表达式使用 `unknown`，不生成猜测性诊断。

- [ ] **Step 5: 增加语义诊断**

报告未定义变量、同一 scope 重复定义和已知类型之间明显不匹配；诊断范围来自 token/AST `PositionInfo`，严重级别使用 Error，无法确定的问题不报告。

- [ ] **Step 6: 运行测试确认通过**

Run: `cmake --build build --target sakurae-language-server-tests && ./build/sakurae-language-server-tests`
Expected: PASS。

- [ ] **Step 7: 提交本任务**

```bash
git add LanguageServer/symbol_table.* LanguageServer/analyzer.* LanguageServer/tests.cpp
git commit -m "feat(lsp): index SakuraE symbols and types"
```

### Task 5: 实现 JSON-RPC 和 LSP server handlers

**Files:**
- Create: `LanguageServer/json_rpc.hpp`
- Create: `LanguageServer/lsp_types.hpp`
- Create: `LanguageServer/server.hpp`
- Create: `LanguageServer/server.cpp`
- Create: `LanguageServer/main.cpp`
- Test: `LanguageServer/tests.cpp`

- [ ] **Step 1: 写 framing 测试**

测试单条和连续 `Content-Length` 消息、空白 header、JSON body 解析、无效 header 和 EOF。

- [ ] **Step 2: 实现 stdio framing**

从 stdin 读取 header 到空行，解析十进制 `Content-Length`，精确读取 body 字节数；响应使用同样的 `Content-Length` header。禁止把日志写入 stdout，调试日志写 stderr。

- [ ] **Step 3: 实现生命周期和文档同步**

支持 `initialize`、`initialized`、`shutdown`、`exit`、`textDocument/didOpen`、`textDocument/didChange`、`textDocument/didClose`。初始化能力声明同步、补全、定义、悬停和文档符号；每次文档改变后重新分析并发送 `textDocument/publishDiagnostics`。

- [ ] **Step 4: 实现诊断 handler**

将 analyzer 诊断转换为 LSP `Diagnostic`，行列转为 zero-based，范围结束位置至少覆盖错误 token；severity 使用 Error，source 使用 `sakurae`。

- [ ] **Step 5: 实现 completion handler**

返回关键字、内置类型、当前文档可见变量、参数和函数；去重并按 label 排序；函数补全使用 `CompletionItemKind::Function`，变量使用 `Variable`，类型使用 `TypeParameter` 或 `Class`，关键字使用 `Keyword`。

- [ ] **Step 6: 实现 definition、hover 和 document symbols**

通过 position 找到引用或声明，definition 返回目标 range；hover 返回 Markdown 代码块，内容包含名称、类型/签名和声明位置；document symbols 返回函数和变量的嵌套层级。

- [ ] **Step 7: 增加协议级测试**

向 server 注入 initialize、didOpen、completion、definition、hover、shutdown 消息，验证响应 JSON 的 method/id、能力字段、诊断和结果范围。

- [ ] **Step 8: 提交本任务**

```bash
git add LanguageServer
git commit -m "feat(lsp): add SakuraE language server"
```

### Task 6: 接入 CMake、Zed 启动配置并进行端到端验证

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `sakurae-zed/extension.toml`
- Create or modify: `sakurae-zed/languages/sakurae/config.toml`
- Create: `LanguageServer/README.md`

- [ ] **Step 1: 增加 CMake target**

将 analyzer、document store、symbol table、JSON-RPC、server 和 LSP main 编译为 `sakurae-language-server`，与现有 `SakuraE` target 分离，避免改变 CLI 链接入口。

- [ ] **Step 2: 固定 JSON 依赖**

优先使用系统 `nlohmann_json` package；若环境没有 package，使用项目内明确目录的 single-header vendor，并在 README 中写明版本和来源。不得在配置阶段隐式联网下载依赖。

- [ ] **Step 3: 配置 Zed language server**

在 `extension.toml` 中声明 server binary 名称、stdio transport、语言 server id，并配置 `.sak` 文件关联。若 Zed 扩展要求通过 Rust `extension` binary 提供 server，新增最小 Rust extension 只负责下载/启动 C++ server，不把语义逻辑迁移到 Rust。

- [ ] **Step 4: 验证构建和测试**

Run: `cmake -S . -B build -DSAKURAE_ENABLE_ASAN=ON`
Run: `cmake --build build --target SakuraE sakurae-language-server sakurae-language-server-tests -j2`
Run: `./build/sakurae-language-server-tests`
Expected: all targets build successfully and tests pass。

- [ ] **Step 5: 验证协议和扩展**

使用临时 stdio 输入发送 initialize、didOpen、completion、definition、hover、shutdown，确认每条响应具有合法 `Content-Length`；在 Zed 开发扩展模式打开 `test.sak`，确认高亮、折叠、符号、缩进、诊断、补全、跳转和悬停均可用。

- [ ] **Step 6: 写文档并提交**

在 `LanguageServer/README.md` 说明构建命令、Zed 开发安装方式、支持的 LSP 能力和当前不支持跨文件模块的限制。

```bash
git add CMakeLists.txt sakurae-zed LanguageServer/README.md
git commit -m "feat(zed): wire SakuraE language server"
```

## 计划自检

- 语法高亮、折叠、符号导航和自动缩进由 Task 2 覆盖。
- 诊断由 Task 4 和 Task 5 覆盖。
- 补全、跳转定义、悬停和文档符号由 Task 4 和 Task 5 覆盖。
- C++ 独立语言服务器和现有 CLI 分离由 Task 5 和 Task 6 覆盖。
- 现有未提交改动不纳入任何任务的提交范围。
- 不在 CMake 配置时隐式联网下载依赖。
