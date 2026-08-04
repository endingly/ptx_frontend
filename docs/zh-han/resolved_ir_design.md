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
