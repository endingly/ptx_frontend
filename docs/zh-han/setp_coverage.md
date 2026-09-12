# SETP 覆盖情况

本文记录 frontend 建模的完整 PTX 9.3 `setp` contract。它补充[语法覆盖矩阵](syntax_coverage.md)，不表示 simulator execution 或 physical GPU 行为。唯一的 machine-readable source 是 `python/code_gen/resources/ptx_spec/comparison_and_selection.yaml`。

规范依据为 NVIDIA PTX ISA 9.3 archive：[ordinary SETP §9.7.6.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) 和 [half/bfloat SETP §9.7.7.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp)。

下表每行都经过 syntax、Resolved IR 与 target-aware checking 建模；均只定义 frontend contract，不提供 simulator semantics。

| Form | 最低 PTX / target | Comparison 与 Boolean modifier | Destination 与 source contract |
| --- | --- | --- | --- |
| ordinary bit `.b16/.b32/.b64` | 1.0 / 全部 target | `.eq/.ne`，可选 `.and/.or/.xor` | 一个 predicate 或 predicate pair；ordinary destination 的任一位置可为 `_`，但 pair 不能同时丢弃两个结果。source 是同宽 register-or-immediate。 |
| ordinary signed `.s16/.s32/.s64` | 1.0 / 全部 target | `.eq/.ne/.lt/.le/.gt/.ge`，可选 Boolean form | 采用 ordinary single/pair sink 和 source 规则。 |
| ordinary unsigned `.u16/.u32/.u64` | 1.0 / 全部 target | signed-family comparison 加 `.lo/.ls/.hi/.hs`，可选 Boolean form | 采用 ordinary single/pair sink 和 source 规则。 |
| ordinary `.f32` | 1.0 / 全部 target | 全部 floating comparison：`.eq/.ne/.lt/.le/.gt/.ge/.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`；可选 Boolean form 与 `.ftz` | 采用 ordinary single/pair sink 和 floating register-or-immediate source 规则。 |
| ordinary `.f64` | 1.0 / `sm_13` | 相同 floating comparison 与可选 Boolean form；不允许 `.ftz` | 采用 ordinary single/pair sink 和 floating register-or-immediate source 规则。 |
| `.f16` | 4.2 / `sm_53` | 全部 floating comparison、可选 Boolean form、可选 `.ftz` | 恰有一个 predicate result。source 是 `.f16` 或同宽 `.b16` register；destination 不能为 `_`。 |
| `.f16x2` | 4.2 / `sm_53` | 全部 floating comparison、可选 Boolean form、可选 `.ftz` | 恰有两个 predicate result，source 是 exact `.b32` register；不允许 sink destination。 |
| `.bf16` | 7.8 / `sm_90` | 全部 floating comparison 与可选 Boolean form；不允许 `.ftz` | 恰有一个 predicate result，source 是 exact `.b16` register；destination 不能为 `_`。 |
| `.bf16x2` | 7.8 / `sm_90` | 全部 floating comparison 与可选 Boolean form；不允许 `.ftz` | 恰有两个 predicate result，source 是 exact `.b32` register；不允许 sink destination。 |

## 有意保留的边界

ordinary syntax 允许单个 `_` destination，或 predicate pair 的一个 `_` lane。frontend 显式表示该信息，checker revalidation 会拒绝被 public IR 编辑为两个 sink 的 pair。half 与 bfloat form 保持 required single/pair predicate destination；其 packed source container 为 exact，而不只是 width-compatible。

可选 Boolean operand 是 predicate source，而不是只能使用 predicate register 的字段。它接收 plain/complemented predicate register，也接收 PTX integer predicate constant。constant 按 C truth rule canonicalize：零为 false，非零为 true；前导 `!` 对 truth value 取反。尽管 `mov.pred` 接收 predicate special register，`setp` 不接收此类 source。

`.f32` 与 `.f64` 是独立的 generated variant，因此 `.ftz` 只对 `.f32` 合法。这会令 PTX modifier language 保持 disjoint，也不需要额外 capability registry。扩展后的 `ComparisonOperator` domain 保持已有 `Eq`、`Lt`、`Ge` 的 numeric identity，并在后面追加其余 normative suffix。

作为补充 compiler evidence，CUDA Toolkit 13.1 `ptxas` 在 PTX 8.0、`sm_90` probe 中接收 Boolean integer constant（包括 complemented constant），但拒绝把 numeric immediate 用作 `.f16`、`.f16x2`、`.bf16` 与 `.bf16x2` comparison source。这些 form 早于 PTX 8.0，且 PTX 9.3 未记录 immediate-source extension。该证据支持 frontend boundary，但不等同于 PTX 9.3 assembler 或 physical-GPU execution result。

## 验证来源

[专用 C++ SETP 测试](../../submod/resolved_ir/test/test_setp_completeness.cpp) 覆盖所有 family 的 parse、resolve、declared-operand checking、target minimum、modifier/layout negative、predicate-constant truth value，以及 mutation 后的 IR sink validation。[已安装 consumer SETP 测试](../../submod/resolved_ir/test/package_consumer/setp_completeness.cpp) 只用 installed public header 验证 floating form、ordinary sink pair、predicate constant，以及 checker 对 selected domain 外 comparison enum 的 rejection。focused Python database test 验证 variant、comparison domain、availability、sink boundary、predicate-source kind 和 packed container。这些来源仅定义 frontend 边界；不验证 simulator 或 hardware execution result。
