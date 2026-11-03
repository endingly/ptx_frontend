# PTX Lexer 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#lexer-design) 为唯一维护依据；本文保留技术参考与历史证据。

## 概述

PTX lexer 将源文本转换为带有文本和源码范围的 token 流。它使用可重入的
Flex scanner 实现，并通过 C++ `PtxLexer` 类对外提供接口。

Lexer 有意只负责词法分类。它识别标点、字面量、标识符、部分稳定的 directive、
空白和注释，但不会穷举所有 PTX 指令、指令后缀、类型、shape、cache operator
或 scope。

lexer 源码树为 `submod/lexer/`，主要实现文件如下：

- `submod/lexer/src/ptx_lexer.l`：Flex 规则、scanner 配置和位置跟踪。
- `submod/lexer/src/ptx_lexer.cpp`：管理生成 scanner 的 C++ 所有权封装。
- `submod/lexer/include/ptx_lexer.hpp`：公开 lexer 接口和 token 表示。
- `submod/lexer/include/ptx_token.hpp`：token kind 和 Flex 语义值类型。
- `submod/lexer/test/test_ptx_lexer.cpp`：lexer 行为测试。

## 设计目标

Lexer 的设计目标包括：

1. 在 PTX 指令集持续扩展时保持稳定的词法行为。
2. Token 自己持有文本，使其在 lexer 前进或销毁后仍然有效。
3. 提供准确的、从 1 开始计算的行列范围。
4. 每个 `PtxLexer` 实例拥有独立的 scanner 状态。
5. 提供一个 token 的前瞻能力，同时不暴露 Flex 实现细节。
6. 使用明确的 error token 表示非法或无法识别的输入。

Lexer 不负责：

- 校验指令语法或 modifier 组合。
- 解释标识符或点号前缀名称的语义。
- 将数值文本转换为 C++ 数值。
- 自动从所有错误结构中恢复。
- 对 token 字符串进行驻留或去重。

## 公开接口

公开 lexer 接口如下：

```cpp
class PtxLexer {
 public:
  struct Token {
    TokenKind kind;
    std::string text;
    SourceRange range;
  };

  explicit PtxLexer(std::string_view src);

  Token next();
  Token peek();
  Token consume();
};
```

`next()` 总是直接向 scanner 请求下一个 token。

`peek()` 返回下一个 token，但不会在 C++ 接口层消费它。该行为通过缓存一个
token 实现。连续多次调用 `peek()` 会返回同一个 token 的副本。

当缓存存在时，`consume()` 返回缓存的 token；否则其行为与 `next()` 相同。

如果 `peek()` 已经缓存了 token，直接调用 `next()` 会绕过缓存。因此调用方
应当统一使用 `next()` 迭代方式，或者统一使用 `peek()`/`consume()` 方式，
不应混用两种方式。

`PtxLexer` 不可复制，也不可移动。每个实例拥有一个 Flex scanner、输入 buffer、
位置状态以及可选的前瞻 token。

## Scanner 集成

Flex 在私有生成目录中生成 `_ptx_lexer.cpp` 和 `_ptx_lexer.hpp`。这些文件属于
实现细节，并隐藏在 `PtxLexer::Impl` Pimpl 类型之后。

Scanner 使用以下 Flex 选项：

```text
reentrant
bison-bridge
noyywrap
nounput
noinput
extra-type="ptx_frontend::PtxLexerExtra*"
```

`reentrant` 使每个 lexer 拥有自己的 `yyscan_t`，而不是使用全局 scanner 状态。
因此不同 lexer 实例可以相互独立地使用，但同一个实例不支持并发访问。

`yy_scan_bytes` 会把构造函数接收到的输入复制到 Flex 持有的 buffer 中。因此
`PtxLexer` 构造完成后，调用方的源 buffer 不需要继续存活。

Scanner 生命周期如下：

1. 分配 `PtxLexer::Impl`。
2. 使用 `yylex_init` 初始化 `yyscan_t`。
3. 使用 `yyset_extra` 绑定 `PtxLexerExtra`。
4. 使用 `yy_scan_bytes` 将输入复制到 Flex buffer。
5. 析构时使用 `yy_delete_buffer` 删除 buffer。
6. 使用 `yylex_destroy` 销毁 scanner。

## 文本所有权

执行 Flex action 时，`yytext` 指向 scanner 内部 buffer。Flex 语义值临时以
`std::string_view` 暴露该文本：

```cpp
yylval->sv = std::string_view(yytext, yyleng);
```

这个 view 只会使用到 `yylex` 返回为止。`PtxLexer::next()` 会立即将它复制到
类型为 `std::string` 的 `Token::text` 中：

```cpp
return Token{kind, std::string(sval.sv), sval.range};
```

因此：

- Token 自己持有其源文本。
- Scanner 前进不会使先前 token 的文本失效。
- Lexer 销毁不会使已经返回的 token 文本失效。
- 不再需要全局字符串池或 `ptx_intern` 函数。

相应代价是每个输出 token 都需要构造一个字符串。当前设计有意优先保证明确的
所有权和简单的公开契约，而不是优先实现字符串驻留或零拷贝 token 存储。

## Token 分类

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--token-classification)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#token-classification)。

### 标点

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--punctuation)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#punctuation)。

### 字面量

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--literals)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#literals)。

### 通用标识符

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--generic-identifiers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#generic-identifiers)。

### 通用点号标识符

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--generic-dot-identifiers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#generic-dot-identifiers)。

### 专用 Directive Token

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--dedicated-directive-tokens)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#dedicated-directive-tokens)。

## 空白和注释

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--whitespace-and-comments)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#whitespace-and-comments)。

## 源码位置

源码位置从 1 开始计算。新的 lexer 从第 1 行、第 1 列开始。

每条匹配规则都会执行 `YY_USER_ACTION`：

1. 将当前位置保存为 token 起点。
2. 按匹配到的每个字节推进位置。
3. 将推进后的位置保存为 token 终点。

因此 `SourceRange` 表示半开区间 `[start, end)`。例如，位于第 4 列的单字符
token，其范围从第 4 列开始，到第 5 列结束。

换行处理规则如下：

- `\n` 使行号加一，并将列号重置为 1。
- `\r` 具有相同行为。
- `\r\n` 被视为一个换行，包括在块注释内。

列号按字节而不是 Unicode code point 计数。PTX 标识符当前仅接受 ASCII，
因此该计数方式与现有词法语法一致。

EOF 使用当前位置上的零宽范围。未终止块注释保留从开头 `/*` 到输入结尾的范围。

## 错误行为

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--error-behavior)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#error-behavior)。

## 构建集成

CMake 使用 `FLEX_TARGET`，在私有生成目录中生成 scanner 实现和头文件。生成的
source 与 `submod/lexer/src/ptx_lexer.cpp` 一同编译到 `lexer` target 中。

修改 `submod/lexer/src/ptx_lexer.l` 后，正常构建流程会重新生成 scanner。

## 测试策略

启用 `BUILD_TESTING` 时，CMake 将 lexer 测试构建为独立的 `test_lexer` 可执行程序，
不与更高层组件共享测试可执行程序。

当前测试覆盖：

- EOF 输出。
- 通用指令标识符。
- 通用类型和 modifier 后缀。
- 复合点号标识符和以数字开头的点号标识符。
- 专用 module、function、visibility 和 declaration directive。
- 寄存器声明、内存 operand 和 predicate 标点。
- constant expression 的单字符与多字符运算符。
- 整数、十六进制、浮点 bit-pattern、十进制浮点和字符串字面量。
- 空白和注释跳过。
- 未终止块注释错误。
- `peek()` 和 `consume()` 行为。

可以构建 lexer 测试 target：

```sh
cmake --build out/build/ci-linux-gcc-debug --target test_lexer
```

也可以通过 CTest 运行：

```sh
ctest --test-dir out/build/ci-linux-gcc-debug --output-on-failure
```

## 已知限制

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--known-limitations)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#known-limitations)。

## 上下文中的 instruction keyword

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#lexer-design--contextual-instruction-keywords)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/lexer_design.md#contextual-instruction-keywords)。
