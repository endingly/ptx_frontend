# C++ Resolved IR 设计

## 状态与边界

本文描述当前实现的 Resolved PTX IR，而不是一个未来的 CFG、SSA 或后端 IR
设计。frontend 的核心数据流为：

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST 忠实保存源码拼写、modifier 顺序和 `SourceRange`；Resolved IR 则记录已经
选定的指令 variant、已解析的 operand 值与诊断位置。二者都属于 frontend 的稳定边界。
lexical symbol binding 与 module resolution 已接通，execution predicate 会解析为带声明
身份的值，special register、external symbol 和真正未声明 reference 也已能区分。
`mov` 的 16/32/64-bit scalar type family 已接入 register、immediate 与 special-register；
32/64-bit form 还接入 data-symbol、`symbol+offset`、function-address 与合法 formal parameter
地址；bit-size form 还支持 2/4-element vector pack/unpack，`.b128` 仅用于 vector form；
`mov.pred` 复用 declaration-aware `ResolvedPredicate` 表示；
generic 与 basic explicit-space scalar 以及 braced-vector `ld`/`st` 已为
14 种 8--64-bit bit-size、integer 与 floating-point type 接入解引用 address operand。
legacy memory-vector payload 最多 128 bit：`.v2` 到 64-bit type，`.v4` 到
32-bit type；PTX 8.8/SM 100 另支持精确 256-bit 的 `.v8` × 32-bit 与 `.v4` × 64-bit。
静态 natural alignment 会检查已绑定 data symbol 的常量 byte offset 和 absolute immediate；
register 与 standalone unresolved address 保持 unknown。其余 source form、其余 memory
qualifier extension、CFG、SSA 和目标 lowering 仍是后续 pass，不应改变此层的结构。
`ResolvedIndirectCallee` 现在为 non-predicate `.reg` indirect target 或已绑定的 function-local
`.callprototype`/`.calltargets` label 提供 descriptor-independent identity；它有意不携带 metadata
payload 或 ABI。generated `Call::Direct` 现有三个额外的 `IndirectCall` layout
（target/metadata、target/input/metadata、return/target/input/metadata），均要求 PTX 2.1 / SM 20；
normal module indirect call 会保留已绑定的 target 与 metadata identity。ABI comparison 仍留给后续工作。

生成的公共层还提供了一个与具体 opcode 无关的边界：

```cpp
using ResolvedInstruction =
    std::variant<Add, Sub, Bar, Bra, Call, Mov, Ld /* ... */>;

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

module resolution 还负责不能放入 generated single-instruction checker 的 direct-call ABI 与
call-context 工作：它取得 canonical prototype/definition signature，检查 return/input actual
和按 formal 定型的 literal，并执行 function-local `.param` 的 qualifier、predicate 与 staging
adjacency 约束。generated checker 仍只负责一个 resolved instruction 及 target-aware descriptor
规则。

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
`ResolvedRegisterRef`、`ResolvedImmediate`、`ResolvedPredicate`、
`ResolvedBranchTarget`、`ResolvedSpecialRegisterRef`、`ResolvedFunctionRef`、`ResolvedSymbolRef`、
`ResolvedAddress`、`ResolvedMovSource` 与 `RegOrImm`。
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
instruction 的可选 execution predicate 作为 opcode 外层公共字段
`std::optional<WithLocs<ResolvedPredicate>>` 保存；module resolution 要求其绑定到 `.pred`
register，standalone resolution 则接受 numbered `%pN`。`ResolvedBranchTarget` 同样区分两种
边界：module resolution 保存当前 function label 的 `SymbolId`，standalone resolution 保存
源码 spelling 而令 identity 为空。

`ResolvedSpecialRegisterRef` 保存准确 spelling、稳定的 `SpecialRegisterId` 与可选 vector
component，不保存依赖具体指令或 target 的有效类型。独立的 special-register 语义注册表
是名称、稳定身份、现行声明 element type、vector width 及 intrinsic 最低 PTX/SM 的单一
事实来源；binding 只复用它做分类。scalar operand 接受标量 special register 或
`%tid.x` 一类 component，不接受未选 component 的 vector base。

ISA 曾扩宽的读取形式属于指令语义，不属于寄存器自身：`mov` variant 在 YAML 的
`operand_type_compatibilities` 中声明 special-register identity、instruction width、有效类型
与最低 PTX/SM，生成到 checker descriptor。checker 仅在本次检查期间选择有效元数据，
不会改写 Resolved IR。当前规则允许 `%tid/%ntid/%ctaid/%nctaid` component 的 16-bit read
从 PTX 1.0 开始，`%gridid` 的 16/32-bit read 分别从 PTX 1.0/1.3 开始；其他使用场景仍按
注册表中的现行声明类型和 intrinsic availability 检查。

单一 scalar variant 的 type 是动态 modifier field，覆盖 `.b16/.u16/.s16`、`.b32/.u32/.s32/.f32` 与
`.b64/.u64/.s64/.f64`。checker 按 PTX 基础类型规则接受同宽 bit-size/任意基础类型和
signed/unsigned integer 组合，但仍拒绝 integer/float 混用；`.f64` 值另携带 SM 13 门槛。

`mov.pred` 使用独立 variant，因为两端字段都是 `ResolvedPredicate`，与分类后的 scalar source
结构不同。module resolution 要求 source/destination 都绑定到未取反的 `.pred` register，并保存
稳定 `SymbolId`；standalone resolution 仍接受无需声明上下文的 numbered predicate register。

scalar 与 vector `mov` 共享同一动态 type modifier variant，因为 `.b16/.b32/.b64` 的
modifier 形式相同；三种 operand layout 分别表示 scalar、pack 与 unpack，不建立重复 variant。
`ResolvedRegisterVector` 保存 2/4 个可选 `ResolvedRegisterRef`，空元素表示 destination-only `_`
sink。resolver 与 checker 都要求 bit-size instruction type、vector 总位宽等于 instruction
位宽，并拒绝 source sink、全 sink destination 与 sub-byte element。`.b128` 仅由 pack/unpack
layout 接受，并携带 PTX 8.3 / SM 70 modifier-value availability。

`ResolvedFunctionRef` 保存源码 spelling、稳定 function `SymbolId` 与 `.func/.entry` 类别。
device-function 地址沿用 `mov` 的 PTX 1.0 baseline；kernel function 地址携带 PTX 3.1 /
SM 35 门槛，供 checker 按 target 检查。当前仅接受 bare function name；带 offset 的形式仍按
data-symbol address 解析并拒绝。

`ResolvedSymbolRef` 保存源码 spelling；module resolution 还保存 declaration `SymbolId`、
parameterized member、declaration kind、声明 state space、实际 address state space 与可表示的
declaration scalar type。普通 data variable 的两种 state space 相同；direct parameter memory
address 与 kernel formal parameter 的 `mov` 取址仍得到 `.param` address，而 device-function
formal parameter 经 `mov` 取址会将参数物化到 stack，因此得到 `.local` address。
device-function formal parameter 的 `mov` 地址值携带 PTX 2.0 / SM 20 baseline；return
parameter 再把最低 PTX 提升至 6.0，供 checker 按 target 检查。function-local `.param`
call-argument variable 是独立的 bound symbol：direct `ld.param`/`st.param` 地址保留 `.param`、
匹配任一 parameter direction，并要求 PTX 2.0 / SM 20；仍不能由 `mov` 取址。standalone resolution 无法完成 lexical binding，因此和 branch target 一样保留
空 identity/state-space。`ResolvedMovSource` 在 binding 后区分 register、immediate、special
register、data symbol 与 address expression，避免这些 identifier 形状在 variant/layout 选择
阶段产生歧义。standalone resolution 无法区分未绑定名称是 data 还是 function，因此仍保留为
空 identity 的 `ResolvedSymbolRef`。

`ResolvedAddress` 的 base 是 `ResolvedRegisterRef`、`ResolvedImmediate` 或
`ResolvedSymbolRef` 的 variant，可选 offset 保留加减 operator 和解析后的 signed 64-bit
value。32/64-bit integer 或 bit-size `mov d, symbol+offset` 使用未加方括号且限定为
addressable data-symbol 或 formal-parameter base 的地址值；
scalar 与 braced-vector `ld`/`st` 要求方括号解引用，覆盖 register、immediate 与
bound-symbol base。每个 opcode 使用 `GenericScalar`、`ExplicitScalar`、`GenericVector`
与 `ExplicitVector` variant；runtime type field 接受 `.b8/.b16/.b32/.b64`、
`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64` 与 `.f32/.f64`，当前 memory type 不包含
`.b128`。vector variant 额外要求 runtime `.v2/.v4/.v8` field，register-vector operand
descriptor 将期望元素数链接到该 field，而不是按 arity 复制 variant。memory vector 使用
element type policy：每个 register element 都按 instruction type 检查，允许
`EqualOrWider` register width。legacy payload 最多 128 bit；generated cross constraint
另加入 PTX 8.8/SM 100 的精确 256-bit `.v8` × 32-bit 与 `.v4` × 64-bit form，地址已知时
要求 global，并允许部分 sink。默认 register-width policy 为 `SameWidth`，
保持 `mov/add/sub`、immediate 与 special-register 的既有行为；只有 `ld` destination 与
`st` source register descriptor 选择 `EqualOrWider`，因此声明 register 位宽可大于等于
instruction type。通过 size 检查后，任一侧为 bit type 即兼容，fundamental signed/unsigned
integer 互相兼容，float 只接受 exact type/size，integer/float 仍不兼容。这同时覆盖声明
register 不超过 64-bit 的 wider load destination 与 store source（包括 store truncation）。
wider actual `.b128` register 在 declaration type 的 target availability 得到表示与检查前明确
拒绝；既有 `mov` vector consumer 的 exact `.b128` compatibility 不受影响。

explicit load 接受 `.const/.global/.local/.param/.shared`，store 接受
`.global/.local/.param/.shared`；`WithLocs` 同时保留 runtime state-space/type modifier 的值与
源码位置。explicit `.f64` 通过 modifier-value availability 增加 SM 13；generic `.f64` 不需要
额外 SM rule，因为 generic variant 已要求 SM 20。generated operand view 转换 bound symbol
的 effective address space，而不是按 declaration spelling 猜测；若它与 runtime field
不同，checker 报告 `AddressStateSpaceMismatch`。generic operand descriptor 携带带逐项
availability 的静态 bound-space allowlist：load 接受已知
`.const/.global/.local/.shared` address，其中 `.const` 要求 PTX 3.1；store 接受
`.global/.local/.shared`，并拒绝已知 `.const/.param` address。explicit `ld.const` 本身仍属于
PTX 1.0 basic explicit baseline。

legacy cache operator 复用同一组 scalar/vector `ld/st` variant，而不是为每个 cache
spelling 复制 variant。load 接受 runtime `.ca/.cg/.cs/.lu/.cv`，store 接受 runtime
`.wb/.cg/.cs/.wt`，每个显式 cache spelling 都携带 PTX 2.0 / SM 20 的
modifier-value availability。源码省略 cache 时，Resolved IR 保存
`CacheOperator::Unspecified` 且 `locs` 为空。这个 sentinel 是刻意保留的 provenance
元数据，不表示 PTX 没有实际硬件默认语义：ISA 仍规定省略时 `ld` 按 `.ca`、`st` 按 `.wb`
生效；IR 保留 `Unspecified`，是为了不把“源码未写”伪装成“显式写了默认值”，也避免它触发
cache value availability 检查。

memory consistency 采用生成的 cross-modifier descriptor，而不是把每种 qualifier
组合展开成 `ld/st` variant。`MemoryConsistency::Omitted`（空 `locs`）与显式
`.weak` 保持不同；`.volatile/.relaxed/.acquire/.release` 保留 modifier location。
checker 只允许 relaxed/acquire/release 携带 scope，拒绝 volatile/ordered/mmio 与
cache 的组合；对已知 address space 执行 global/shared、PTX 9.1 的
`volatile.local` 及 scalar `.mmio.relaxed.sys` 规则，而不猜测 unknown generic
address。生成的 `memory_vector` cross constraint 以 arity > 4、payload > 128 或 sink
识别 modern candidate，要求 256 bit、地址已知时 global、以及 PTX 8.8/SM 100；只有这些
modern load/store vector 可使用部分 sink，all-sink 与 legacy sink 仍拒绝。scalar、legacy
`.v2/.v4` 与 modern 256-bit 的静态 natural alignment 会按 total access size 检查已知 address。

`ResolvedAddress` 另行记录 enclosing function kind。generated address view 仅从已绑定的
`InputParameter`/`ReturnParameter`/function-local call argument 推导可选 parameter direction，不根据 spelling 猜测。
对于 explicit `.param`，生成的 operand constraint 要求 `ld` 使用 input parameter、`st`
使用 return parameter；已知方向错误只报告 `ParameterDirectionMismatch`，不叠加 target
诊断。device-function `ld.param` 与所有 `st.param` 都应用 YAML 提供的 PTX 2.0 / SM 20
function availability；kernel input `ld.param` 保持 explicit-form baseline。identity 未知的
address 不猜方向，但已知 device-function provenance 仍触发 load 门槛；standalone load 的
unknown context 不触发。该上下文规则不会修改 `ResolvedSymbolRef::address_availability`，后者
继续描述 `mov` 等 address-value 语义。

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
  std::optional<WithLocs<ResolvedPredicate>> execution_predicate;
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

`Flat` 用于逗号分隔、位置固定的 operand slot。唯一新增的 layout algorithm 是 `Call`：它识别
三种 direct-call group 排列，并把 input group 解析为一个 variadic field。它是固定 algorithm，
不是通用 repeat DSL；可变参数与 call group 仍不能伪装成 `Flat`。

## Resolution 协议

`resolve<T>(const AstInstruction&)` 与带 `ResolveContext` 的重载共享生成的 opcode 专用
实现，公共逻辑依次执行：

1. 公共 matcher 先用全部 syntax descriptor 诊断真正未知的 spelling，再分别在每个
   候选 variant 内把 spelling 绑定到唯一活动 slot。重复占用一个 slot 会被诊断；单个
   variant 内一个 spelling 归属多个活动 slot 则是 descriptor bug。
2. `selectVariant<T>` 只依据上述 variant-local 绑定选择唯一 variant。`absent`、
   `optional`、`required/fixed` 都按 slot 和允许值匹配，不依赖源码 modifier 顺序。
3. 在选定 variant 内按 AST operand shape 与 arity 选择唯一 `OperandLayout`。
4. `resolve_fields` 解析公共 execution predicate，并按 resolved descriptor 把 modifier 和
   operand 转换为带位置的 resolved 值；有 binding context 时，guard 必须绑定到 `.pred`
   register，普通寄存器必须解析到当前 lexical scope 的 `.reg` declaration，两者都会写入
   `SymbolId` 与声明类型，direct branch target 必须绑定到当前 function 的 label。
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
| Resolved descriptor | resolved field kind、modifier binding、operand binding、结构化 type/state-space 表达式、带逐项 availability 的静态 state-space allowlist 与语义 role/access |
| Checker descriptor | variant/layout/value 的 availability、operand type compatibility 与 rule ID |

三者不互相复制职责。Syntax descriptor 不应保存 resolved C++ 类型；Resolved descriptor
不负责 modifier 拼写识别；Checker descriptor 不重新描述 resolve binding。

## Checker 契约

`checker::check<T>` 是每个 opcode 的生成 wrapper，公共 checker 至少检查：

- variant、已选 operand layout 与实际 modifier value 的最低 PTX 版本、SM 版本与 target family；
- layout tag 的范围；
- layout tag/payload 一致性；
- operand 字段 ID、resolved shape，以及由结构化 descriptor 约束的 immediate 或已绑定
  register 声明类型。
- special-register intrinsic 元数据，以及由当前 instruction width 选择的上下文类型兼容与
  availability；该选择只产生临时检查视图，不改变 Resolved IR。
- static generic state-space allowlist、explicit modifier-derived constraint 与已知
  bound-symbol effective address space 的匹配，并检查 allowlist entry availability；
  register、immediate 与 standalone base 的未知 space 不推断。
- 由 generated operand constraint 描述的 explicit `.param` input/return direction 与
  function-context availability；方向错误优先于上下文 availability。

`rule_id` 留给指令特有规则的 typed wrapper。寄存器符号可见性与 `.reg` state-space 在
module resolution 阶段检查；公共 checker 已处理生成的 address-space constraint，跨
instruction 约束仍不属于当前 ABI。

## 扩展规则

- YAML 的 semantic variant 由 modifier 组合定义；不得因生成方便而增加假 variant。
- 每个 generated member 必须是一个 resolved PTX fact 或其位置，不生成 `direct`、
  `sub_struct` 等 C++ 后端布局开关。
- 新 operand shape 应先加入 Syntax AST 与 syntax descriptor，再加入 resolver 与
  checker 的对应 resolved value。
- 新的多 layout 指令必须测试正常 resolution、非法 layout、以及 tag/payload 不一致。

实现入口见 `submod/resolved_ir/include/ptx_resolved_ir.hpp`、
`submod/resolved_ir/include/ptx_resolved_ir_checker.hpp` 与生成的
`resolved_ir.gen.hpp`。

direct-call ABI、function-local call-argument `.param` memory、带限定的 `::entry`/`::func`
form，以及 call adjacency/predication constraint 均由 module resolution 覆盖。indirect-call
metadata、scalar `.b128` 与 wider `.b128` register 所需的 declaration-type availability 仍不在
本切片范围内。legacy scalar/vector `ld/st` cache operator、PTX 8.8 modern memory vector、static
memory-address alignment 与 memory consistency qualifier 已纳入本切片。
