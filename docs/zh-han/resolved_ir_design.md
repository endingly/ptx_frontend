# Resolved PTX IR 设计

## 状态

本文定义 PTX Syntax AST 解析与解析（resolution）之后产出的目标语义表示。它是本
frontend 的第二层、也是最后一层核心 IR。

```text
PTX 源码
  -> 无损 token stream
  -> Syntax AST
  -> Resolved PTX IR
```

## 范围与目标

Resolved PTX IR 表示已经完成 name binding、instruction form 选择、operand 验证与
target 验证后的程序。它必须：

- 在语义字段中不包含未解析的文本引用；
- 保留最终选定的 PTX instruction form；
- 只表示 well-formed instruction instance；
- 保留 instruction 级和字段级、可关联诊断的 provenance；
- 便于从 YAML 生成 C++ struct 与 member；
- 保持接近 PTX，不应过早转换为 CFG 或 SSA IR。

CFG、SSA、optimization、interpretation 与 target lowering 均为下游 pass，不属于
此表示的职责。

## Module 与 statement 结构

resolved module 保留 PTX 的线性源码结构。label 保持显式存在，branch operand 在
初始阶段引用 `LabelId`；后续 CFG pass 可将其替换或补充为 `BlockId` edge。

```cpp
struct ResolvedStatement {
  std::optional<ResolvedPredicate> predicate;
  ResolvedInstruction instruction;
  SourceOrigin origin;
};

struct ResolvedFunction {
  FunctionId id;
  std::vector<ResolvedStatement> body;
};
```

predicate 属于 `ResolvedStatement`，而不是每个 instruction 类型的成员。这样避免
在每个生成 instruction struct 中重复 PTX statement 层属性。

## Source provenance

checker 工作在 Resolved IR 上，且必须产出源码诊断，因此 Resolved IR 必须保留与
Syntax AST 的来源关联。一个语义值可能来自一个或多个 AST node，单个 `SourceRange`
并不总是足够。

```cpp
struct SourceOrigin {
  SourceRange primary;
  std::vector<SourceRange> related;
};

template <typename T>
struct WithOrigin {
  T value;
  SourceOrigin origin;
};
```

`primary` 是默认需要标注的位置；`related` 保存共同决定该语义值的其他源码片段。
例如 `add.sat.u8x4` 的 resolved form 可将 `.u8x4` 作为 `primary`，并将 `.sat`
保存为 `related`。

每个 resolved statement 都保存 statement 级 provenance；可独立触发诊断的 operand
与 form/modifier field 同时使用 `WithOrigin<T>`。这不是要求复用旧的
`WithLoc<ParsedOp>` 模型：包装的值是 `RegisterId`、`ResolvedImmediate` 等 resolved
semantic value，而不是 AST value。

## Identity 与 resolved reference

resolver 在合适的所有权范围内分配 opaque ID：

```cpp
struct RegisterId { uint32_t value; };
struct SymbolId   { uint32_t value; };
struct LabelId    { uint32_t value; };
struct FunctionId { uint32_t value; };
```

具体实现可使用 strong typedef、index class 或紧凑整数包装，但不同 ID 类别不能隐式
互相转换。

resolver 根据上下文将 AST 中的文本引用映射为 ID。因此原本的 `AstIdentifierRef`
可以解析为 register、variable、function、label 或其他 symbol，而不要求 Syntax AST
提前作出该语义决定。

## Resolved operand

resolved operand 保存语义 identity 与 value，而不是源码拼写。其具体分支会随 PTX
覆盖范围扩展，但初始模型可概念化为：

```cpp
struct ResolvedImmediate {
  ImmediateBits bits;
  ScalarType type;
};

using ResolvedValue = std::variant<RegisterId, ResolvedImmediate>;

struct ResolvedAddress {
  std::variant<RegisterId, SymbolId> base;
  int64_t offset;
  StateSpace space;
};

struct ResolvedVectorMember {
  RegisterId base;
  uint8_t member;
};
```

immediate 表示必须保留 PTX 语义所需的信息，包括 bit pattern 与 type，不能依赖 AST
中的 literal spelling。address space、vector width 与 operand category 均在
resolution 阶段验证，而非交由后续 consumer 推断。

可独立诊断的 operand 应同时保存 provenance，例如 `WithOrigin<RegisterId>`、
`WithOrigin<ResolvedValue>`。

## Instruction 与 form

`ResolvedInstruction` 是按 opcode 组织的生成全局 variant：

```cpp
using ResolvedInstruction = std::variant<Add, Atom, Ld, St, Bra /* ... */>;
```

每个 opcode struct 以能够保留语义区别的最小方式记录所选 PTX form。

### 只有一种 operand layout 的 form

当一个 opcode 的全部 form 具有相同的 resolved operand layout 时，使用 form tag 与
共享 operand struct：

```cpp
struct Add {
  enum class Form { U32, SatS32, U16x2, SatU8x4 };

  struct Operands {
    WithOrigin<RegisterId> dst;
    WithOrigin<RegOrImmediate> src1;
    WithOrigin<RegOrImmediate> src2;
  };

  Form form;
  Operands operands;
};
```

`Form` 唯一决定 `.sat`、`.s32` 等 fixed modifier，不应把这些事实冗余地保存为相互
独立的字段。

### operand layout 不同的 form

当合法 form 在结构上不同，使用内部语义 variant；instruction 的共享数据保留在其
外部：

```cpp
struct Atom {
  struct Common {
    StateSpace space;
  };

  struct Basic {
    WithOrigin<RegisterId> dst;
    WithOrigin<ResolvedAddress> addr;
    WithOrigin<ResolvedValue> value;
  };

  struct CompareAndSwap {
    RegisterId dst;
    ResolvedAddress addr;
    ResolvedValue compare;
    ResolvedValue replacement;
  };

  Common common;
  std::variant<Basic, CompareAndSwap> operands;
};
```

该内部 variant 属于语义模型：每个 alternative 对应不同 PTX operand layout；它不是
任意的 code-generation layout option。

## 生成 C++ 模型

YAML 生成以下 Resolved IR 产物：

- opcode struct；
- 语义模型需要时的嵌套 common/operand struct；
- form enum 与内部 form variant；
- category 与全局 `ResolvedInstruction` variant；
- resolver 使用的静态 descriptor。

每个生成 member 都必须对应一个 resolved PTX fact。生成器不能再将 `direct`、
`sub_struct`、`sub_variant` 等通用布局模式暴露为用户可选的 backend mechanism。
schema 描述 form 结构，template 选择必需的 C++ 语法。

form name 由稳定、machine-readable 的 YAML form identifier 生成，而不是手写 API
义务。可读的生成名称有价值，但生成过程必须 deterministic，并检查 collision。

## Resolver 契约

resolver 接收 `syntax_ast::AstInstruction`、symbol table 与可选 PTX target，并负责：

1. 将 identifier reference 解析为带类别的 ID；
2. 结合 opcode、modifier sequence 与完整 AST operand shape 匹配候选；
3. 选择唯一合法的 semantic form；
4. 验证 type、state space、arity 与 instruction-specific constraint；
5. 验证 PTX/SM/family availability、deprecation 与 removal；
6. 产出带 provenance 的生成 opcode struct，或产出关联到 AST range 的诊断。

具有相同 opcode/modifier、但 operand layout 不同的候选，应在此层完成消歧。

## modifier variant 与 operand layout 的两阶段选择

当前 resolver 将单条 instruction 的选择拆成两个彼此独立的阶段：

```text
AstInstruction
  -> 根据 opcode 与 modifier 选择唯一 Variant
  -> 在该 Variant 内根据 AST operand 形状选择唯一 OperandLayout
  -> 解析 operand 并构造对应的 Resolved IR payload
```

这里的 `Variant` 表示 modifier 组合确定的语义 form；`OperandLayout` 表示同一个
variant 内 operands 的数量、位置、可选性、分组与语法形状。前者不能根据 operand
选择，后者也不能重新改变已选定的 variant。

### modifier 规范化与 variant 选择

Syntax AST 保留 modifier 的源码顺序和拼写。selector 首先将每个 modifier 映射到由
instruction descriptor 定义的 `kind_id`，构造概念上的槽位表：

```text
add.sat.s32 ...

"saturate" -> ".sat"
"type"     -> ".s32"
```

构造该表时应诊断未知 modifier 与同一 `kind_id` 的重复 modifier。随后，对每一个
候选 variant 按 `kind_id` 而非 AST 列表下标检查其 `ModifierDescriptor`：

- `Absent`：该 kind 不得出现；
- `Optional`：可不出现；出现时必须属于允许值；
- `Required`：必须出现，且必须属于允许值。

所有 modifier kind 由 descriptor 显式声明的 variant 才能匹配。零个候选表示用户
PTX 不合法；多个候选表示生成的 ISA descriptor 重叠，必须报告歧义，不能按声明
顺序任取一个。

### OperandLayout descriptor

`OperandLayoutKind` 是全 PTX 共享的、手写的匹配策略 enum，不由生成器为每条指令
生成。生成器只生成某个 layout 的 slots 及其语义约束。初始阶段只需要扁平的逗号
分隔 operand layout：

```cpp
enum class OperandLayoutKind : uint8_t {
  Flat,
};

enum class OperandPresence : uint8_t {
  Required,
  Optional,
};

struct OperandSlotDescriptor {
  std::string field_id;
  OperandSyntaxShape allowed_syntax_shapes;
  OperandPresence presence;

  OperandRole role;
  OperandAccess access;
  OperandShape allowed_resolved_shapes;
  StateSpace allowed_state_spaces;
};

struct OperandLayoutDescriptor {
  OperandLayoutKind kind;
  std::vector<OperandSlotDescriptor> slots;
};
```

`allowed_syntax_shapes` 用于 layout selection：它约束 AST 节点是 identifier、immediate、
address、vector 或未来的 group。其余字段用于已选择 layout 后的 semantic resolution
与 checker。`field_id` 是生成器和 builder 使用的稳定字段标识，如 `dst`、`src1`、
`barrier_id`。

`OperandSyntaxShape` 必须能表达 AST 的每种重要形状；特别是 `AstVectorMember` 不应
混同于普通 identifier。未来支持 `call` 的 `(...)` 参数组时，应先在 Syntax AST 中
增加 `AstOperandGroup`，再增加 `Group` shape 和相应的 layout kind。

一个 variant 可以有多个 `operand_layouts`。对于 `bar.sync a {, b}`，一个 `Flat`
layout 加 optional 尾随 slot 已足够；对于 `call` 的 direct、indirect、带返回值或参数
组等真正不同结构，可在同一个 modifier variant 下放置多个 layout。layout selector
必须要求唯一命中。

### 实现顺序

1. 完成 modifier 的 `kind_id -> actual modifier` 规范化表，并按 presence/value 选择
   唯一 variant。
2. 将 `OperandLayoutKind` 定义为 `check_end` 的公共 enum，`VariantDescriptor` 使用
   `operand_layouts` 保存 layout descriptor。
3. 实现 `AstOperand -> OperandSyntaxShape` 分类函数及 shape 位运算。
4. 恢复 `Add` 的有效 descriptor：为其整数 variant 描述 required `type` modifier，
   并提供一个包含 `dst`、`src1`、`src2` 的 `Flat` layout。
5. 实现 `select_operand_layout(variant, ast.operands)`；它检查 arity、optional slot 与
   AST shape，并要求唯一命中。
6. 在通用 resolver 中依次执行 variant 选择、layout 选择与按 slot 解析；最终将已
   解析字段交给生成的 typed builder 构造 Resolved IR。

## YAML 要求

instruction spec 必须提供稳定 form ID，以及生成 resolved field 和 resolver
descriptor 所需的信息：

```yaml
forms:
  - id: sat_s32
    requires: { sat: true, type: s32 }
    availability: { ptx: "1.0" }
    operands:
      - { name: dst, resolved_type: RegisterId }
      - { name: src1, resolved_type: RegOrImmediate }
      - { name: src2, resolved_type: RegOrImmediate }
```

normalized Python model 必须区分：

- opcode 的 shared field；
- 具有相同 operand layout 的 form tag；
- 结构不同的 form payload。

## 实现顺序

1. 定义核心 ID、resolved operand、statement 与 source-origin policy。
2. 手写 `Add` resolved instruction 与 resolver pilot。
3. 扩展 YAML/schema/normalization：加入稳定 form ID 与 resolved field descriptor。
4. 生成一个 instruction category 及其 resolver descriptor。
5. 逐 category 迁移，并为合法 form、operand-layout ambiguity、symbol 与 target
   availability 添加 resolver test。
