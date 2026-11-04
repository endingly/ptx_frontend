# YAML Instruction Spec：结构与原则

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec) 为唯一维护依据；本文保留技术参考与历史证据。

## 目的

`python/src/ptx_frontend/spec/resources/ptx_spec/` 中的 YAML 是 PTX 指令事实的 canonical
声明来源（`instructions/ptx_spec/` 是源码构建使用的仓库输入目录）。它描述合法的源码
形式、variant、operand layout、availability 和规则标识；Python generator 从中同时
生成 Syntax descriptor、Resolved descriptor、checker descriptor 和 C++ instruction
结构。它不是 C++ 代码模板，也不是 backend layout 配置。

每个文件使用同级 package schema
`python/src/ptx_frontend/spec/resources/ptx-instr-v1.schema.yaml`（YAML comment 通过
`../ptx-instr-v1.schema.yaml` 引用）：

```yaml
schema: ptx-instr/v1
ptx_isa: "9.3"
category: arithmetic
codegen_category: arithmetic
```

schema 负责字段形状和基础枚举；normalizer 负责跨字段的生成器不变量，例如一个
variant 不能同时写 `operands` 和 `operand_layouts`。schema enum、YAML entry 或 `rule`
identifier 本身都不是 executable checker：只有 normalizer、generated descriptor、resolver
和 checker 已实现对应 contract 时才有意义。

## 扩展边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--extension-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#extension-boundary)。

## 预定义数据引用

本 DSL 中 `$name` 是对当前 YAML 文件预定义数据的唯一引用写法，无需引号。`type_sets`
与 `value_sets` 在 `values` 中通过 `$name` 引用，`operand_patterns` 在 `operands` 中也通过 `$name` 引用；
裸字符串不再表示引用。normalizer 在生成前递归展开引用、检测循环，并保证引用与原地写出
相同数据得到完全相同的 `InstructionSpec`。`$` 不用于 type expression；后者有独立的函数
语法，避免与预定义数据引用混淆。

## 顶层结构

常用顶层字段如下：

| 字段 | 含义 |
| --- | --- |
| `type_sets` | 可由 `$name` 引用的 modifier value 集合 |
| `value_sets` | 可由 `$name` 引用的非类型 modifier value 集合 |
| `operand_patterns` | 可由 name 复用的 operand 列表 |
| `category` | 当前 YAML 对应的 PTX 文档分类 |
| `codegen_category` | 当前 YAML 内指令的生成源码分区 |
| `instructions` | 一个或多个 opcode 定义 |

每个 instruction 至少包含 `opcode` 和 `variants`，可以有 instruction-level 的
`syntax`、`operands`、`section` 与 `doc`。`category` 与 `codegen_category` 都是必需的
文件级字段，不得在 instruction 中重复。一个 opcode 可以自然分散在多个 category YAML
中；同 opcode 的所有定义必须使用相同的 `codegen_category`。当 variant 属于不同 PTX
文档节时，也可以在同一 YAML 中拆成多个 instruction 定义。`arithmetic.yaml` 是刻意将
PTX 9.7.1 至 9.7.5 合并的例外。

合并按 spec 文件路径及文件内声明顺序进行。文件路径本身就是定义来源，不再需要额外的
`fragment` ID。database 会在发射代码前拒绝重复 variant ID、PascalCase 后冲突的
C++ variant 名、存在歧义的 modifier slot 绑定，以及可接受同一有序 modifier 序列
（包括显式声明的顺序别名）的重叠 variant。

## Variant 与 modifier

variant 的 `name` 是稳定 machine-readable identifier；它生成 C++ variant 名与
descriptor key。当前模型中，一个 variant 表示一组互斥的 modifier slot/presence 与
value 约束，而不是由 operand 数量或版本区间确定。只有 allowed value 不同、其余形式
相同的版本演进应合并到同一 variant。

modifier 的核心字段：

```yaml
- name: type
  kind: type
  domain: scalar_types
  presence: required
  values: [$add_integer_scalar]
```

`presence` 的语义为：

| 值 | 语义 |
| --- | --- |
| `absent` | 该 modifier kind 不得出现 |
| `optional` | 可不出现；出现时必须匹配允许 spelling |
| `required` | 必须出现并匹配一个 `values` spelling |
| `fixed` | 必须出现且 value 固定；resolved struct 生成 static constexpr 成员 |

`optional` modifier 必须显式给出省略时的语义 `default`。default 的类型必须与
modifier kind 一致：`flag` 使用布尔值；`type` 使用 `values` 中的 scalar type；
`rounding` 使用 `values` 中的舍入模式（如 `rn`）；legacy `cache` 使用语义上的
source-absence sentinel `unspecified`。
例如：

```yaml
- name: sat
  kind: flag
  presence: optional
  token: .sat
  default: false

- name: type
  kind: type
  presence: optional
  values: [u32, u64]
  default: u32

- name: rounding
  kind: rounding
  domain: rounding_modes
  presence: optional
  values: [$rounding_modes]
  default: rn

- name: cache
  kind: cache
  domain: cache_operators
  presence: optional
  values:
    - value: [ca, cg, cs, lu, cv]
      availability: {ptx: "2.0", sm: 20}
  default: unspecified
```

省略 modifier 时，resolver 将 default 写入 resolved field，并令其 `locs` 为空；
显式 modifier 会覆盖 default，并保存其源码位置。`absent`、`required` 与 `fixed`
不得书写 `default`。default 仍是有效语义值，因此其 value availability 仍会被 checker
检查；因为没有 modifier 源码位置，相关诊断回退到整条 instruction 的 range。legacy
`cache` 是例外：`unspecified` 不是可拼写的 PTX value，而是 source absence sentinel，
因此不会触发 modifier-value availability。

`constraints` 可携带 typed `memory_consistency` descriptor。它引用生成后的
semantics、scope、cache 与 address field（也可引用 mmio/state-space field）；
normalization 会拒绝未激活或未知的引用。memory qualifier 在 syntax 中保持独立，而
descriptor-backed checker 统一执行 cross rule。`omitted` 与 `none` 是 source-absence
default，不是可拼写的 modifier value。

`flag` 通常给出 `token: ".sat"`；`type` 的 token 通常从 `value` 或 `values` 推导。
`name` 是当前 variant 内的 modifier slot ID，`kind` 决定解析后的值类型。同一个
spelling 可以在不同 variant 绑定不同 slot，例如 `.f32` 在普通 Add 中绑定 `type`，在
mixed Add 中绑定 `result_type`。matching 遵循 `modifiers` 的声明顺序；可以跳过
optional 和 absent slot，但不能跳过 required slot。只有 required/fixed slot 可以
共享 spelling，且必须由有序位置消除绑定歧义。不同 variant 接受的有序 modifier
序列必须互斥，否则 database 会在生成前拒绝。

以固定版本 PTX 手册的 **Syntax** 顺序作为规范声明顺序。有兼容性证据支持其他拼写时，
variant 可以声明 `modifier_order_aliases`：每项必须是全部 modifier slot 名称的完整
排列，包括 absent slot。例如 mixed Add/Sub 的规范顺序是
`rounding, sat, result_type, input_type, ftz`，同时保留历史顺序：

```yaml
modifier_order_aliases:
  - [rounding, result_type, input_type, ftz, sat]
```

别名选择同一个 variant，绑定相同的语义 field、default 与源码位置；它不允许任意
重排，也不放宽 checker 约束。database 会拒绝不完整的排列，以及让同一源码序列
产生不同 slot 绑定的别名。

此规范顺序策略用于解决固定版本
[mixed-precision Sub 文档](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#mixed-precision-floating-point-instructions-sub)
中的不一致：Syntax 将 `.sat` 放在 type 之前，而一个 example 将它放在 type 之后。
两种形式都保留；这不意味着 Syntax 穷举了 assembler 接受的所有拼写。

使用 Python 包生成代码的消费者应同步升级 C++ frontend，并重新生成、编译产物。
variant ID 和 field 名称稳定不代表二进制布局兼容：规范 slot 重排也会改变生成成员的
顺序，syntax descriptor 还增加了 alias span。

`values` 的单项可改写为对象，为某个语义值追加 target availability：

```yaml
values:
  - u32
  - value: u64
    availability: {ptx: "2.0", sm: 20}
```

未写 availability 的值不增加 variant 的要求；选中 `u64` 时，checker 会在 variant/layout
之后额外检查此值的 PTX、SM 与 family 要求，并将诊断定位到该 modifier。value availability
只能追加要求，不能降低 variant availability。

若一种 modifier 形式的部分 allowed values 在较晚版本才加入，应把 variant 的
`availability` 设为所有 value 共有的最低要求，再把新增要求写到对应 value 上。例如
`add.u32` 与 `add.u16x2` 同属 no-sat variant，后者单独要求 PTX 8.0 / sm_90；不得仅因
该版本差异复制一个 `add_simd_no_sat_sm90` variant。


历史浮点控制省略 variant 可声明 `floating_point_default: approximate_flush_to_zero`、
`approximate` 或 `round_nearest_even`。此 closed typed interpretation 独立于 source
modifier；所有声明的 `rounding`、`approx`、`full` 与 `ftz` slot 必须为
`presence: absent`。第一种解释要求 fixed `type: f32`，后两种要求 fixed `type: f64`。
未知值、冲突的 explicit control/type 均拒绝。normalization 与 Resolved IR 保留此 fact；
生成的 exact class 暴露 static `FloatingPointDefault floating_point_default`，不合成
source modifier field。见[历史 contract](resolved_ir_design.md#历史浮点控制省略形式)。

## Operand 与 operand pattern

当前接受条件与限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--operands-and-operand-patterns)；API、算法及证据见 [英文技术参考](../us-en/yaml_instruction_spec.md#operands-and-operand-patterns)。

## Operand layout

`operands` 是单一 layout 的简写；normalizer 会把它转为名为 `default` 的 layout。若
同一 modifier variant 有多个合法 operand 形态，使用显式 `operand_layouts`：

```yaml
operand_layouts:
  - name: barrier
    operands: $bar_sync_barrier
  - name: barrier_and_thread_count
    operands: $bar_sync_barrier_and_thread_count
```

layout name 是稳定语义 ID，不是 C++ layout directive。它按声明顺序确定
`ResolvedOperandLayoutTag` 的索引，改名或重排需视为 generated ABI 变化。当前只支持
`Flat` positional layout；同一 variant 的 layout 必须能由 operand 数量/shape 唯一匹配。

不要为 operand 形态不同而发明 variant。例如 `bar.sync a` 和 `bar.sync a, b` 应在一个
`.sync` variant 内使用两个 layout；而 `bar.sync` 与 `bar.cta.sync` 的 modifier 组合
不同，应使用两个 variant。

## Immediate operand constraint

variant-level `constraints` 可以对具名 operand 声明可执行的整数规则。根据
constraint kind，它可以是 `kind: imm` 或 `kind: reg_or_imm`；`reg_or_imm` rule
仅在 source 是已知 immediate 时生效：

```yaml
constraints:
  - {kind: immediate_value, operand: mode, values: [4, 8, 16]}
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

每个 variant 最多一个 `immediate_value`，它是非空且无重复的 allowlist。每个 operand
最多一个 `immediate_range`；`minimum` 为 inclusive，下界可选的 `maximum` 也为
inclusive，省略 `maximum` 表示没有上界。每个 variant 最多一个
`immediate_multiple_of` divisor rule。和 `immediate_range` 一样，它可指向 `imm`
或 `reg_or_imm` operand：生成的 checker 会检查已知 immediate，而把动态未知的
register 值留给 runtime。当具名 operand 的语义合理时，这些 descriptor 可以组合使用。

所有配置值使用生成代码的 `uint64_t` 域：YAML 中实际的整数必须落在
`0..18446744073709551615`（`2^64 - 1`）。负数、Boolean、float 或其他非整数，以及
超出该上限的值都会被拒绝。normalizer 会在去重前验证每个
`immediate_value.values[index]`，并以同一规则验证 `minimum`、出现时的 `maximum` 和
`divisor`；还会拒绝 `maximum < minimum`，并要求 `divisor > 0`。

operand 引用刻意是 variant-wide，而不是 layout-local。它必须至少出现在一个 operand
layout。对全部三类 constraint，省略该 operand 表示 constraint 不适用于该 layout，且不
会导致 missing-field error。operand 出现时，`immediate_value` 要求 `kind: imm`，而
`immediate_range` 与 `immediate_multiple_of` 接受 `kind: imm` 或 `kind: reg_or_imm`。
`reg` occurrence 会导致 normalization error，并指明 variant、constraint kind、operand 与
layout。这样既允许 optional operand，又在值 contract 要求时保留 immediate-only rule。

`setmaxnreg.inc.sync.aligned.u32` 与 `setmaxnreg.dec.sync.aligned.u32`
使用相同的 range 和 divisibility rule：

```yaml
operands:
  - {name: count, kind: imm, role: src, access: read, type: u32}
constraints:
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

因此两种 action 下 `192` 都合法，`23`、`257` 和 `25` 都会被拒绝。`bfe.u32` 与 `bfi.b32` 都使用两个
独立的 inclusive range：`offset` 和 `width` 均为 `0..255`；两个 operand 在各自唯一的
layout 中都是 immediate operand。

resolver 中，integer immediate 携带 scalar type、use-width bits、求值后的 64-bit source bits，
以及数值上的 signed-negative 性质。source 在没有 `U`/`u` 且不超过 `INT64_MAX` 时为 signed；
unary minus 保留该 type，而 unsigned value 按该宽度回绕。range、multiple-of 与 exact-value
都要求 source 为 nonnegative，并比较原始 source bits；因此非零 source 即使窄化为允许的
target-width bit pattern，也不能满足 fixed control rule。signed `-0` 因而等同 zero。
floating immediate 解析为 IEEE raw bits：decimal form
要求 `f32` 或 `f64`，`0f...`/`0d...` 分别是 unsigned 32-/64-bit bit-pattern literal，
必须恰好使用 `f32`/`f64`，且不能带符号。因此这些 constraint 是 integer-domain rule；不要
用看似数值的 bounds 表达 floating-point ordering。

normalization diagnostics 会给出 variant、constraint kind、精确 field（包括
`values[index]`）以及非法 value。runtime 中 value/range/divisibility 不满足会产生定位到
immediate operand 的 `ImmediateValueMismatch`；若生成后的 descriptor 不可能地引用了缺失或
非-immediate operand，则产生定位到整条 instruction 的 `RuleViolation`。这样可以区分
configuration error 与 source-program error。

## 完整示例：bar

```yaml
operand_patterns:
  bar_sync_immediate_barrier:
    - {name: barrier, kind: imm, role: barrier,
       access: read, type: u32, immediate_conversion: require_target_range}
  bar_sync_barrier:
    - {name: barrier, kind: reg_or_imm, role: barrier,
       access: read, type: u32, immediate_conversion: require_target_range}

instructions:
  - opcode: bar
    syntax: "bar{.cta}.sync barrier{, thread_count}"
    variants:
      - name: bar_sync
        availability: {ptx: "1.0", sm: 10}
        modifiers:
          - {name: cta, kind: flag, presence: absent, token: ".cta"}
          - {name: sync, kind: flag, presence: fixed, value: true,
             token: ".sync"}
        operand_layouts:
          - {name: immediate_barrier, operands: $bar_sync_immediate_barrier}
          - {name: barrier, operands: $bar_sync_barrier,
             availability: {ptx: "2.0", sm: 20}}
          - {name: barrier_and_thread_count,
             operands: $bar_sync_barrier_and_thread_count,
             availability: {ptx: "2.0", sm: 20}}
        rule: parallel_sync_and_communication.bar_sync
```

`bar_cta_sync` 使用 fixed `.cta`，并以 PTX 7.8 为 availability。layout 可以另带
可选 `availability`；缺省时不额外增加要求，等价于只继承 variant 的可用性。checker 会
同时检查 variant 与已选 layout 的约束。因此这里的 immediate layout 可在 PTX 1.0 / sm_10
使用，而 register 或 thread-count layout 额外要求 PTX 2.0 / sm_20。

## Availability、rule 与 example

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--availability-rules-and-examples)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#availability-rules-and-examples)。

## Add 的当前覆盖

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--current-add-coverage)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#current-add-coverage)。

## 编写原则

1. 记录 PTX 语义事实，而不是生成实现偏好；禁止 `direct`、`sub_struct`、
   `sub_variant` 之类的 C++ storage 选项。
2. 对 modifier variant 使用稳定且描述性的 name，例如 `add_sat`、`bar_cta_sync`；名称
   不应包含仅由 allowed-value availability 表达的版本后缀。
3. 优先复用 `type_sets`、`value_sets` 与 `operand_patterns`，但不要把语义不同的 operand 强行放入
   同一 pattern。
4. 新增 spec 前确认 lexer/AST 能形成所需 operand shape；不能时先扩展语法层。
5. 新增 layout 必须补 resolver/checker 测试，尤其是 tag 范围与 tag/payload 不一致。
6. schema 通过不代表生成器支持；运行 Python tests、CMake build 与 CTest 验证。

## Whole-opcode acceptance

不能因为一个新 variant 能 parse、YAML database test 通过，或 generated model 有非零
variant count，就称 opcode complete。应针对相关 PTX 9.3 normative section 冻结所声称的范围，
包括相邻的 excluded form，并用 independently derived 的 positive/negative source case 覆盖其
modifier、operand、availability 与 diagnostic boundary。model/generator consistency check 只能说明
YAML 按预期 normalized 和 emitted，不是 independently derived 的 ISA-conformance evidence。

当 declaration type、binding identity、call/control metadata、function context 或 address
provenance 会影响 legality 时，必须通过带 source version 和 target context 的
declaration-aware module 测试。standalone instruction test 适合 local syntax/resolution，但不能替代
module check。应 mutate public Resolved IR，并运行 AST-free validation，检查 retained identity、
provenance 与 descriptor invariant。最后，用仅含 public header 的 installed-package consumer
编译并运行。这些 frontend check 不证明 simulator execution、dynamic protocol correctness、独立
probe 之外的 assembler acceptance，或 GPU/hardware behavior。

推荐验证命令：

```bash
PYTHONPATH=python python3 -m unittest discover -s python/tests -t python -p 'test_*.py' -v
cmake --build out/build/ci-linux-gcc-debug -j2
ctest --preset ci-linux-gcc-debug --output-on-failure
```

## Typed Video 契约

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--typed-video-contracts)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#typed-video-contracts)。

## SM 可用性的数值独占上界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--exclusive-numeric-sm-availability-bound)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#exclusive-numeric-sm-availability-bound)。

## 历史101 source profile 与验证 feature

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#yaml-instruction-spec--historical-101-source-profiles-and-validation-features)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/yaml_instruction_spec.md#historical-101-source-profiles-and-validation-features)。
