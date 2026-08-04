# PTX Frontend IR 设计

## 状态

本文定义 PTX frontend 的目标 IR 架构，并取代当前“parser 直接产出以
`ParsedOp` 为参数的生成指令结构”的设计。

该架构只有两个一等表示：

```text
PTX 源码
  -> Syntax AST
  -> Resolved PTX IR
```

控制流图、SSA 和目标相关 lowering 都是可选的派生表示，不属于 parser 的核心
IR 契约。

## 目标

- 保留诊断、格式化和错误恢复所需的源码形式与 source range。
- 让 parsing 独立于符号解析、目标选择和指令语义 form 选择。
- 将每个已确定语义的 PTX instruction form 表示为简单的生成 C++ struct。
- 在 resolved 表示中以显式 ID 替代未经检查的字符串标识符。
- 保持 YAML 的声明式语义：它描述 PTX 事实，而不是 C++ 存储布局选择。

## 1. Syntax AST

Syntax AST 是手写、通用且忠实于源码的表示。它不为每个 opcode 生成一个 C++
类型。parser 只判断 token 序列是否构成有效的 PTX 语法结构，不能决定最终选择
哪个语义 instruction form。

一条 instruction 的概念结构如下：

```cpp
struct AstInstruction {
  AstOpcode opcode;
  std::vector<AstModifier> modifiers;
  std::vector<AstOperand> operands;
  std::optional<AstPredicate> predicate;
  SourceRange range;
};
```

`AstOpcode`、`AstModifier` 和每个 `AstOperand` 分支均保留原始拼写与
`SourceRange`。`AstOperand` 是语法层联合类型，例如：

```cpp
using AstOperand = std::variant<
    AstIdentifierRef,
    AstIdentifierRef,
    AstImmediate,
    AstAddress,
    AstVectorMember,
    AstVectorPack>;
```

此阶段的 `%r1`、`foo`、`target` 都只是文本引用；特别是 `%r1` 与 `foo` 都是
`AstIdentifierRef`。parser 不推断它们是 register、variable、function 还是 label；
`AstAddress`、`AstImmediate` 与 vector 相关分支只记录 grammar shape。同样地，
modifier 按源码中的顺序记录，而不是被编码成某个生成指令 variant。

声明、directive、label、function 和 module 也采用相同的源码保真风格。只有 AST
需要保存每个 token 的位置。

## 2. Resolved PTX IR

语义分析器接收完整的 Syntax AST、symbol table、instruction database 与可选的
PTX target。它在检查完整 modifier 和 operand 序列后，才选择 instruction form。

输出为 Resolved PTX IR，其中不再存在未解析的标识符字符串：

- register 使用 `RegisterId`；
- variable、function、parameter 使用 `SymbolId`；
- branch destination 使用 `LabelId` 或 `BlockId`；
- operand 带有已解析的 address space、type 和 immediate 信息。

Resolved IR 保留用于诊断的 source provenance。每条 statement 均有 instruction 级
origin；可独立触发诊断的字段可以使用 `WithOrigin<T>`，记录一个 primary range 和
若干 related range。包装的值是 resolved semantic data，而不是旧混合模型中的
`WithLoc<ParsedOp>`。

每个语义 PTX instruction form 都是一个扁平的生成 struct。例如：

```cpp
struct AddU32 {
  RegisterId dst;
  RegOrImmediate src1;
  RegOrImmediate src2;
};

struct AddSatS32 {
  RegisterId dst;
  RegOrImmediate src1;
  RegOrImmediate src2;
};

using ResolvedInstruction = std::variant<AddU32, AddSatS32>;
```

两个 form 即使字段完全一致，也可以刻意使用不同 struct。这样能保留已选择的 PTX
语义，后续 pass 不必再从 modifier 字段重新推导该决定。

## 解析（resolution）的职责

resolution 负责：

1. 解析名称并构造 ID。
2. 检查 modifier 名称、重复出现和声明的 slot 顺序。
3. 结合 modifier 与完整 operand 形态，在同 opcode 的候选 form 中匹配。
4. 检查 operand 类别、type、state space、arity 与指令特有约束。
5. 检查 PTX version、SM version、target family、deprecated 和 removed 状态。
6. 产出 `ResolvedInstruction`，或产出关联到 Syntax AST range 的诊断。

因此，当多个 form 具有相同 opcode/modifier 而 operand layout 不同时，应由此层
消除歧义，而不是由 parser 过早报错。

## 生成 C++ IR 的规则

生成器只生成简单的数据声明：

- 每个 resolved instruction form 一个 `struct`；
- 每个 resolved 字段一个 member；
- category 和全局的 `std::variant` alias；
- resolution 所需的 instruction descriptor。

生成的 IR 不能再使用 `direct`、`sub_struct`、`sub_variant` 等 backend 布局策略。
这些策略描述的是 C++ 存储机制，而不是 PTX 语义；它们必须从 backend schema、
normalization model 和 template 中删除。

现有通用的 `PtxInstruction<Operand>` / `ParsedOp` 模型不再是新的 IR 边界。它将
未解析的文本 operand 与已选中的语义指令形态混合，而 label、predicate 和声明仍在
operand 模板参数之外保存原始标识符。

## YAML 模型

YAML 仍然是生成 resolved form 的来源，但需具有两个清晰的语义区：

- `syntax`：opcode 拼写、modifier slot、operand grammar category、可选性和源码
  层 form；
- `forms`：语义约束、availability、resolved field 定义和 operand resolution 规则。

概念示例：

```yaml
instructions:
  add:
    syntax:
      modifiers: [sat, type]
      operands: [dst, src1, src2]
    forms:
      - name: AddSatS32
        requires:
          sat: true
          type: s32
        availability: { ptx: "1.0" }
        fields:
          - { name: dst, type: RegisterId }
          - { name: src1, type: RegOrImmediate }
          - { name: src2, type: RegOrImmediate }
```

schema 必须验证该模型，且不应将尚不支持的 backend hook 或 C++ 布局选项伪装成
PTX capability。

## 可选的下游表示

需要做 control-flow analysis、optimization、interpretation 或 translation 的客户，
可以从 Resolved PTX IR 构建 CFG，并按需构建 SSA。它们是职责明确的独立 pass，
不会改变 Syntax AST 或 resolved instruction-form 的契约。

## 迁移计划

1. 引入 Syntax AST 类型，并让 parser API 返回 AST module/instruction。
2. 引入 ID 与 resolved operand 类型，并以一个手写的 pilot resolved form 实现
   resolver。
3. 以扁平 `forms` 替换 generator backend schema，并生成 resolved instruction
   struct 与 descriptor。
4. 按 instruction category 逐步迁移；每个已迁移 form 都要有 AST parser test 与
   resolver test。
5. 删除 `ParsedOp`、`PtxInstruction<Operand>`、`emit.kind` 以及仅用于旧混合模型的
   template。
