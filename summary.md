# PTX Parser Parsing 生成逻辑审查

## 当前修复进度（截至 `599be47`）

- [x] 公开 parser API 可链接，并移除了 lexer 对测试专用 `ptx_intern` 的依赖（`c04818c`）。
- [x] parser 按 operand kind 解析，覆盖负数、浮点、位模式、向量和地址等当前支持的 operand（`599be47`）。
- [x] modifier 按 PTX syntax 中声明的 slot 顺序校验（`599be47`）。
- [x] availability 已生成到 parser；调用方可用 `ParserOptions::target` 启用 PTX/SM/family 校验（`599be47`）。
- [ ] 同 opcode、同 modifier、仅 operand layout 不同的候选，仍不能按 operand 形态分派。
- [x] 已删除旧 parser generator 路径、收敛不支持的 schema 配置、修复生成文件格式化，并补充 Python 与 C++ parser 测试（`599be47`）。

## 审查结论

### 1. 高：公开 parser API 当前无法正常链接（已修复：`c04818c`）

`include/ptx_parser.hpp:24` 声明了 `PtxParser` 和便捷的
`parseInstruction()`，但 `src` 中没有对应实现。同时 lexer 依赖全局
`ptx_intern`，`src/ptx_lexer.l:14` 只有声明，库中该符号保持未定义；现有
测试在 `test/test_ptx_lexer.cpp:13` 自行提供实现，掩盖了链接问题。

### 2. 高：operand 约束在生成过程中丢失（已修复：`599be47`）

`python/code_gen/gen_parser_category.py:754` 的 operand view 只保留字段名；
模板对所有 operand 统一调用 `parseOperandWithLoc()`，不区分 `reg`、
`reg_or_imm` 等类型。实测 `add.u32 1, 2, 3;` 被接受，尽管 `dst` 在
spec 中要求寄存器。

### 3. 高：负立即数等合法 operand 无法解析（已修复：`599be47`）

`ParserCore::parseOperandWithLoc()` 只处理 `Ident`、无符号十进制和十六进制，
`src/ptx_parser_core.cpp:129` 不处理 lexer 已提供的 `Minus`、浮点、向量、
地址表达式等 token。实测 `add.s32 %r1, %r2, -1;` 被拒绝。

### 4. 中：modifier 顺序没有按 PTX syntax 校验（已修复：`599be47`）

模板把 modifiers 当作无序集合扫描，见
`python/templates/ptx_parser_category.gen.cpp.j2:25`。因此实测
`add.u32.sat ...` 被接受，而声明语法是 `add{.sat}.{type}`。建议 view
model 保留 modifier slot 顺序，并按 slot 顺序匹配。

### 5. 中：availability 信息完全没有进入生成 parser（已修复：`599be47`）

`VariantSpec` 虽保存 PTX/SM availability，但 `ParserVariantView` 没有对应
字段。实测要求 PTX 9.2、SM 120 的 `add.u8x4` 无条件通过。若 parser 只
负责语法，应明确由语义检查器接收目标版本并验证；当前代码中看不到这层保护。

### 6. 中：同 opcode 多候选只能靠 modifiers 区分

registry 在消费 operand 前就计算所有候选，见
`python/templates/ptx_parser_registry.gen.cpp.j2:20`。因此两个同名 opcode
如果 modifiers 相同、仅 operand layout 不同，会被直接报告 ambiguous，无法
按照 operand 形态分派。

### 7. 中：schema/normalize 宣称支持的配置与 parser generator 不一致（已修复：`599be47`）

`normalize` 接受 `emit.kind: custom`，见 `python/code_gen/normalize.py:152`，
但 parser view 构建会报 unsupported，见
`python/code_gen/gen_parser_category.py:275`。`optional_policy` 等 backend
字段也没有被 parser generator 使用。应当删除尚未支持的 schema 能力，或在
统一的 capability validation 阶段给出明确错误。

## Python 可维护性（已修复：`599be47`）

- `gen_parser.py` 是约 880 行的旧生成路径，而 `gen_all.py` 实际使用
  category/registry 新路径；两套 view model、命名和模板长期并存，很容易修错
  位置。建议删除旧路径或明确标为 legacy 并停止安装入口。
- Jinja `Environment` 创建、输出写入、命名转换和部分 sub-variant 校验在多个
  模块重复，适合提取共享的 renderer 与 semantic validator。
- `format_generated_files()` 使用非递归 `glob`，见
  `python/scripts/gen_all.py:180`，但产物实际位于 `public/`、`private/`，所以
  格式化当前不会处理任何生成文件。直接使用已经收集的 `generated_files` 最稳妥。
- CMake 的生成文件清单硬编码了 `integer_arithmetic`，见 `CMakeLists.txt:49`。
  新增 category 后必须手工同步且重新配置，否则新 `.gen.cpp` 可能不进入 target。
- 没有 Python generator 测试，也没有 parser C++ 测试。目前 19 个测试全部是
  lexer 测试，无法防止以上回归。

## 验证结果

审查时现有 19 个 CTest 全部通过；完整 codegen 可运行。另用生成产物做了最小
解析探针，确认了 operand 类型、modifier 顺序、availability 和负立即数问题。
后续 `599be47` 新增了 Python codegen 测试及 C++ parser 参数化测试，覆盖已修复
的行为。

## 建议修复顺序

1. [x] 恢复可链接的公开 API，并明确 `ptx_intern` 的所有权。
2. [x] 实现按 operand kind 分派及完整 literal 解析。
3. [x] 处理 modifier 顺序和版本语义。
4. [x] 收敛旧生成器并补充 generator golden tests、parser 参数化测试。
5. [ ] 让 registry 在同 opcode、同 modifier 的多个候选间按 operand layout 分派。
