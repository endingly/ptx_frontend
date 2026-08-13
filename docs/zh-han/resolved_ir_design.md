# C++ Resolved IR 设计

## 状态与边界

本文描述当前实现的 Resolved PTX IR，而不是一个未来的 CFG、SSA 或后端 IR
设计。frontend 的核心数据流为：

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST 忠实保存源码拼写、modifier 顺序和 `SourceRange`；Resolved IR 则记录已经
选定的指令 variant、已解析的 operand 值与诊断位置。二者都属于 frontend 的稳定边界。
lexical symbol binding 与 module resolution 已接通；完整的 special/external symbol
分类、地址语义、CFG、SSA 和目标 lowering 仍是后续 pass，不应改变此层的结构。

生成的公共层还提供了一个与具体 opcode 无关的边界：

```cpp
using ResolvedInstruction = std::variant<Add, Sub, Bar /* ... */>;

std::expected<ResolvedInstruction, ResolveDiagnostic>
resolveInstruction(const syntax_ast::AstInstruction& ast);

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveModule(const syntax_ast::AstModule& ast);
```

`resolveInstruction` 根据指令数据库生成，并分发到现有的 `resolve<T>` 特化。调用者不再
需要手写 opcode 分派，同时每个 opcode 仍保留强类型结构。`resolveModule` 先建立
`SymbolTable`，再为每个 function scope 构造显式 `ResolveContext`；返回的
`ResolvedModule` 拥有 symbol table，`ResolvedFunction` 以函数 `SymbolId` 标识。
standalone `resolveInstruction` 与 `resolve<T>` 不要求声明上下文，继续服务单指令工具。
directive、declaration 与 label 目前仍由 Syntax AST/symbol table 保存，不复制成未解析的
Resolved IR 字符串字段。

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
modifier 得到的编译期常量，或由 optional modifier 的 YAML `default` 注入的实例值。
后者仍保存在 `WithLocs<T>` 中：`value` 是语义默认值，空 `locs` 表示源码没有显式写出。
当前 modifier 基础值包括 `bool`、`ScalarType` 与 `RoundingMode`；后者使
`.rn/.rz/.rm/.rp` 成为可静态检查的语义值，而不是运行时字符串。operand 基础值包括
`ResolvedRegisterRef`、`ResolvedImmediate` 与 `RegOrImm`。
`ResolvedImmediate` 保存整数 bits 和 `ScalarType`，因此 checker 不必
重新解释 literal 文本。

`AstImmediateKind` 保留 lexer 对 literal 的分类。整数 decimal/hex（包括可选 `U`
后缀）按目标整数或 bit type 的位宽做范围检查；负数以该目标宽度的二进制补码存入
`bits`，不会再无条件扩展为 64 位。decimal float 目前支持转换至 `F32` 与 `F64`；
`0f<8 hex>` 与 `0d<16 hex>` 分别作为 `F32` 与 `F64` 的原始 IEEE bit pattern。
其他浮点格式需要其明确的量化规则后再加入，不能静默按整数处理。

`ResolvedRegisterRef` 拥有完整源码拼写与 `ResolvedRegisterClass`。在 module resolution
中，它还保存 declaration `SymbolId`、可选 parameterized member index 和声明
`ScalarType`；因此 named register（如 `%tmp`）与 `name<count>` member 都有稳定身份。
numbered-register index 仍只是可选便捷属性，不能单独充当身份。无 binding context 的
standalone resolver 保留旧边界：只接受 numbered register，并令 symbol/type 字段为空。

## 按 opcode 生成的结构

每个 opcode 生成一个外层 struct，并用 `VariantType` 和 `std::variant` 表示由
modifier 组合唯一确定的 variant：

```cpp
struct Add {
  enum class VariantType { IntegerNoSat, Sat, PackedOptionalSat };

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

fixed modifier 不作为每个 instruction instance 的可写状态保存。合并后的 `Add::Sat`
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

`resolve<T>(const AstInstruction&)` 与带 `ResolveContext` 的重载共享生成的 opcode 专用
实现，公共逻辑依次执行：

1. 公共 matcher 先用全部 syntax descriptor 诊断真正未知的 spelling，再分别在每个
   候选 variant 内把 spelling 绑定到唯一活动 slot。重复占用一个 slot 会被诊断；单个
   variant 内一个 spelling 归属多个活动 slot 则是 descriptor bug。
2. `selectVariant<T>` 只依据上述 variant-local 绑定选择唯一 variant。`absent`、
   `optional`、`required/fixed` 都按 slot 和允许值匹配，不依赖源码 modifier 顺序。
3. 在选定 variant 内按 AST operand shape 与 arity 选择唯一 `OperandLayout`。
4. `resolve_fields` 按 resolved descriptor 把 modifier 和 operand 转换为带位置的
   resolved 值；有 binding context 时，寄存器必须解析到当前 lexical scope 的 `.reg`
   declaration，并写入 `SymbolId` 与声明类型。
5. 生成的 builder 将字段放入对应 C++ struct 或 layout payload。

零个匹配 variant/layout 是用户诊断；多个匹配 layout 或 descriptor 与生成结构无法
对应是生成器/descriptor bug，使用 `ResolveException` 区分于 `ResolveDiagnostic`。

`selectVariant<T>` 是手写公共 ABI 头中的通用模板适配器，任何满足 `PtxOperator`
concept 的类型都可以直接使用；它把 descriptor 交给 out-of-line 的非模板 matcher，
再把选中的 variant name 转成对应 `VariantType`。全部 opcode struct 以及
`resolve<T>`、`check<T>` 的显式特化
声明集中在单一生成头 `resolved_ir.gen.hpp`；后两者的定义不使用 `inline`，而是按 YAML
category 生成到 `resolved_ir_<category>.gen.cpp` 并编译进库。这一边界把体积小且通用的
类型适配留在模板中，同时避免每个 consumer translation unit 重复解析 variant matcher、
大型 resolve builder 与 checker visit/lambda，并保留统一公开 include。

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
- operand 字段 ID、resolved shape，以及由结构化 descriptor 约束的 immediate 或已绑定
  register 声明类型。

`rule_id` 留给指令特有规则的 typed wrapper。寄存器符号可见性与 `.reg` state-space 在
module resolution 阶段检查；地址空间和跨 instruction 约束仍不属于当前公共 checker
ABI。

## 扩展规则

- YAML 的 semantic variant 由 modifier 组合定义；不得因生成方便而增加假 variant。
- 每个 generated member 必须是一个 resolved PTX fact 或其位置，不生成 `direct`、
  `sub_struct` 等 C++ 后端布局开关。
- 新 operand shape 应先加入 Syntax AST 与 syntax descriptor，再加入 resolver 与
  checker 的对应 resolved value。
- 新的多 layout 指令必须测试正常 resolution、非法 layout、以及 tag/payload 不一致。

实现入口见 `include/ptx_ir/resolved/ptx_resolved_ir.hpp`、
`include/ptx_ir/ptx_resolved_ir_checker.hpp` 与生成的 `resolved_ir.gen.hpp`。
