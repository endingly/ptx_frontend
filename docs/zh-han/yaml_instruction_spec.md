# YAML Instruction Spec：结构与原则

## 目的

`instructions/ptx_spec/` 中的 YAML 是 PTX 指令事实的声明来源。它描述合法的源码
形式、variant、operand layout、availability 和规则标识；Python generator 从中同时
生成 Syntax descriptor、Resolved descriptor、checker descriptor 和 C++ instruction
结构。它不是 C++ 代码模板，也不是 backend layout 配置。

每个文件使用 `instructions/schemas/ptx-instr-v1.schema.yaml`：

```yaml
schema: ptx-instr/v1
ptx_isa: "9.3"
category: arithmetic
codegen_category: arithmetic
```

schema 负责字段形状和基础枚举；normalizer 负责跨字段的生成器不变量，例如一个
variant 不能同时写 `operands` 和 `operand_layouts`。

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
C++ variant 名、同一 variant 内一个 spelling 归属多个活动 modifier slot，以及可接受
同一无序 modifier 集合的重叠 variant。

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
mixed Add 中绑定 `result_type`；但在单个 variant 内必须唯一归属一个活动 slot。
modifier matching 不依赖源码顺序。不同 variant 接受的无序 modifier 集合必须互斥，
否则 database 会在生成前拒绝。

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

## Operand 与 operand pattern

operand 由稳定 `name`、syntax `kind`、语义 `role`、`access` 与可选 `type` 定义：

```yaml
- name: src1
  kind: reg_or_imm
  role: src1
  access: read
  type: {expr: modifier(type)}
```

目前完整生成/resolve 支持的是 `reg`、`imm`、`reg_or_imm`、`pred`、`pred_or_not`、
`addr` 与 `reg_vector`；`pred_or_not` 接受 `%pN` 或 `!%pN`，并在 resolved IR 保留取反标记。
schema 仍可以描述更广的 PTX operand kinds。新增 schema enum 并不等于已支持：必须
同步扩展 Python Syntax/Resolved model、C++ resolver 和 checker。`type` 可以引用
modifier（`modifier(type)`），也可以是固定 scalar type（例如 `u32`）。目前唯一支持的
type-expression 函数是 `modifier(name)`：它读取当前 variant 的 active `kind: type` modifier。
schema 仍保留 `same_as(...)`、`one_of(...)` 和 `same_size_as(...)` 作为未来语法，但
normalizer 会明确报错表示尚未支持。

address operand 也可以从 active `kind: state_space` modifier 派生所要求的
state space：

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space: {expr: modifier(state_space)}
```

normalizer 会保留该引用，resolved descriptor 保存对应 modifier field ID。checker 仅在
address 具有 declaration-derived effective state space 时比较；register、immediate 与
standalone address 仍为 unknown，不根据 spelling 推断。
当前 scalar/vector `ld/st` explicit form 为每个 opcode 使用单一 runtime modifier field，不为每个
state-space value 复制 variant。

explicit `.param` address 可增加窄化的方向与 function-context constraint：

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space: {expr: modifier(state_space)}
  parameter:
    direction: input
    function_availability: {ptx: "2.0", sm: 20}
```

`parameter` 只能用于 `kind: addr`，并且必须伴随 state-space modifier expression；该 active
modifier 必须允许 `param`（或 fixed 为 `param`），normalizer 会拒绝脱离 `.param`、因而不会
生效的 constraint。resolved descriptor 保存 typed input/return direction 与 availability。
runtime 选择 `.param` 后，公共 checker 优先拒绝已知的错误 parameter direction；否则在
constraint 期望 return 或 address 已知属于 device function 时应用 function availability。
因此当前 load constraint 允许 entry input parameter 使用 explicit-form baseline，但在
device function 中要求 PTX 2.0 / SM 20；store 的 return constraint 在所有 context 都要求
该 target。未知 identity 不猜测 direction；已知非 `.param` symbol 只由原有 exact
state-space mismatch 处理。

scalar string 或 list 可定义静态 effective-address allowlist。list item 可以是普通
state-space string，也可以是带 `value`/`availability` 的 object：

```yaml
- name: address
  kind: addr
  role: addr
  access: read
  state_space:
    - value: const
      availability: {ptx: "3.1"}
    - global
    - local
    - shared
```

scalar form 等价于一个没有额外 target requirement 的单项 list。静态 value 与
`expr: modifier(...)` 互斥。resolved descriptor 将每个静态 value 映射到
`MemoryStateSpace` 并保留 availability；公共 checker 拒绝 list 之外的已知 effective
space，并检查命中 entry 的 availability。register、immediate 与 standalone address 的
unknown space 仍接受。当前 generic scalar/vector load 使用上例 policy，generic scalar/vector store 仅允许
`.global/.local/.shared`。

当前 scalar/vector `ld/st` variant 复用一个包含 `.b8/.b16/.b32/.b64`、
`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64` 与 `.f32` 的 type set，再由各 variant 追加 `.f64`。
legacy load 额外建模 `.ca/.cg/.cs/.lu/.cv`，legacy store 额外建模 `.wb/.cg/.cs/.wt`；
显式 cache spelling 统一附加 PTX 2.0 / SM 20 availability，而省略时解析为不可拼写的
`unspecified` sentinel。PTX 的实际硬件默认语义仍遵循 ISA：省略时 `ld` 等效 `.ca`、`st`
等效 `.wb`；但 Resolved IR 有意保留 `Unspecified`，以区分 source provenance 与
modifier-value availability。explicit `.f64` 附加 SM 13 availability；generic `.f64`
不复制该门槛，因为 generic variant 已要求 SM 20。data operand 使用
`type: {expr: modifier(type)}`，由 runtime modifier 及其位置驱动公共
fundamental-type 检查。register operand 还可以选择显式 width policy：
legacy memory-vector payload 最多 128 bit：`.v2` 到 64-bit type，`.v4` 到
32-bit type；生成的 `memory_vector` constraint 另只允许 PTX 8.8/SM 100 的
256-bit `.v8` × 32-bit 与 `.v4` × 64-bit，且地址已知时必须 global。

```yaml
- name: dst
  kind: reg
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
```

`register_width` 默认为 `same_width`。normalizer 会拒绝在非 register operand 或没有 type
expression 的 operand 上使用非默认 `equal_or_wider`，避免 constraint 静默失效；`reg_vector`
operand 也可使用该 policy，并逐元素检查。resolved operand descriptor 保存该 policy，不生成
runtime Resolved IR field。当前 scalar `ld` destination、scalar `st` source，以及 legacy
`.v2/.v4` memory vector element 使用 `equal_or_wider`：声明 register size 必须大于等于
instruction size；通过 size 检查后，任一侧 bit type 与 signed/unsigned integer pair 兼容，
float 要求 exact type/size，integer/float 不兼容。immediate 与 special-register check 仍为
same-width。wider actual register 当前只覆盖到 64-bit；在 declaration type 的 target availability
得到检查前，`.b128` 仍明确拒绝。scalar `.b128` instruction type 仍不属于当前范围。

`reg_vector` operand 必须用 `vector.arity` 声明合法元素数。静态形式使用整数或列表：

```yaml
vector: {arity: [2, 4], type_policy: aggregate, allow_sink: true}
```
memory vector 的 generated cross rule 只允许精确 256-bit modern payload 使用部分 sink；
legacy vector 与 all-sink form 仍拒绝。

legacy memory vector 则把 arity 链接到 required runtime vector modifier：

```yaml
- name: dst
  kind: reg_vector
  role: dst
  access: write
  type: {expr: modifier(type)}
  register_width: equal_or_wider
  vector: {arity: {expr: modifier(vector)}, type_policy: element}
```

`type_policy: aggregate` 按整条 instruction 的 bit width 检查 vector payload，用于
`mov` pack/unpack；`type_policy: element` 逐元素按 instruction type 检查，用于 legacy
memory vector。该 memory vector 保留最多 128-bit 的 legacy form（`.v2` 到 64-bit type，
`.v4` 到 32-bit type），并加入上述 PTX 8.8 的 256-bit form。`VectorArity` 是 required modifier domain，不支持
optional/default 形式。

应使用语义 role（如 `dst`、`src1`、`barrier`、`thread_count`）而不是为了复用字段
随意命名 `srcN`；role 和 access 进入 resolved descriptor，供 checker 和后续规则使用。

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

variant-level `constraints` 可以对具名 `kind: imm` operand 声明可执行的整数规则：

```yaml
constraints:
  - {kind: immediate_value, operand: mode, values: [4, 8, 16]}
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

每个 variant 最多一个 `immediate_value`，它是非空且无重复的 allowlist。每个 operand
最多一个 `immediate_range`；`minimum` 为 inclusive，下界可选的 `maximum` 也为
inclusive，省略 `maximum` 表示没有上界。每个 variant 最多一个
`immediate_multiple_of` divisor rule。当具名 operand 的语义合理时，这些 descriptor
可以组合使用。

所有配置值使用生成代码的 `uint64_t` 域：YAML 中实际的整数必须落在
`0..18446744073709551615`（`2^64 - 1`）。负数、Boolean、float 或其他非整数，以及
超出该上限的值都会被拒绝。normalizer 会在去重前验证每个
`immediate_value.values[index]`，并以同一规则验证 `minimum`、出现时的 `maximum` 和
`divisor`；还会拒绝 `maximum < minimum`，并要求 `divisor > 0`。

operand 引用刻意是 variant-wide，而不是 layout-local。对三种 constraint kind 中的每一种，
该具名 operand 都必须存在于此 variant 的**每一个** operand layout，且在每个 layout 中
都必须是 `kind: imm`。哪怕只有一个具名 layout 缺少该 operand，或写成
`reg`/`reg_or_imm`，normalization 也会报错，并指明 variant、constraint kind、operand 与
layout。不得用 layout-local constraint DSL 或“runtime 缺 operand 就跳过”的规则绕过它。
未来 instruction 若确实需要 layout-specific rule，必须新增经过明确设计的 contract；不能
放宽这个不变量。

当前冻结的 `setmaxnreg.inc.sync.aligned.u32` form 展示了 range 与 divisibility rule 的组合：

```yaml
operands:
  - {name: count, kind: imm, role: src, access: read, type: u32}
constraints:
  - {kind: immediate_range, operand: count, minimum: 24, maximum: 256}
  - {kind: immediate_multiple_of, operand: count, divisor: 8}
```

因此 `192` 合法，`23`、`257` 和 `25` 会被拒绝。`bfe.u32` 与 `bfi.b32` 都使用两个
独立的 inclusive range：`offset` 和 `width` 均为 `0..255`；两个 operand 在各自唯一的
layout 中都是 immediate operand。

resolver 中，integer immediate 携带 scalar type、受 operand width 限制的 raw bits，以及
源端 signed marker。例如 signed `-1` 的 raw bits 是该 operand width 下的 two's-complement，
同时保留 `is_negative`；checker rule 运行前不会把它转换成抽象 signed integer。range 和
multiple-of 会先拒绝 negative marker，再比较或取余。exact-value 则有意比较 raw bits，
所以 allowlist 是 bit-value contract。floating immediate 解析为 IEEE raw bits：decimal form
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
       access: read, type: u32}
  bar_sync_barrier:
    - {name: barrier, kind: reg_or_imm, role: barrier,
       access: read, type: u32}

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

每个 variant 必须声明：

```yaml
availability:
  ptx: "8.0"
  sm: 100
  family: sm_100f      # 可选的最低 family-specific 源特性 target
rule: integer_arith.add # 可选但建议提供
```

checker 公共逻辑解释最低 PTX、SM 与 `family` 要求。`family` 是最低
family-specific 源特性 target：checker 只在 source target profile 的
`enabled_family_features` 中查找。显式 catalog 为：`sm_100` → 无；
`sm_100f`/`sm_100a` → `sm_100f`；`sm_103` → 无；`sm_103f`/`sm_103a` →
`sm_100f`、`sm_103f`；`sm_120f` → 仅 `sm_120f`。不得由 SM 数字或 target 后缀
推断此集合。它不同于 PTX 到物理 GPU 的 translation compatibility；后者当前不建模。
`a` target 是 exact identity，不能作 family spelling；需要精确 target 时使用
`any_of: [{target: sm_100a}]`。capability clause、exact target 与 `family` 均是相互独立的
constraint。`rule` 是稳定 rule ID，供 instruction-specific checker 使用。
`examples`、`doc` 和 `description` 记录规范意图，不能替代可执行的 C++/
Python 测试。

`any_of` 的一个 clause 也可以包含 `family`；它是该 clause 内的 AND-term，沿用同一
`enabled_family_features` 查找，不要求该 exact target spelling。

`operand_layouts` 中的 `availability` 与 variant availability 是累积关系，而不是覆盖关系：
只有选中的 layout 会增加自己的 PTX、SM 或 family 要求。若多个 layout 都能匹配同一语法，
resolver 只会选择唯一的、语法 shape 严格更具体的 layout；相同或不可比较的候选是 YAML
建模错误，不能借 availability 消除歧义。

`kind: reg_vector` 的 operand 会生成 `ResolvedRegisterVector` payload。静态
`vector.arity` 只作为 descriptor check；动态
`vector.arity: {expr: modifier(vector)}` 还会记录到所选 runtime modifier field 的链接。该信息
不影响 modifier variant 选择。例如 `mov` 的 scalar、pack 与 unpack 是同一 modifier variant
的三种 layout，不能因 `.b16/.b32/.b64` 形式重叠而复制 variant；`ld.v2` 与 `ld.v4` 则是由
required runtime `vector` modifier 选择的同一个 vector variant。

## Add 的当前覆盖

`arithmetic.yaml` 中按节拆分的 `add` 定义会自动合并。当前覆盖标准
`.f32/.f32x2/.f64`、mixed-precision `.f32.{f16,bf16}` 以及半精度
`.f16/.f16x2/.bf16/.bf16x2` 形式；舍入模式解析为 `RoundingMode`，`.rm/.rp.f32`
的 `sm_20` 要求使用 value availability 表达。packed 与 half/bfloat 形式按 PTX 规范
只接受 register operand。

mixed variant 使用 `result_type` 与 `input_type` 两个具名 slot：前者 fixed 为 `f32`，
后者接受 `f16/bf16`。operand 的结构化 type expression 分别引用这两个 slot，因此
resolver 与 checker 无需从 modifier 的位置或字符串重新推断 operand type。该 variant
要求 PTX 8.6 / sm_100，并允许可选舍入与 `.sat`。

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

推荐验证命令：

```bash
PYTHONPATH=python python3 -m unittest discover -s python/tests -t python -p 'test_*.py' -v
cmake --build out/build/ci-linux-gcc-debug -j2
ctest --preset ci-linux-gcc-debug --output-on-failure
```
