# C++ 命名与 legacy interface policy

新增的手写 C++ function、method、data member 与 local helper 使用
`snake_case`。type name、concept 与 enum value 延续项目现有 C++ 风格。该规则同时
约束 declaration 和 use site；generated source 由其 generator 拥有，必须通过该 pipeline
修改。

公开 spelling 是 compatibility contract。即使仓库内没有 consumer，也不能据此删除或改名：
下游可能 include installed header，或直接初始化 public descriptor。未来的 public rename
必须提供经 review 的 compatibility path，或明确作为 versioned API break。

## 已审计而保留的 spelling

| Spelling | Scope | 保留理由 |
| --- | --- | --- |
| 既有 public camelCase resolver entry point，包括 `resolveModule`、`resolveModuleOnly`、`resolveAndValidateModule`、`validateModule`、`checkModuleAvailability` 与 `resolveInstruction` | Installed public resolution API | 这些既有 spelling 保持 source-compatible public API。`snake_case` 规则用于新的 handwritten/internal interface；它不隐含重命名既有 overload 或 entry point。 |
| `detail::selectVariant` 及 `resolved_ir::selectVariant` compatibility alias | Installed resolution-detail header；generated resolver 会调用 | 这是 public template adapter。改名会破坏直接使用它的下游，并需要协调 generator 与 API migration。新的 non-public helper 使用 `snake_case`。 |
| `resolve_fields` | Resolution-detail interface；generated resolver 会调用 | 已是 `snake_case`；它是 generated descriptor 与 handwritten resolution 的稳定 bridge。 |
| `SyntaxVariantDescriptor::get_required_modifier_num` | Public descriptor header | 既有 installed method spelling 和 generated descriptor contract。除非明确批准兼容式 public API migration，否则保留。 |
| `SyntaxInstructionDescriptor::Opcode_name` | Public aggregate descriptor 与 generator output | 既有 aggregate-designator spelling 由 generated code 使用，也可能被下游 aggregate initialization 使用。不能静默改名。新的 descriptor field 使用 `snake_case`，例如 `ResolvedInstructionDescriptor::opcode_name`。 |
| `check_end` | Installed descriptor namespace 与 generator model | 历史 namespace taxonomy，不表示 checker 是否完成。其 public generated descriptor 使机械 namespace rename 成为 API/generator migration，而不是 formatting cleanup。 |

`ptx_frontend_common` 中已废弃的 commented-out source/test target 已从 CMake file
删除，不再作为 dormant build policy 保留；剩余 interface library 与 exported target 是有意的。

## Formatting gate

CI 固定 `clang-format` major 21，并只对 `git ls-files` 返回的文件执行
`--dry-run --Werror`。`generated`、`vendor`、`third_party` 目录下的 C/C++ path，以及
`.gen.*` 文件被排除，因为它们由 generation 或 vendoring workflow 拥有。应 regenerate
这些 artifact，不应手工执行 formatter 修改。
