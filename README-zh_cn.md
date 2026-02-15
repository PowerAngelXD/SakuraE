<div align="center">
<img src="banner.png"/>

<h1>
   🌸SakuraE🌸
   <br/>
   <img src="https://img.shields.io/badge/Version-dev0.0-red"/>
   <img src="https://img.shields.io/badge/Windows%2011-333333?style=flat&logo=quarto&logoColor=white&labelColor=0078D4"/>
   <img src="https://img.shields.io/badge/Arch%20Linux-333333?style=flat&logo=archlinux&logoColor=white&labelColor=1793D1"/>
</h1>

[English Version](README.md)

</div>

### 基于 LLVM 的可编译编程语言

## [TODO 列表](todo-zh_cn.md)

## 项目结构 (主要)
> [!WARNING]
> 该部分由AI生成

```
SakuraE/
├── CMakeLists.txt                  # CMake 构建配置文件
├── main.cpp                        # 编译器主入口
├── atrI/                           # 交互式 CLI 与配置管理
│   ├── atrI.hpp                    # atrI 主头文件
│   ├── commands.hpp                # CLI 命令定义
│   ├── README.md                   # atrI 文档
│   ├── utils.hpp                   # CLI 工具函数
│   └── config/                     # 配置管理
│       └── config.hpp              # 配置定义
├── Compiler/                       # 核心编译器组件
│   ├── Error/                      # 错误处理工具
│   │   └── error.hpp               # 错误定义与处理
│   ├── Frontend/                   # 前端组件 (词法分析, 语法分析, AST)
│   │   ├── AST.hpp                 # 抽象语法树定义
│   │   ├── grammar.txt             # 语法解析规则
│   │   ├── lexer.cpp               # 词法分析器实现
│   │   ├── lexer.h                 # 词法分析器头文件
│   │   ├── parser_base.hpp         # 基础解析工具与解析组合子
│   │   ├── parser.cpp              # 语法解析器实现
│   │   └── parser.hpp              # 语法解析器头文件
│   ├── IR/                         # 中间表示 (IR) 模块
│   │   ├── docs/                   # IR 文档与规范
│   │   ├── generator.cpp           # IR 生成器实现 (AST 访问者)
│   │   ├── generator.hpp           # IR 生成工具
│   │   ├── IR.hpp                  # 核心 IR 定义
│   │   ├── README-zh_cn.md         # IR 文档 (中文)
│   │   ├── README.md               # IR 文档 (英文)
│   │   ├── struct/                 # IR 结构组件
│   │   │   ├── block.hpp           # 基本块表示
│   │   │   ├── function.hpp        # 带有作用域管理的函数表示
│   │   │   ├── instruction.hpp     # 指令定义与 OpKind 枚举
│   │   │   ├── module.hpp          # 模块表示
│   │   │   ├── program.hpp         # 程序级 IR
│   │   │   └── scope.hpp           # 符号作用域管理
│   │   ├── type/                   # 类型系统
│   │   │   ├── type.cpp            # IRType 实现
│   │   │   ├── type.hpp            # IRType 定义 (int, float, array, pointer 等)
│   │   │   ├── type_info.cpp       # TypeInfo 实现
│   │   │   └── type_info.hpp       # 用于前端类型表示的 TypeInfo
│   │   └── value/                  # 数值与常量系统
│   │       ├── constant.cpp        # 常量值实现
│   │       ├── constant.hpp        # 常量值定义
│   │       └── value.hpp           # 数值表示
│   ├── LLVMCodegen/                # LLVM 后端代码生成模块
│   │   ├── LLVMCodegenerator.cpp   # sakIR 到 LLVM IR 转换的实现
│   │   └── LLVMCodegenerator.hpp   # LLVM 代码生成定义与状态管理
│   └── Utils/                      # 工具函数
│       └── Logger.hpp              # 日志工具
├── Runtime/                        # 运行时库
│   ├── alloc.cpp                   # 内存分配器实现
│   ├── alloc.h                     # 分配器头文件
│   ├── print.cpp                   # 基础 I/O 实现
│   ├── raw_string.cpp              # 字符串处理实现
│   ├── README-zh_cn.md             # 运行时文档 (中文)
│   └── README.md                   # 运行时文档 (英文)
├── includes/                       # 外部依赖
│   ├── magic_enum.hpp              # 枚举反射库
│   └── String.hpp                  # 自定义字符串工具
├── sakurae/                        # VSCode 插件
│   ├── language-configuration.json # 语言配置
│   ├── package.json                # 插件清单
│   ├── README.md                   # 插件文档
│   └── syntaxes/                   # 语法高亮
│       └── sak.tmLanguage.json     # SakuraE 的 TextMate 语法
└── README.md                       # 本文件
```

## 构建

### 前置条件
- **C++ 编译器**: 支持 C++23 的 GCC 13+ 或 Clang 16+ (如 CMakeLists.txt 中指定)
- **CMake**: 3.24 或更高版本
- **LLVM**: 已安装并配置 16+ 版本 (项目需要使用 LLVM 库)
- **Ninja**: 至少为 1.13.2 版本

### 构建步骤
1. **克隆仓库**:
   ```bash
   git clone https://github.com/powerangelxd/SakuraE.git
   cd SakuraE
   ```

2. **创建构建目录**:
   ```bash
   mkdir build
   cd build
   ```

3. **使用 CMake 配置**:
   ```bash
   cmake -G Ninja ..
   ```
   这将检测 LLVM 并使用 Ninja 设置支持 C++23 标准的构建

4. **编译项目**:
    ```bash
    ninja
    ```

5. **在 build 文件夹中创建一个 `.sak` 文件**
   ```bash
   touch test.sak
   ```
   然后编写如下程序进行测试：
   ```bash
   echo "func fib(n: i32) -> i32 {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
   }
   
   func main() -> i32 {
        __println(\"Hello world, check fib(21):\");
        return fib(21);
   }" >> test.sak
   ```

5. **运行编译器** (可选测试):
    ```bash
    ./SakuraE
    ```

如果遇到问题，请确保已安装 LLVM 开发库 (例如 `llvm-dev` 包) 并且 `llvm-config` 在 PATH 中可用

## IR 开发
关于 IR 的更详细开发规范，请点击下方链接：

[IR README](Compiler/IR/README-zh_cn.md)

## 运行时库
如果你想了解关于SakuraE运行时库的内容，点击下面的链接：

[SakuraE Runtime Library](Runtime/README-zh_cn.md)

## 贡献者

- [The Flysong](https://github.com/theflysong) : 提供了本项目Parser的设计思路

## 赞助商

- [SendsTeam](https://github.com/SendsTeam) : 在开发过程中提供 LLM 服务
