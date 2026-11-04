# C++ Resolved IR 设计

当前支持范围、语法与操作接受条件、类型和目标限制以英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design) 为唯一维护依据；本文保留技术参考与历史证据。

## 向量数据搬运集成

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--integrated-vector-data-movement)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#integrated-vector-data-movement)。

## 显式 vector MOV

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--explicit-vector-mov)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#explicit-vector-mov)。

## 具名数组地址

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--named-array-addresses)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#named-array-addresses)。

## 指令常量表达式求值与转换

数值源语法及转换边界见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--locations-and-primitive-values)；算法与数据结构见 [英文技术参考](../us-en/resolved_ir_design.md#locations-and-primitive-values)。

## 当前验证边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--current-validation-limits)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#current-validation-limits)。

## 状态与边界

当前接受条件与限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--status-and-scope)；API、算法及证据见 [英文技术参考](../us-en/resolved_ir_design.md#status-and-scope)。

## Named memory-vector source

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--named-memory-vector-sources)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#named-memory-vector-sources)。

## 位置与基本值

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--locations-and-primitive-values)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#locations-and-primitive-values)。

## 如何阅读生成的 C++ 代码

先跟踪一条指令，例如 `mov.u32 %r0, %r1;`。它的 **opcode** 是 `mov`，
选中的**语义形式（semantic form）**是 `Scalar`，最终用 `MovScalar` 类表示。
当前 MOV 的另外三个形式是 `MovB128PackUnpack`、`MovV4U32` 和 `MovPred`。
一个形式内部还可以有多个**操作数布局（operand layout）**，例如 `MovScalar`
支持 scalar 和 pack/unpack 排列，由选中的布局决定哪些操作数字段有值。
`mov.u32` 和 `mov.f32` 都可以是 `MovScalar`，类型差异保存在动态 `type` 字段中，
不是每一种 modifier spelling 都生成一个类。

阅读时，先把三种东西分开：最终 IR 对象拥有这一条指令的值与源码位置；静态数组
是所有指令共享的只读规则与索引；选择结果和字段 scratch 只在一次 resolve 中暂存
中间结果。下文的调用流程是 C++ 的解析、转换和检查流程，不是在执行 PTX 指令。

你最终保留的是 final 类对象。`MovScalar` 拥有 resolved `type`、选中布局的操作数值
及其 `WithLocs` 位置，也保留继承的执行谓词等状态。生成的 `instruction_kind()`
返回精确形式 identity，`clone()` 复制拥有的数据，`check()` 通过 checker descriptor
检查对象，`visit_references()` 在调用期间把引用借给 observer。`kind`、`opcode`
及形式特有的 `static constexpr` 常量属于共享事实，不会为每条指令复制一份元数据。
opcode identity、opcode 内的 form ordinal、公开 `InstructionKind` 和 form 内的
`operand_layout` tag 属于不同编号域。例如 Scalar 的局部 ordinal 是 0，
`MovScalar::kind` 是其公开形式 identity；layout tag 为 0 则表示该形式的第一个
layout，不能用来表示 MOV 的第 0 个形式。

### 先找到一个 opcode 的文件

下表用 `G` 表示 `<build>/submod/resolved_ir/generated`，用 `P` 表示
`G/public/ptx_frontend/resolved_ir`。这些是构建产物；修改时应追溯生成器或其输入，
不要直接改生成文件。

| `G` 或 `P` 下的文件 | 阅读目的 |
| --- | --- |
| `P/identity/data_movement/mov.gen.hpp` | MOV opcode/form 的强类型 identity 常量，不包含具体类。 |
| `P/ptx_instruction_base.gen.hpp` | 共享的生成 `Instruction` 基类接口及 identity 声明。手写 `ptx_instruction_base.hpp` 是它的 include wrapper。 |
| `P/model/data_movement/mov.gen.hpp` | 公开 final 类的成员和操作、descriptor getter、MOV resolver 声明。大 family 还会生成 `*_forms_NNN.gen.hpp` 声明分片。 |
| `P/ptx_resolved_ir.gen.hpp` | 包含各 opcode model 头的总入口。 |
| `P/ptx_instruction_catalogue.gen.hpp` | 包含具名 `Opcode`/`InstructionKind` enumerator 的总 identity catalogue，不包含全部具体形式类。 |
| `G/private/resolved_ir_data_movement_mov.gen.cpp` | MOV descriptor storage、方法定义、私有选择表和 `resolveMov`。下文主要沿这个文件阅读。 |
| `G/private/resolved_ir_matrix_wgmma_{descriptors,methods}_NNN.gen.cpp` | 大 family 的 descriptor/method 分片；WGMMA 构造 helper 也在 method 分片中。 |
| `G/private/resolved_ir_dispatch.gen.cpp` | 全局 opcode 分发和 `resolveInstruction` 入口。 |

生成文件会使用共享的手写类型和算法。`WithLocs`、resolved 值载体、`ResolvedModule`
来自手写 foundation/module 层；descriptor 类型，以及 `ModifierCandidateRow`、
`ModifierCandidatePlan`、`select_indexed_variant`、`resolve_selected_fields` 也都是
手写定义。生成器为它们提供具体的 storage、数组和调用。`Instruction` 本身是由
生成器发射的共享基类，不能因为它和手写类型一起出现，就把所有支撑类型都归为
生成类型。辅助 domain 查询代码见相关 family 文档，跟踪一条 MOV 暂不需要展开它们。

### 把 storage 看作共享元数据

MOV 源文件开头附近的三个 storage struct 分别组织三类事实：

| 生成的 storage 与 getter | 回答的问题 |
| --- | --- |
| `MovDescriptorStorage` → `mov_syntax_descriptor()` | 哪些 modifier slot/spelling、操作数形状和 layout 可以表达这个形式？ |
| `MovResolvedDescriptorStorage` → `mov_resolved_descriptor()` | modifier/operand 经哪些 binding 填入哪些 resolved field，值种类是什么？ |
| `MovCheckerDescriptorStorage` → `mov_checker_descriptor()` | 选中对象还需要满足哪些 availability、类型兼容性及其他合法性约束？ |

其中 `variants` 按 canonical form 顺序排列；modifier、layout、binding、constraint
数组描述各形式的规则。descriptor 中的 `std::span` 借用静态生命周期的数组，getter
返回稳定的 catalogue 引用。这些数组描述整个 family 的指令，不保存当前这一条指令
的值。运行时读取的是已经编译的 C++ 元数据，不是在运行 YAML 解释器。更详细的边界
见后面的[三份 descriptor](#三份-descriptor)。

同一个生成的 resolved storage block 内，完整相等的 operand-binding 序列共享一个
静态数组，以及配套的 vector-arity、register-type、address-space 数组。生成器先收集
这些组并统一输出存储，再输出各 form/layout 独立的 fields 和引用；共享只在该 block
内生效，分片时也不会跨 shard。各 form/layout 的 identity、字段和 checker/诊断关联
仍独立，binding 数组指针相同不代表语义形式相同。

### 读懂私有选择表

opcode 源文件后面的 selection block 用于先排除不可能的形式，再做精确匹配。
`SelectedFormOrdinal` 给当前 opcode 的零起始 canonical 位置命名；`selected_kinds`
把每个位置映射到对应 final 类的强类型 `kind`。旁边的 `static_assert` 校验这些
identity 与生成上下文一致，公开 kind 不是由局部 ordinal 强转或算术推导出来的。

`candidate_rows` 按 `spelling` 排序。每行的 `known` 只表示旧的全 opcode spelling
检查是否认识它，包含 canonical `Absent` slot 提到的 spelling；它**不表示**该形式
或该源码位置接受这个 spelling。候选成员关系保存在 `candidate_words` 中：如果有
`F` 个形式，每行占连续的 `W = ceil(F / 64)` 个 64-bit word。第 `r` 行从 `r * W`
开始，形式 `j` 对应 word `r * W + j / 64` 的 bit `j % 64`。只要该形式的 canonical
或 alias 中某个非 absent slot 能消费这个 spelling，该 bit 就会置位。这个并集
故意忽略书写顺序、出现次数及 required slot 是否齐全。`candidate_plan` 是这两个
静态数组的 view，不是另一份拥有数据的表。

当前 MOV 快照的 ordinal 为 `0 = Scalar`、`1 = B128PackUnpack`、`2 = V4U32`、
`3 = Pred`，因此 `W = 1`。其中几行可以合起来看：

| Spelling | `known` | Mask，最右侧为 bit 0 | 剩余的形式候选 |
| --- | --- | --- | --- |
| `.u32` | `true` | `0x5` = `0101` | Scalar、V4U32 |
| `.v4` | `true` | `0x4` = `0100` | V4U32 |
| `.pred` | `true` | `0x8` = `1000` | Pred |
| `.b128` | `true` | `0x2` = `0010` | B128PackUnpack |

为方便阅读，上表把 row 和 mask 放在一起；实际 C++ 分为 rows 和 words 两个数组。
`.v4.u32` 的交集是 `0x4 & 0x5 = 0x4`，只表示剩下一个候选，不表示合法性检查完成。
`.u32.v4` 得到相同交集，也不会因此合法：原有 exact matcher 仍检查书写顺序、
required/optional/absent slot、重复及声明的 order alias。全部 known-spelling 检查
先于匹配完成；失败或歧义时回退到完整的通用 selector，保留原诊断及源码范围。
没有 modifier 时，初始候选是全部形式。多个精确 modifier match 仍按原来的 arity、
再 shape 顺序消歧；只有一个 modifier match 时，即使操作数有错，也交给字段解析
继续诊断。

WGMMA 只是同一结构的规模扩大：当前快照有 2,151 个形式、166 行 spelling，
`W = 34`，共 5,644 个 word。零起始的第 9 行是 `.fence`，mask 中包含 canonical
form bit 2,148。这些数字只是当前快照示例，会随 spec 改变，不能当作 ABI 依赖。
物理分片不会让全 opcode ordinal 重新从零编号。WGMMA getter 按 canonical 顺序
组装静态连续 catalogue rows；选择表和薄 resolver 仍在
`resolved_ir_matrix_wgmma.gen.cpp` 中，最后构造时才调用 method 分片中的对应
`wgmma_construct_NNN` helper。

### 沿一条指令跟踪运行流程

对开头的 scalar 示例，可以按下面的顺序读调用：

```text
mov.u32 %r0, %r1; 的 AST
  -> resolveInstruction -> 全局 resolve_instruction 按 opcode 分发
  -> resolveMov
  -> select_form
       栈上 candidates 数组 + 静态 candidate_plan 排除不可能的形式
       select_indexed_variant 调用原 exact matcher 和原消歧规则
       选择错误交回完整通用 selector
  -> SelectedForm { ordinal, kind, syntax 引用, resolved 引用 }
  -> resolve_selected_fields
       结合 ResolveContext（含 texture mode）选 layout
       再次绑定 winner 的 modifier，填充既有的字符串键 field maps
  -> 构造 MovScalar，把 resolved 值及位置填入成员
  -> 后续通过 check(...) 使用 checker
```

对这一条 `.u32` 源码，mask 先留下 Scalar 和 V4U32；精确匹配发现缺少 required
vector modifier（`.v2` 或 `.v4`），排除 V4U32，因此 `SelectedForm` 引用 Scalar。两个 identifier 选中其 `scalar`
layout（当前 layout tag 为 0）。字段解析把 `.u32` 转成 `ScalarType::U32`，沿
`dst`/`src` binding 解析 `%r0`/`%r1` 并保留位置。构造代码填充
`MovScalar::type`、`dst_register`、`src_mov_source`，仅供 pack/unpack 使用的成员
保持未设置。通过 `std::unique_ptr<Instruction>` 返回的是这个对象，选择表和
scratch map 都不是返回的 IR。

`select_form` 创建按 opcode 大小确定的栈上 `candidates` scratch 数组，返回
`SelectedForm`。其中 `ordinal` 是局部形式位置，`kind` 是公开强类型 identity，
`syntax` 和 `resolved` 引用该形式的静态 catalogue row。这个临时对象不携带最终
操作数 payload，不是返回给调用者的 IR。`resolveMov` 等 `resolve{Opcode}` 函数
负责把选择、共享字段解析和 final 类构造串起来。`resolve_selected_fields` 返回临时的
`ResolvedInstructionFields`，其中 modifier/operand 的 `ResolvedFieldMap` 保留既有
字符串键 scratch 表示，直到构造时把值填入最终对象的强类型成员。公开
`select_variant_name` 继续作为调用者自定义 descriptor 的通用兼容入口；生成的
resolver 显式传入私有 plan，才会使用索引路径。

`resolveInstruction` 本身不等于 target-aware module validation。声明绑定、模块级
义务及 checker 调用由本文其他部分说明的外围 module 流程负责。上面各步的详细
错误与 ownership 规则见后面的 [Resolution 协议](#resolution-协议)。

要追溯生成来源，先看
[`selection_index.py`](../../python/src/ptx_frontend/code_gen/selection_index.py)
如何导出 spelling mask，
[`render_models/resolved_source.py`](../../python/src/ptx_frontend/code_gen/render_models/resolved_source.py)
如何准备 typed selection 与 construction 事实，以及
[`templates/resolved/resolve.j2`](../../python/src/ptx_frontend/code_gen/templates/resolved/resolve.j2)
如何表达选择表 storage、`SelectedForm` 和 resolver 调用。
[`resolved_model.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_model.py)
发射公开 model/identity 接口，
[`resolved_dispatch.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_dispatch.py)
发射全局分发。共享手写实现则在
[`ptx_resolved_ir_modifiers.cpp`](../../submod/resolved_ir/src/ptx_resolved_ir_modifiers.cpp)、
[`ptx_resolved_ir_selection_private.hpp`](../../submod/resolved_ir/src/ptx_resolved_ir_selection_private.hpp)
及 [`ptx_resolved_ir.cpp`](../../submod/resolved_ir/src/ptx_resolved_ir.cpp)。

[`resolved_checks.py`](../../python/src/ptx_frontend/code_gen/emit/resolved_checks.py)
将 final-form checker 渲染适配到 typed check model 与固定 macro；纯 builder
负责有序的 layout/family/cross-rule 检查及与 resolver 准备共用的 named-array address policy。
`resolved_source` 组合嵌套 source model，由固定模板表达 resolver、构造、descriptor 与 reference visit。Checker view 直接接收最终类限定名及 payload member 表达式；
caller 在使用前保护 optional payload，诊断 field ID 仍来自 metadata。

## 按 opcode 生成的结构

每个语义形式生成一个继承 `Instruction` 的 final 类。每个 opcode 的窄生成头
包含该 opcode 的所有形式、descriptor 访问器和 resolver，例如：

```cpp
class AddIntegerNoSat final : public Instruction {
public:
  ResolvedOperandLayoutTag operand_layout;
  WithLocs<ScalarType> type;
  WithLocs<ResolvedRegisterRef> dst;
  WithLocs<RegOrImm> src1;
  WithLocs<RegOrImm> src2;
};
```

可选的 `execution_predicate` 与虚函数从 `Instruction` 继承。

fixed modifier 不作为每个 instruction instance 的可写状态保存。`AddSat`
中，`.sat` 固定，而 type 是带独立 availability 的 allowed value，因此生成：

```cpp
inline static constexpr bool saturate = true;
WithLocs<ScalarType> type;
```

这既避免后续 pass 重复判定固定事实，也保留了实际 type 及其源码位置。

一个 variant 可以有多个同 kind 的具名 modifier slot。mixed-precision Add 例如生成
`static constexpr result_type = F32` 与动态的 `WithLocs<ScalarType> input_type`；三个
operand 的类型表达式分别引用 `result_type`、`input_type`、`result_type`。slot ID 是
variant-local 的，因此 `.f32` 在普通 Add 中可以绑定 `type`，在 mixed Add 中绑定
`result_type`，不会退化为全局字符串到 kind 的映射。

## 一个 variant 内的多个 operand layout

modifier 组合相同但 operand 形态不同，不应人为拆成多个语义形式。final 类
保留一个 layout tag；所有 layout 都存在的字段直接存放，仅部分 layout 存在的
字段使用强类型 optional。`bar.sync a{, b}` 的形式近似为：

```cpp
class BarSync final : public Instruction {
public:
  ResolvedOperandLayoutTag operand_layout;
  inline static constexpr bool sync = true;
  std::optional<WithLocs<ResolvedImmediate>> barrier_immediate;
  std::optional<WithLocs<RegOrImm>> barrier_reg_or_imm;
  std::optional<WithLocs<RegOrImm>> thread_count;
};
```

`ResolvedOperandLayoutTag` 是生成 descriptor 中 layout 的索引。checker 在解引用
layout 专有字段前验证 tag 合法性、必需和禁止 optional 的存在性，以及每个 operand
binding。同一个 descriptor 字段 ID 如有不同 C++ 值类型，成员名添加确定性的
value-kind 后缀，例如 `barrier_immediate` 和 `barrier_reg_or_imm`。不一致是损坏的
resolved IR，诊断种类为
`OperandLayoutPayloadMismatch`。

`Flat` 用于逗号分隔、位置固定的 operand slot。唯一新增的 layout algorithm 是 `Call`：它识别
三种 direct-call group 排列，并把 input group 解析为一个 variadic field。它是固定 algorithm，
不是通用 repeat DSL；可变参数与 call group 仍不能伪装成 `Flat`。

## Resolution 协议

逐 opcode resolver 及其带 `ResolveContext` 的重载共享生成实现，公共逻辑依次执行：

1. 公共 matcher 先用全部 syntax descriptor 诊断真正未知的 spelling，再分别在每个
   候选 variant 内把 spelling 绑定到有序 slot。required/fixed slot 可以通过位置
   消除共享 spelling 的歧义；database 会拒绝涉及 optional slot 的重复 spelling。
   重复占用一个 slot 会被诊断。
2. Form selection 只依据上述 form-local 绑定选择唯一语义形式。`absent`、
   `optional`、`required/fixed` 都按 slot、允许值和规范或显式别名顺序匹配。
   顺序别名不产生新的语义 variant，也不改变 field 绑定。
3. 在选定 variant 内按 AST operand shape 与 arity 选择唯一 `OperandLayout`。
4. `resolve_fields` 解析公共 execution predicate，并按 resolved descriptor 把 modifier 和
   operand 转换为带位置的 resolved 值；有 binding context 时，guard 必须绑定到 `.pred`
   register，普通寄存器必须解析到当前 lexical scope 的 `.reg` declaration，两者都会写入
   `SymbolId` 与声明类型，direct branch target 必须绑定到当前 function 的 label。
5. 生成的 builder 构造精确 final 类并填充直接字段与 optional 字段。

零个匹配 variant/layout 是用户诊断；多个匹配 layout 或 descriptor 与生成结构无法
对应是生成器/descriptor bug，使用 `ResolveException` 区分于 `ResolveDiagnostic`。

生成的 opcode resolver 使用私有的有序 spelling 索引对 form bitset 做保守交集，
随后按 canonical form 顺序调用既有的精确有序 binder。已知 spelling 仍包含 canonical
`absent` slot；alias 仅扩充可消费的候选。所有未知 spelling 检查先于绑定完成；选择失败
回退到通用 `select_variant_name`，保留诊断及其优先级。成功结果保留 opcode 内的强类型
ordinal、由 GenerationContext 校验的 `InstructionKind` 和静态 syntax/resolved descriptor
直接引用。`resolve_fields` 仍重新绑定 winner，并结合 resolution context 选择 layout。
索引表与薄调用保留在各 opcode 原有的 `.gen.cpp` 中，WGMMA 构造分片保持独立。
通用 `select_variant_name` 继续支持调用者自定义 descriptor。
按 YAML
`codegen_category` 生成的稳定逐 opcode 聚合头包含 final 类定义及 resolver 声明；
只需 model 的调用者可使用生成的 base 头。
`ptx_resolved_ir.gen.hpp` 包含全部逐 opcode 聚合头。形式数量超过 64 的 opcode
按规范顺序生成 public `*_forms_NNN.gen.hpp` 声明分片及 private
`*_methods_NNN.gen.cpp`、`*_descriptors_NNN.gen.cpp` 分片，每片最多 64 个形式。
逐 opcode 头仍是公共 include 入口。descriptor getter 保持 `const&`/`noexcept`
契约：有界且只执行一次的 function-local `static const std::array` 按规范顺序拼接
分片行，提供无 heap allocation、生命周期稳定的连续存储。非 inline 方法定义编入库。
内部固定引用域 observer 不是公开的全家族 visitor 模板；在既有分片中增加形式也不
承诺其他代码完全无需重新编译。

对于 WGMMA，稳定的逐 opcode resolver 仍按既有顺序执行选择和字段解析，再按精确
kind 分发到所属方法分片中的私有构造 helper。helper 借用临时字段并返回相同的精确
final 类；公开类与 resolver 签名均不变。

## 三份 descriptor

同一 YAML spec 生成三份职责不同的静态 descriptor：

| Descriptor | 用途 |
| --- | --- |
| Syntax descriptor | modifier spellings、presence、AST operand shape 与 layout slots |
| Resolved descriptor | resolved field kind、modifier binding、operand binding、结构化 type/state-space 表达式、带逐项 availability 的静态 state-space allowlist 与语义 role/access |
| Checker descriptor | variant/layout/value 的 availability、operand type compatibility 与 rule ID |

三者不互相复制职责。Syntax descriptor 不应保存 resolved C++ 类型；Resolved descriptor
不负责 modifier 拼写识别；Checker descriptor 不重新描述 resolve binding。

normalized specification model 会在加载边界把 YAML 的 modifier kind/presence 以及
operand kind/role/access spelling 转成语义 enum。Resolved IR validation 只使用这些 enum
和 PTX constraint，不加载 C++ domain mapping。C++ type name、enum expression 与 member
alias 只在 code generation 时应用。例如语义 IR 中的 `.sat` field 保持 `sat`，backend
仍可输出历史 C++ member spelling `saturate`。Checker 的 operand view 也按
`ResolvedValueKind` 与 field origin dispatch，因此 backend type-name 的改变不能选择不同
的语义 branch。

## Checker 契约

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--checker-contract)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#checker-contract)。

## 扩展规则

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--extension-rules)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#extension-rules)。

## Owned Video 操作数

`ResolvedVideoOperand` 拥有带位置的 `RegOrImm`、可选带位置的 `VideoSelector` variant、书写的寄存器取负 Boolean 及可选 minus range。Identifier 位置与外层 selector/negation range 分离，用于绑定诊断。Source 数组保留书写的高到低 digit 顺序，索引连接后的 A+B carrier。`video_default_selector` 和 `video_effective_selector` 提供默认值，不虚构书写来源。生成 form 公开静态 `video_lanes`/`video_operation`。`video_mad_interpretation` 独立于书写 dtype 推导 product/input-C/final 符号；负数常量不变成寄存器取负控制。公开 helper 位于 `<ptx_frontend/resolved_ir/ptx_video.hpp>`。Checker operand view 仅在同步检查期间借用 owned Video payload；模块引用遍历使用内部 carrier 的 identifier 位置。


## 转换指令的 special-register source

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--conversion-special-register-sources)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#conversion-special-register-sources)。

## 历史浮点控制省略形式

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--historical-omitted-floating-controls)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#historical-omitted-floating-controls)。

### 拥有式 debug section 值

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--owning-debug-section-values)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#owning-debug-section-values)。

### Target debug 缺失检查与 recognition 边界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--target-debug-absence-and-recognition-boundary)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#target-debug-absence-and-recognition-boundary)。

## 历史 entry body input

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--historical-entry-body-inputs)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#historical-entry-body-inputs)。

## SM 可用性的数值独占上界

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--exclusive-numeric-sm-availability-bound)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#exclusive-numeric-sm-availability-bound)。

## 历史101 source profile 与验证 feature

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--historical-101-source-profiles-and-validation-features)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#historical-101-source-profiles-and-validation-features)。

### 已弃用的最大 CTA 提示

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--deprecated-maximum-cta-hint)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#deprecated-maximum-cta-hint)。

### 历史调试序列化

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--legacy-debug-serialization)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#legacy-debug-serialization)。

### 间接函数地址 carrier

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--indirect-function-address-carriers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#indirect-function-address-carriers)。

### call 的 selected scalar operand

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--selected-scalar-call-operands)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#selected-scalar-call-operands)。

### 备用标量字面量转换

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--alternate-scalar-literal-conversion)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#alternate-scalar-literal-conversion)。

### Packed32 exact word 与 native call formal

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--packed32-exact-words-and-native-call-formals)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#packed32-exact-words-and-native-call-formals)。

### 所引用 debug function-name 字节

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--referenced-debug-function-name-bytes)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#referenced-debug-function-name-bytes)。

### 浮点 value-vector source 契约

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--floating-value-vector-source-contract)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#floating-value-vector-source-contract)。

## 显式标量 Bit16 来源

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--explicit-scalar-bit16-sources)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#explicit-scalar-bit16-sources)。

## Packed SETP raw 与 exact word

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--packed-setp-raw-and-exact-words)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#packed-setp-raw-and-exact-words)。

### 命名矩阵片段

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--named-matrix-fragments)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#named-matrix-fragments)。

### Shared matrix descriptor carrier 中的 scalar component

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--scalar-components-in-shared-matrix-descriptor-carriers)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#scalar-components-in-shared-matrix-descriptor-carriers)。

### TCGEN 必需的 named output-lane mask

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--tcgen-mandatory-named-output-lane-masks)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#tcgen-mandatory-named-output-lane-masks)。

### 普通 BF16 FMA/SET 源成员

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--ordinary-bf16-fmaset-source-members)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#ordinary-bf16-fmaset-source-members)。

### 标量 F16 atomic 源成员

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--scalar-f16-atomic-source-members)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#scalar-f16-atomic-source-members)。

### 标量 BF16 atomic 源成员

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--scalar-bf16-atomic-source-members)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#scalar-bf16-atomic-source-members)。

## Packed32 atomic source members

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--packed32-atomic-source-members)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#packed32-atomic-source-members)。

### 标量 packed32 源成员

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--scalar-packed32-source-members)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#scalar-packed32-source-members)。

### Atomic vector value 源

本节的当前支持范围、接受条件及保留限制见英文 [SDK 支持契约](../sdk_support.md#resolved-ir-design--atomic-vector-value-sources)；保留的实现、API 或证据说明见 [英文技术参考](../us-en/resolved_ir_design.md#atomic-vector-value-sources)。
