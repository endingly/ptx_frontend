# SETP 覆盖情况

本文记录 frontend 建模的完整 PTX 9.3 `setp` contract。它补充[语法覆盖矩阵](syntax_coverage.md)，不表示 simulator execution 或 physical GPU 行为。唯一的 machine-readable source 是 `python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml`。

规范依据为 NVIDIA PTX ISA 9.3 archive：[ordinary SETP §9.7.6.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#comparison-and-selection-instructions-setp) 和 [half/bfloat SETP §9.7.7.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#half-precision-comparison-instructions-setp)。

下表每行都经过 syntax、Resolved IR 与 target-aware checking 建模；均只定义 frontend contract，不提供 simulator semantics。

| Form | 最低 PTX / target | Comparison 与 Boolean modifier | Destination 与 source contract |
| --- | --- | --- | --- |
| ordinary bit `.b16/.b32/.b64` | 1.0 / 全部 target | `.eq/.ne`，可选 `.and/.or/.xor` | 一个 predicate 或 predicate pair；ordinary destination 的任一位置可为 `_`，但 pair 不能同时丢弃两个结果。source 是同宽 register-or-immediate。 |
| ordinary signed `.s16/.s32/.s64` | 1.0 / 全部 target | `.eq/.ne/.lt/.le/.gt/.ge`，可选 Boolean form | 采用 ordinary single/pair sink 和 source 规则。 |
| ordinary unsigned `.u16/.u32/.u64` | 1.0 / 全部 target | signed-family comparison 加 `.lo/.ls/.hi/.hs`，可选 Boolean form | 采用 ordinary single/pair sink 和 source 规则。 |
| ordinary `.f32` | 1.0 / 全部 target | 全部 floating comparison：`.eq/.ne/.lt/.le/.gt/.ge/.equ/.neu/.ltu/.leu/.gtu/.geu/.num/.nan`；可选 Boolean form 与 `.ftz` | 采用 ordinary single/pair sink 和 floating register-or-immediate source 规则。 |
| ordinary `.f64` | 1.0 / `sm_13` | 相同 floating comparison 与可选 Boolean form；不允许 `.ftz` | 采用 ordinary single/pair sink 和 floating register-or-immediate source 规则。 |
| `.f16` | 4.2 / `sm_53` | 全部 floating comparison、可选 Boolean form、可选 `.ftz` | 恰有一个 predicate result。source 是 `.f16` 或同宽 `.b16` register，或 numeric F16 floating constant；也接受显式 raw B16 integer word。destination 不能为 `_`。 |
| `.f16x2` | 4.2 / `sm_53` | 全部 floating comparison、可选 Boolean form、可选 `.ftz` | 恰有两个 predicate result，source 是 native `.f16x2` 或 `.b32` register；不允许 sink destination。 |
| `.bf16` | 7.8 / `sm_90` | 全部 floating comparison 与可选 Boolean form；不允许 `.ftz` | 恰有一个 predicate result；接受实际 `.b16` register、numeric BF16 word 或显式 raw B16 integer word，destination 不能为 `_`。 |
| `.bf16x2` | 7.8 / `sm_90` | 全部 floating comparison 与可选 Boolean form；不允许 `.ftz` | 恰有两个 predicate result，source 是 exact `.b32` register；不允许 sink destination。 |

## 有意保留的边界

ordinary syntax 允许单个 `_` destination，或 predicate pair 的一个 `_` lane。frontend 显式表示该信息，checker revalidation 会拒绝被 public IR 编辑为两个 sink 的 pair。half 与 bfloat form 保持 required single/pair predicate destination；native F16x2 source 仅接受 `.f16x2` 或 `.b32`，非 fundamental BF16x2 source 保留 exact `.b32` container；任意其他同宽 type 仍被排除。

可选 Boolean operand 是 predicate source，而不是只能使用 predicate register 的字段。它接收 plain/complemented predicate register，也接收 PTX integer predicate constant。constant 按 C truth rule canonicalize：零为 false，非零为 true；前导 `!` 对 truth value 取反。尽管 `mov.pred` 接收 predicate special register，`setp` 不接收此类 source。

`.f32` 与 `.f64` 是独立的 generated variant，因此 `.ftz` 只对 `.f32` 合法。这会令 PTX modifier language 保持 disjoint，也不需要额外 capability registry。扩展后的 `ComparisonOperator` domain 保持已有 `Eq`、`Lt`、`Ge` 的 numeric identity，并在后面追加其余 normative suffix。

作为补充 compiler evidence，CUDA Toolkit 13.1 `ptxas` 在 PTX 8.0、`sm_90` probe 中接收 Boolean integer constant（包括 complemented constant），但拒绝把 numeric immediate 用作 `.f16`、`.f16x2`、`.bf16` 与 `.bf16x2` comparison source。这些 form 早于 PTX 8.0，且 PTX 9.3 未记录 immediate-source extension。该旧证据是历史补充，不是普通 scalar-half literal 的规范性全面禁令，也不是当前 PTX 9.3/GPU 结果；下方显式标量支持替代这一禁令推论。

## 验证来源

[专用 C++ SETP 测试](../../../submod/resolved_ir/test/test_setp_completeness.cpp) 覆盖所有 family 的 parse、resolve、declared-operand checking、target minimum、modifier/layout negative、predicate-constant truth value，以及 mutation 后的 IR sink validation。[Python database 测试](../../../python/tests/spec/test_setp_completeness.py) 验证 variant、comparison domain、availability、sink boundary、predicate-source kind 和 packed container。这些来源仅定义 frontend 边界；不验证 simulator 或 hardware execution result。

Scalar F16 numeric floating source 保留 RegOrImm 与公共 binary16 RNE converter。下方实现为四个 F16 及四个 BF16 字段另设显式 Bit16 integer arm，复用已经接受的 logical BF16 converter，不改变 Boolean/predicate 或 packed policy。当前父版本的 model、Debug 与 installed ABI 检查已经通过；独立最终验收仍待完成。

## 显式标量 Bit16 来源

实现已经通过完整 model/default 审核、normal generation、定向 Debug 检查及匹配的 installed consumer；独立最终验收仍待完成。
`integer_immediate_type` 是尾部 optional `ScalarType` member，默认缺省。
仅八个既有 scalar SETP source row 选择 B16：F16/BF16 普通及 Boolean form 的
src1/src2。四个 BF16 source member 从 register reference 迁移为 `RegOrImm`，
采用 logical BF16 和 SameWidth；四个 F16 member 的 kind/type/width 不变。
Descriptor ABI 变化要求重新编译所有 consumer。

来源只求值一次。整数结果选择显式 raw B16 arm：`1` 是 word `0x0001`，
`0x10001` 窄化为同一 word 并保留 64-bit integer origin。浮点结果沿用既有 logical
converter：`1.0` 为 F16 `0x3c00` 或 BF16 `0x3f80`。直接 unsigned `0f` 保留
既有 root route 和 lexical precision，不通过包装或试转换分类。缺省 policy 的普通
logical floating operand 仍拒绝 integer origin，packed format 也不变。本支持不扩展
packed SETP、atomic、storage、call 或其他 scalar source 权限。

当前 immediate 由实际 type 选择 arm，两个 word 都限制在 16 bit。Logical numeric
word 没有 integer origin 或 integer negative flag；B16 origin 可以缺省，存在时仍检查
mask/sign 一致性，cache-free word 要求 negative flag 为 false。合法 word/arm 修改
不认证已经擦除的源码历史；变更 supplied syntax 仍执行独立的 source association。
Instruction check 的证明使用完整 target context，AvailableContext 延后不构成合法性证书。
F16 实际 carrier 仍为 B16 或 F16，BF16 仍为实际 B16，并保留 intrinsic declaration/class
检查；不改变 Base compatibility。
