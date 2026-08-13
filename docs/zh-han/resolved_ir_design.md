# C++ Resolved IR 设计

## 状态与边界

本文描述当前实现的 Resolved PTX IR，而不是一个未来的 CFG、SSA 或后端 IR
设计。frontend 的核心数据流为：

```text
PTX source -> Token stream -> Syntax AST -> Resolved IR -> checker
```

Syntax AST 忠实保存源码拼写、modifier 顺序和 `SourceRange`；Resolved IR 则记录已经
选定的指令 variant、已解析的 operand 值与诊断位置。二者都属于 frontend 的稳定边界。
CFG、SSA、符号表的完整绑定和目标 lowering 是后续 pass，不应改变此层的结构。

## 位置与基本值

每个可独立诊断的 resolved 值使用：

```cpp
template <typename T>
struct WithLocs {
  T value;
  std::vector<SourceRange> locs;
};
```

`locs` 允许一个语义值关联多个源码片段；空集合表示没有直接源码位置，例如由 fixed
modifier 得到的编译期常量。当前基础值包括 `ResolvedRegisterRef`、`ResolvedImmediate`
与 `RegOrImm`。`ResolvedImmediate` 保存整数 bits 和 `ScalarType`，因此 checker 不必
重新解释 literal 文本。

在符号表与声明绑定完成之前，`ResolvedRegisterRef` 会拥有寄存器的完整源码拼写，并
同时保存 `ResolvedRegisterClass` 和 numbered-register index。index 只是便捷属性，不能
单独充当身份：例如 `%r1` 与 `%rd1` 的 index 都是 1，但它们的 spelling 不同。当前
resolver 支持 numbered-register 子集，并区分普通寄存器和 `%pN` predicate；未来接入
符号表后，应以 declaration `SymbolId` 补充或替换这层词法引用。

## 按 opcode 生成的结构

每个 opcode 生成一个外层 struct，并用 `VariantType` 和 `std::variant` 表示由
modifier 组合唯一确定的 variant：

```cpp
struct Add {
  enum class VariantType { IntegerNoSat, SatS32 /* ... */ };

  struct IntegerNoSat {
    ResolvedOperandLayoutTag operand_layout;
    WithLocs<ScalarType> type;
    WithLocs<ResolvedRegisterRef> dst;
    WithLocs<RegOrImm> src1;
    WithLocs<RegOrImm> src2;
  };

  using Variant = std::variant<IntegerNoSat /* ... */>;
  Variant variant;
};
```

fixed modifier 不作为每个 instruction instance 的可写状态保存。例如 `add.sat.s32`
的 variant 将生成：

```cpp
inline static constexpr bool saturate = true;
inline static constexpr ScalarType type = ScalarType::S32;
```

这既保存了已选语义，也避免后续 pass 对同一事实做重复判定。

## 一个 variant 内的多个 operand layout

modifier 组合相同但 operand 形态不同，不应人为拆成多个 modifier variant。此时生成
一个 layout tag 和嵌套 payload variant。`bar.sync a{, b}` 的形式为：

```cpp
struct Bar::Sync {
  ResolvedOperandLayoutTag operand_layout;
  inline static constexpr bool sync = true;

  struct BarrierOperands { WithLocs<RegOrImm> barrier; };
  struct BarrierAndThreadCountOperands {
    WithLocs<RegOrImm> barrier;
    WithLocs<RegOrImm> thread_count;
  };
  using Operands = std::variant<BarrierOperands,
                                BarrierAndThreadCountOperands>;
  Operands operands;
};
```

`ResolvedOperandLayoutTag` 是生成 descriptor 中 layout 的索引。checker 必须同时验证
tag 合法、tag 与 payload alternative 一致，以及 payload 的每个 operand binding。
tag/payload 不一致是损坏的 resolved IR，诊断种类为
`OperandLayoutPayloadMismatch`。

当前唯一实现的 layout algorithm 是 `Flat`：逗号分隔的、位置固定的 operand slots。
`Group`、可变参数、call 参数组等需要先扩展 Syntax AST，再增加新的 layout kind；不能
把它们伪装成 `Flat`。

## Resolution 协议

`resolve<T>(const AstInstruction&)` 是生成的 opcode 专用薄封装，公共逻辑依次执行：

1. `collect_actual_modifiers` 将 modifier spelling 映射为 descriptor `kind_id`，诊断
   未知 spelling 与重复 kind。
2. `selectVariant<T>` 只依据 modifier 槽位选择唯一 variant。`absent`、`optional`、
   `required/fixed` 都按 kind 和允许值匹配，而非按源码中的 modifier 下标匹配。
3. 在选定 variant 内按 AST operand shape 与 arity 选择唯一 `OperandLayout`。
4. `resolve_fields` 按 resolved descriptor 把 modifier 和 operand 转换为带位置的
   resolved 值。
5. 生成的 builder 将字段放入对应 C++ struct 或 layout payload。

零个匹配 variant/layout 是用户诊断；多个匹配 layout 或 descriptor 与生成结构无法
对应是生成器/descriptor bug，使用 `ResolveException` 区分于 `ResolveDiagnostic`。

## 三份 descriptor

同一 YAML spec 生成三份职责不同的静态 descriptor：

| Descriptor | 用途 |
| --- | --- |
| Syntax descriptor | modifier spellings、presence、AST operand shape 与 layout slots |
| Resolved descriptor | resolved field kind、modifier binding、operand binding、结构化类型表达式与语义 role/access |
| Checker descriptor | variant/layout 的 PTX/SM/family availability 与 rule ID |

三者不互相复制职责。Syntax descriptor 不应保存 resolved C++ 类型；Resolved descriptor
不负责 modifier 拼写识别；Checker descriptor 不重新描述 resolve binding。

## Checker 契约

`checker::check<T>` 是每个 opcode 的生成 wrapper，公共 checker 至少检查：

- variant、已选 operand layout 与实际 modifier value 的最低 PTX 版本、SM 版本与 target family；
- layout tag 的范围；
- layout tag/payload 一致性；
- operand 字段 ID、resolved shape 与由结构化 descriptor 约束的 immediate 类型。

`rule_id` 留给指令特有规则的 typed wrapper。寄存器声明类型、符号可见性、地址空间
和跨 instruction 约束依赖完整 symbol table，尚不属于当前公共 checker ABI。

## 扩展规则

- YAML 的 semantic variant 由 modifier 组合定义；不得因生成方便而增加假 variant。
- 每个 generated member 必须是一个 resolved PTX fact 或其位置，不生成 `direct`、
  `sub_struct` 等 C++ 后端布局开关。
- 新 operand shape 应先加入 Syntax AST 与 syntax descriptor，再加入 resolver 与
  checker 的对应 resolved value。
- 新的多 layout 指令必须测试正常 resolution、非法 layout、以及 tag/payload 不一致。

实现入口见 `include/ptx_ir/resolved/ptx_resolved_ir.hpp`、
`include/ptx_ir/ptx_resolved_ir_checker.hpp` 与生成的 `resolved_ir.gen.hpp`。
