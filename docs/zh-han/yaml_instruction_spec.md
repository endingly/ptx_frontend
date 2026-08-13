# YAML Instruction Spec：结构与原则

## 目的

`instructions/ptx_spec/` 中的 YAML 是 PTX 指令事实的声明来源。它描述合法的源码
形式、variant、operand layout、availability 和规则标识；Python generator 从中同时
生成 Syntax descriptor、Resolved descriptor、checker descriptor 和 C++ instruction
结构。它不是 C++ 代码模板，也不是 backend layout 配置。

每个文件使用 `instructions/schemas/ptx-instr-v1.schema.yaml`：

```yaml
schema: ptx-instr/v1
ptx_isa: "9.2"
category: integer_arithmetic
section: "9.7.1"
```

schema 负责字段形状和基础枚举；normalizer 负责跨字段的生成器不变量，例如一个
variant 不能同时写 `operands` 和 `operand_layouts`。

## 预定义数据引用

本 DSL 中 `$name` 是对当前 YAML 文件预定义数据的唯一引用写法，无需引号。`type_sets`
在 `values` 中通过 `$name` 引用，`operand_patterns` 在 `operands` 中也通过 `$name` 引用；
裸字符串不再表示引用。normalizer 在生成前递归展开引用、检测循环，并保证引用与原地写出
相同数据得到完全相同的 `InstructionSpec`。`$` 不用于 type expression；后者有独立的函数
语法，避免与预定义数据引用混淆。

## 顶层结构

常用顶层字段如下：

| 字段 | 含义 |
| --- | --- |
| `type_sets` | 可由 `$name` 引用的 modifier value 集合 |
| `operand_patterns` | 可由 name 复用的 operand 列表 |
| `instructions` | 一个或多个 opcode 定义 |

每个 instruction 至少包含 `opcode` 和 `variants`，可以有 instruction-level 的
`syntax`、`operands`、`category`、`section` 与 `doc`。同一 spec database 中 opcode
必须全局唯一。

## Variant 与 modifier

variant 的 `name` 是稳定 machine-readable identifier；它生成 C++ variant 名与
descriptor key。当前模型中，一个 variant 由确切的 modifier kind/value 组合确定，
而不是由 operand 数量确定。

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
modifier kind 一致：`flag` 使用布尔值；`type` 使用 `values` 中的 scalar type。
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
```

省略 modifier 时，resolver 将 default 写入 resolved field，并令其 `locs` 为空；
显式 modifier 会覆盖 default，并保存其源码位置。`absent`、`required` 与 `fixed`
不得书写 `default`。default 仍是有效语义值，因此其 value availability 仍会被 checker
检查；因为没有 modifier 源码位置，相关诊断回退到整条 instruction 的 range。

`flag` 通常给出 `token: ".sat"`；`type` 的 token 通常从 `value` 或 `values` 推导。
modifier matching 使用 `kind_id`，不依赖源码 modifier 顺序。不同 variant 的 modifier
组合必须互斥，否则 selector 会报告歧义。

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

## Operand 与 operand pattern

operand 由稳定 `name`、syntax `kind`、语义 `role`、`access` 与可选 `type` 定义：

```yaml
- name: src1
  kind: reg_or_imm
  role: src1
  access: read
  type: {expr: modifier(type)}
```

目前完整生成/resolve 支持的是 `reg`、`imm`、`reg_or_imm`、`pred` 与
`pred_or_not`；`pred_or_not` 接受 `%pN` 或 `!%pN`，并在 resolved IR 保留取反标记。
schema 仍可以描述更广的 PTX operand kinds。新增 schema enum 并不等于已支持：必须
同步扩展 Python Syntax/Resolved model、C++ resolver 和 checker。`type` 可以引用
modifier（`modifier(type)`），也可以是固定 scalar type（例如 `u32`）。目前唯一支持的
type-expression 函数是 `modifier(name)`：它读取当前 variant 的 active `kind: type` modifier。
schema 仍保留 `same_as(...)`、`one_of(...)` 和 `same_size_as(...)` 作为未来语法，但
normalizer 会明确报错表示尚未支持。

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
  sm: 90
  family: sm_90a       # 可选
rule: integer_arith.add # 可选但建议提供
```

checker 公共逻辑解释最低 PTX、SM、family；`rule` 是稳定 rule ID，供 instruction-specific
checker 使用。`examples`、`doc` 和 `description` 记录规范意图，不能替代可执行的 C++/
Python 测试。

`operand_layouts` 中的 `availability` 与 variant availability 是累积关系，而不是覆盖关系：
只有选中的 layout 会增加自己的 PTX、SM 或 family 要求。若多个 layout 都能匹配同一语法，
resolver 只会选择唯一的、语法 shape 严格更具体的 layout；相同或不可比较的候选是 YAML
建模错误，不能借 availability 消除歧义。

## 编写原则

1. 记录 PTX 语义事实，而不是生成实现偏好；禁止 `direct`、`sub_struct`、
   `sub_variant` 之类的 C++ storage 选项。
2. 对 modifier variant 使用稳定且描述性的 name，例如 `add_sat_s32`、`bar_cta_sync`。
3. 优先复用 `type_sets` 与 `operand_patterns`，但不要把语义不同的 operand 强行放入
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
