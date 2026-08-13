# PTX Lexer 设计

## 概述

PTX lexer 将源文本转换为带有文本和源码范围的 token 流。它使用可重入的
Flex scanner 实现，并通过 C++ `PtxLexer` 类对外提供接口。

Lexer 有意只负责词法分类。它识别标点、字面量、标识符、部分稳定的 directive、
空白和注释，但不会穷举所有 PTX 指令、指令后缀、类型、shape、cache operator
或 scope。

主要实现文件如下：

- `src/ptx_lexer.l`：Flex 规则、scanner 配置和位置跟踪。
- `src/ptx_lexer.cpp`：管理生成 scanner 的 C++ 所有权封装。
- `include/ptx_lexer.hpp`：公开 lexer 接口和 token 表示。
- `include/ptx_token.hpp`：token kind 和 Flex 语义值类型。
- `test/test_ptx_lexer.cpp`：lexer 行为测试。

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

### 标点

每个标点字符具有独立的 token kind，包括：

```text
, . : ; @ ( ) [ ] { } < > <= >= << >> - + * / % & && ^ | || ! ~ ? = == !=
```

正负号不属于数值 token。例如，`-1` 会产生一个 `Minus`，随后产生一个
`Decimal`。多字符运算符拥有独立 token kind，供 declaration constant-expression
parser 保留准确的运算符和优先级。

### 字面量

Lexer 可以识别：

- 十进制整数，以及可选的 `u` 或 `U` 后缀。
- 以 `0x` 或 `0X` 开头的十六进制整数，以及可选的无符号后缀。
- 由 `0f` 和八个十六进制数字组成的 PTX 浮点 bit-pattern 字面量。
- 由 `0d` 和十六个十六进制数字组成的 PTX 双精度 bit-pattern 字面量。
- 带小数部分或指数部分的十进制浮点字面量。
- 可以包含转义字符的双引号字符串。
- 内建拼写 `WARP_SZ`。

字面量 token 保留其准确的源码拼写。数值转换和字符串反转义不属于 lexer
职责。

### 通用标识符

普通标识符符合以下形式：

```text
[A-Za-z_$%][A-Za-z0-9_$%]*
```

它们被输出为 `TokenKind::Ident`。该类别包括指令名、寄存器、特殊寄存器、
label、symbol，以及 `sm_80` 这样的 target 名称。

将指令名称保持为通用标识符，可以避免 PTX 每增加一条指令就修改 lexer。

### 通用点号标识符

大部分点号前缀拼写被输出为 `TokenKind::DotIdent`。通用形式支持使用 `::`
连接的 atom，也支持逗号分隔的 atom：

```text
.sat
.u32
.shared::cluster
.collector::b0::smem
.scale::2,1
```

`.16x64b` 这类以数字开头的 shape 也可以被识别。

连续后缀会保持为多个独立 token。例如：

```text
add.sat.s32
```

被切分为：

```text
Ident("add") DotIdent(".sat") DotIdent(".s32")
```

部分 PTX 拼写会在同一个后缀中包含额外的普通点号。已知形式，例如
`.async.global` 和 `.b8x16.b6x16_p32`，具有显式规则，并保持为一个
`DotIdent` token。

### 专用 Directive Token

少量稳定且具有重要结构意义的 PTX directive 使用专用 token kind，包括：

- `.version`、`.target` 和 `.file` 等 module/debug directive。
- `.visible` 和 `.extern` 等 visibility/linking directive。
- `.entry` 和 `.func` 等 function directive。
- `.maxnreg` 和 `.reqntid` 等 kernel tuning directive。
- `.reg`、`.global` 和 `.param` 等 declaration directive。

这些规则位于通用 `DOT_IDENT` 规则之前。Flex 首先选择最长匹配，并使用规则
顺序解决等长匹配，因此专用 directive 会优先于通用类别。

一个专用拼写在所有词法上下文中都保持其 token kind。例如，即使 `.global`
出现在一组点号前缀名称中，它仍然会被识别为 `DotGlobal`。需要同时接受两类
token 的调用方应当在适当场景下依据 token 拼写处理。

## 空白和注释

空格、制表符和换行符会被跳过。

行注释以 `//` 开头，并持续到行末，但注释规则本身不消费换行符。随后的空白
规则负责消费换行符。

块注释使用独占的 `BLOCK_COMMENT` scanner 状态：

1. `/*` 进入该状态。
2. 跳过所有内容，直到遇到 `*/`。
3. `*/` 返回初始状态。
4. 如果在该状态中遇到输入结尾，则输出 `TokenKind::Error`。

块注释不支持嵌套。

被跳过的文本仍然会经过 `YY_USER_ACTION`，因此空白和注释即使不产生 token，
也仍然会推进源码位置。

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
- 当 `\r\n` 两个字节属于同一次 Flex 匹配时，它们被视为一个换行。

列号按字节而不是 Unicode code point 计数。PTX 标识符当前仅接受 ASCII，
因此该计数方式与现有词法语法一致。

EOF 和未终止块注释使用当前位置上的零宽范围。

## 错误行为

无法识别的字符会被输出为 `TokenKind::Error`。`Token::text` 包含该错误字节，
`Token::range` 是其正常源码范围。

未终止的块注释会输出 `TokenKind::Error`，文本为空，范围是输入结尾处的零宽
范围。

Lexer 使用 error token 报告错误，而不是抛出异常。调用方可以在第一个错误处
停止，也可以在适合恢复的情况下继续请求 token。

EOF 被输出为 `TokenKind::Eof`，文本为空，范围是当前位置上的零宽范围。

## 构建集成

CMake 使用 `FLEX_TARGET`，在私有生成目录中生成 scanner 实现和头文件。生成的
source 与 `src/ptx_lexer.cpp` 一同编译到 `ptx_frontend` library 中。

修改 `src/ptx_lexer.l` 后，正常构建流程会重新生成 scanner。

## 测试策略

Lexer 测试构建为独立的 `ptx_lexer_test` 可执行程序，不与更高层组件共享测试
可执行程序。

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

可以直接运行 lexer 测试：

```sh
./out/build/ci-linux-gcc-debug/ptx_lexer_test
```

也可以通过 CTest 运行：

```sh
ctest --test-dir out/build/ci-linux-gcc-debug --output-on-failure
```

## 已知限制

- 每个输出 token 的文本都会单独分配。
- Lexer 只接受 ASCII 标识符。
- 块注释不能嵌套。
- 块注释中的 `\r\n` 当前会作为两次独立 Flex 匹配被消费，因此行号会推进
  两次。在块注释之外，CRLF 通常在同一次空白匹配中被消费，行号只推进一次。
- 字符串 token 保留转义和引号，不进行解码。
- 正负号与数值字面量分开输出。
- 十进制浮点语法并不接受 C++ 数值转换函数可能接受的所有拼写。
- `yy_scan_bytes` 接受 `int` 长度，而公开构造函数接受
  `std::string_view::size_type`；对于极大的输入，需要在窄化转换前显式检查
  长度。
- 公开对象不提供 reset 操作，并且不可移动。

修改这些限制时，应当同时增加有针对性的 lexer 测试，以明确新的词法契约。
