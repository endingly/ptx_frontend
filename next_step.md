# Symbol binding：当前进度与下一步

通用 variable declaration 已覆盖 module/function scope、linkage qualifier、
`.reg/.param/.local/.shared/.global/.const`、`.align`、`.v2/.v4`、parameterized
variable name、多维 array，以及 initializer/constant-expression 的主要 grammar shape。

## 已完成：lexical symbol binding

新增公开的 lexical symbol-binding pass：

```cpp
auto result = ptx_frontend::binding::bindSymbols(module);
```

`SymbolTable` 当前支持：

- module root scope 与每个 `.entry/.func` 的 function scope；
- module/function variable、input/return parameter、function、label symbol；
- function local 对 module symbol 的 lexical shadowing；
- instruction predicate/operand、initializer symbol、array-dimension symbol reference；
- `name<count>` 的紧凑表示和成员 lookup，不展开成大量 symbol；
- same-scope duplicate symbol、parameterized name-set overlap 与非法/零
  parameterized count 诊断；
- 未解析 reference 的保留，供后续 special-register/linkage/opcode-aware 诊断。

公共类型位于 `submod/binding/include/ptx_symbol_table.hpp`，设计说明位于：

- `docs/zh-han/symbol_binding_design.md`；
- `docs/us-en/symbol_binding_design.md`。

同时修正了 parameterized variable name 的已有偏差：PTX 允许 `<count>` 用于任意 state
space，并非仅 `.reg`；但它不能同时声明 array 或 initializer。CST/AST 字段已从
`register_count` 直接更名为 `parameterized_count`，没有保留重复兼容字段。

## 关于 fixed address

最新版 NVIDIA PTX ISA 的 variable-declaration 概述仍有“optional fixed address”一句，但
规范没有提供独立语法、约束或示例。当前不据此发明 parser grammar；只有取得规范性
grammar 或可验证的 `ptxas` 行为后再加入对应 CST/AST 节点。

## 已完成：binding-aware module resolution

新增公开入口：

```cpp
auto result = ptx_frontend::resolved_ir::resolveModule(module);
```

- `ResolvedModule` 拥有本次解析的 `SymbolTable`；
- `ResolvedFunction` 使用 function `SymbolId`，不再只依赖字符串名称；
- module resolution 为每个 function scope 显式传入 `ResolveContext`；
- `ResolvedRegisterRef` 保存 declaration `SymbolId`、parameterized member index 与
  declaration `ScalarType`，并支持 named register；
- module context 中 typed instruction operand 的未声明 register、非 `.reg` symbol 和
  predicate/general register class 不匹配都会产生 resolve diagnostic；
- checker 现在能按 declaration type 校验 register operand；
- standalone `resolveInstruction`/`resolve<T>` 仍可无 symbol table 使用，保持原有单指令
  边界。

## 已完成：reference classification

- `Symbol` 记录 `.extern/.visible/.weak` linkage；
- `SymbolReference` 区分 declared、external、special-register 与 unresolved；
- `.extern` reference 仍绑定本 module 中的 declaration `SymbolId`；
- special register 按 PTX ISA 预定义名称和有界 family 精确匹配；
- 真正 unresolved 的 predicate、instruction operand、initializer 与 array-dimension
  reference 会产生按来源区分的 binding diagnostic；
- module resolution 对 special register 给出“已识别但当前 operand 不支持”的独立诊断。

## 已完成：declaration semantics

新增公开的 `declaration_semantics::checkDeclarations(module, symbols)` pass，并接入
`resolveModule()`：

- array dimension 必须求值为正整数 constant；未定长仅允许第一维且由 initializer 推导；
- array/vector initializer 的 brace nesting 与各维元素上限会被校验，同时保留 PTX
  允许少填并补零的规则；
- scalar initializer 区分 integer、floating 与 symbol address，并限制 address target 与
  destination type；
- module scope 同名 declaration 共享稳定 `SymbolId`，随后校验 variable/function signature、
  linkage、prototype/definition 组合与 multiple definition；
- function symbol 的 `owned_scope` 在存在 definition 时指向 definition scope。

## 已完成：P1 correctness hardening

- integer constant evaluator 现在保存 `.s64/.u64` 类型与完整 64-bit bit pattern，支持负数
  中间值、cast、usual arithmetic conversion 和 signed shift；
- 未建模的 function-header token 在 CST parser 直接报错，不再经 `header_tokens` 进入 CST
  后由 AST lowering 静默丢弃；
- parameterized declaration 会与 explicit/generated name set 检查 overlap，lookup 只接受
  无前导零的规范成员后缀，同时允许 parameterized base 与同名 explicit symbol 共存。

## 已完成：call/branch 专用 operand grammar

- `call` 的 return/input group、callee 和 target-set/prototype 现在拥有独立 CST/AST 节点；
- direct `bra` target 现在是独立 label-target node；
- parser 按 opcode 校验 call/branch layout，不再把 call group 当成 vector pack；
- binding 使用独立 reference kind，并校验 function/function-pointer、`.reg/.param` call
  parameter 与 function-local label；
- descriptor-facing operand shape 已同步到 C++ 与 Python model，但尚未把非 `Flat` call
  layout 伪装成 generated opcode。

## 已完成：binding-aware execution predicate

- 每个 generated opcode 外层保存可选的 `WithLocs<ResolvedPredicate>`，不再在
  AST→Resolved IR 时丢失 `@%p/@!%p`；
- module resolution 要求 guard 绑定到当前 scope 的 `.pred` declaration，并保存
  `SymbolId`、声明类型、negation 与 source range；
- standalone resolver 继续支持无需 symbol table 的 numbered `%pN` guard。

## 已完成：binding-aware direct branch resolution

- 新增 `bra` YAML 规格并接入 generated public type、private resolve/check 实现及三类
  descriptor；
- `ResolvedBranchTarget` 保存源码 spelling，module resolution 还保存当前 function label 的
  稳定 `SymbolId`；
- `.uni` 与 execution predicate 都会保留，checker 校验独立的 branch-target operand shape；
- standalone `resolveInstruction("bra target;")` 保持无 symbol table 可用，目标 identity 为空。

## 已完成：binding-aware special-register resolution

- 新增独立的 `special_registers` 语义注册表，以预定义名称为键记录 element type、vector
  width、最低 PTX ISA 与最低 SM；binding 的分类逻辑复用该注册表，不复制名称集合；
- 新增 `ResolvedSpecialRegisterRef`，保存源码 spelling、稳定 identity 与可选 vector component；
  vector family 仅在选择 `.x/.y/.z` 标量 component 后进入当前 scalar operand；
- checker 除 operand shape/type 外，还按具体 special-register value 检查 PTX/SM 可用性；
- `mov.u32 d, sreg` 首先消费该表示并覆盖 standalone 与 binding-aware module resolution；
  后续统一 source 阶段已在不复制 special-register registry 的前提下扩展 source form。

注册表只描述 special register 的稳定身份、现行声明类型/shape 与 intrinsic availability；
指令相关的历史读取版本由 YAML checker descriptor 描述，resolution 不生成 target-dependent
有效类型。

## 已完成：binding-aware address/symbol resolution

- 新增 `ResolvedSymbolRef`，module resolution 保存稳定 `SymbolId`、parameterized member、
  declaration kind、声明/有效 address state space 与可表示的 declaration scalar type；
  standalone resolution 保留 spelling 并令 declaration identity/state-space 为空；
- 新增 `ResolvedAddress`，base 明确区分 register、immediate address 与 data symbol，offset
  保留加减运算及已解析的 signed 64-bit value；
- `mov.u64 d, symbol` 首先覆盖非参数 addressable data variable 的取地址形式；后续统一
  source 阶段已加入 symbol+offset，parameter address 与 function address 也已完成；
- generic scalar `ld/st` 要求方括号解引用，address base 支持 register、immediate 与
  binding-aware data symbol；basic explicit state-space scalar form 也已接入；
- generic form 的 PTX 2.0 / SM 20 与 basic explicit form 的 PTX 1.0 / SM 0 可用性由
  generated checker 统一检查，generic bound-space policy 由 operand descriptor 提供。

## 已完成：分类后的 mov scalar source

- 新增 `ResolvedMovSource`，在 binding 后统一区分 register、immediate、special register、
  data symbol 与 address expression，避免这些 source 共享 identifier syntax shape 时产生
  variant/layout 选择歧义；
- 该阶段使 32/64-bit scalar `mov` 支持 register、typed immediate、对应宽度的现行 special
  register、addressable data symbol、formal parameter 及 `symbol+constant-offset`；
- 两种宽度现在共享 `mov_scalar_src` operand kind；此前仅用于限制 `.u32` address shape 的
  `mov_data_src`/`mov_address_src` 分裂已不再表达真实语义，因此从 schema/backend model 移除；
- descriptor 仍按 variant 精确限制允许的 resolved operand shape，checker 对 register、
  immediate 与 special register 执行 instruction type 检查，并保留 special-register target
  availability；
- 16-bit、历史 special-register width 与 vector form 均在后续阶段完成。

## 已完成：mov formal-parameter address

- `mov.u32/.u64` 均可取得 ordinary data variable、kernel/device-function formal parameter 的
  地址，并支持 constant offset；function-local `.param` call-argument variable 仍按 PTX 规则
  拒绝取址；
- `ResolvedSymbolRef` 分开保存 declaration state space 与 effective address state space：kernel
  parameter 的 `mov` 地址属于 `.param`，device-function parameter 经 `mov` 取址会物化到
  stack，因此地址属于 `.local`；未经过 `mov` 物化的 direct parameter memory address 保持
  `.param`，供 `ld/st` state-space compatibility 使用；
- input/return parameter identity 通过 declaration `SymbolKind` 保留；device-function
  parameter 地址携带 PTX 2.0 / SM 20 baseline，return parameter 将最低 PTX 提升至 6.0；
  direct symbol 与带 offset address 都由 checker 检查这些门槛；
- standalone resolution 继续只保留 spelling，不在缺少 module/function context 时猜测参数类别、
  state space 或 availability。

## 已完成：mov function address

- 新增 `ResolvedFunctionRef`，module resolution 保存稳定 function `SymbolId` 与
  `.func/.entry` 类别；
- `mov.u32/.u64` 接受 bare device/kernel function name；带 offset 的形式继续按 data-symbol
  address 解析并拒绝；
- device-function 地址沿用 `mov` 的 PTX 1.0 baseline；kernel function 地址携带 PTX 3.1 /
  SM 35 门槛，并由 checker 按 target 检查；
- standalone resolution 无法区分未绑定名称是 data 还是 function，因此仍保留为无 identity 的
  `ResolvedSymbolRef`。

## 已完成：mov 16/32/64-bit scalar type family

- 原先固定 `.u32/.u64` 的两条 variant 合并为一个动态 type field variant，覆盖
  `.b16/.u16/.s16/.b32/.u32/.s32/.f32/.b64/.u64/.s64/.f64`；
- checker 在共享入口实现 PTX 基础类型兼容：同宽 bit-size 接受任意基础类型，signed/unsigned
  integer 互相兼容，integer/float 混用仍拒绝；
- `.f64` modifier value 携带 SM 13 门槛；data address 限制为 integer/bit-size type，function
  address 限制为 integer type；当前无 warning channel，因此 PTX 允许但会警告的 signed
  function-address form 保持成功；
- data/function address 继续限定为 32/64-bit，16-bit form 不会误接受 symbol address；
- vector form 在后续阶段完成。

## 已完成：mov predicate form

- 新增结构独立的 `mov_pred` variant，source/destination 复用现有 `ResolvedPredicate`，不扩展
  `ResolvedMovSource`；
- module resolution 要求两端绑定到未取反的 `.pred` register，并保留 declaration `SymbolId`
  与 parameterized member；standalone resolution 继续支持 numbered predicate register；
- checker 复用 fixed `.pred` operand type/shape descriptor，PTX 1.0 起适用于全部 target；
- vector form 在后续阶段完成。

## 已完成：mov historical special-register widths

- `%tid/%ntid/%ctaid/%nctaid` component 的 16-bit read 使用 PTX 1.0 baseline；
- `%gridid` 的 16/32-bit read 分别使用 PTX 1.0/1.3 baseline，现行 64-bit read 仍从 PTX
  3.0 可用；
- 历史规则由 `mov_scalar` 的 YAML `operand_type_compatibilities` 声明并生成到 checker
  descriptor；checker 按 stable identity 与 instruction width 创建临时有效类型/availability
  视图，不修改 Resolved IR；同宽 bit/signed form 复用现有基础类型兼容，float 和无历史
  规则的 special register 仍拒绝。

## 已完成：special-register target 分层重构

- special-register catalog 只保留 intrinsic identity、现行声明 type/shape 与自身 availability，
  删除 `mov` 专用历史字段和 `effectiveMovInfo`；
- `ResolvedSpecialRegisterRef` 只保留 target-independent identity、component 与 spelling；
- YAML variant 可用 `operand_type_compatibilities` 表达某个 operand/value/width 的上下文有效
  type 与 PTX/SM 门槛；generator 将其解析为 checker descriptor，不进入 syntax descriptor；
- checker 在验证期间临时选择上下文规则，覆盖历史 `mov` read，同时确保其他 consumer 和
  Resolved IR 不继承 `mov` 语义；
- syntax baseline 继续由编译期支持范围固定，不接收 target version/architecture 输入。

## 已完成：mov vector pack/unpack

- `mov_scalar` 保持单一 modifier variant；scalar、pack、unpack 由三个 operand layout 表示，
  避免为相同 `.b16/.b32/.b64` modifier 组合复制 variant；
- 新增 `ResolvedRegisterVector`，以 2/4 个可选 `ResolvedRegisterRef` 保存 vector payload；空元素只表示
  destination `_` sink，binding 不再把 `_` 当作未声明 symbol；
- YAML `kind: reg_vector` 的 `vector.arity` 进入 resolved/checker descriptor；resolver 与 checker
  都验证 arity、bit-size instruction type、element width、source sink 与全 sink destination；
- `.b128` 仅由 pack/unpack layout 接受，并按 modifier value 要求 PTX 8.3 / SM 70；scalar
  `.b128` 明确拒绝；
- C++ 回归覆盖 2/4-element pack、unpack、sink、`.b128` 双向读取、target 门槛及损坏
  Resolved IR 的 element-width 检查。

## 已完成：ld/st generic bound-space policy 与 basic explicit state-space 闭环

- generic scalar `ld/st` 保持 PTX 2.0 / SM 20；generic load 对已知 effective space
  接受 `.const/.global/.local/.shared`（`.const` entry 要求 PTX 3.1），generic store 接受
  `.global/.local/.shared` 并拒绝 `.const/.param`；load 的单一 `ExplicitScalar` variant 接受
  `.const/.global/.local/.shared`，store 的单一 `ExplicitScalar` variant 接受
  `.global/.local/.shared`，显式 form 使用 PTX 1.0 / SM 0 baseline；
- resolved `MemoryStateSpace` identity 与 backend runtime domain 已覆盖上述值；实际
  state-space modifier 保留为带 location 的 generated instance field，generic variant 不伪造
  state-space field；
- YAML operand `state_space` 的 scalar/list static allowlist 与
  `{expr: modifier(state_space)}` 由 schema、normalizer、resolved model 与 descriptor
  贯通；list entry 可携带独立 availability，normalizer 拒绝重复、未知或混合 shape，dynamic
  expression 只能引用 active state-space modifier；
- generated address `OperandView` 从 `ResolvedAddress` 的 symbol base 读取 effective address
  state space；declaration/effective identity 保持区分，register、immediate 与 standalone
  address 的 unknown space 不推断；
- checker 对已知 effective space 执行 static generic allowlist 或 runtime explicit exact-match，
  并以 `AddressStateSpaceMismatch` 诊断不兼容空间；命中 static entry 后按其 availability
  检查 PTX/SM/family。generic `.const` load 的 PTX 3.1 条件已由数据建模，unknown register
  address 仍正常通过，`st.const` modifier 明确不受支持；
- Python schema/model/normalizer/generator 与 C++ resolver/checker 回归覆盖 static plain/
  availability entry、非法输入、descriptor、generic allow/reject/availability、runtime
  explicit field/location、store source type、方括号要求及损坏 descriptor field；
- legacy `.v2/.v4` vector memory form 已在后续切片完成；memory consistency、modern vector
  form 与 `.b128` 仍待后续。explicit `ld.const` 本身属于 PTX 1.0 basic explicit baseline。

## 已完成：ld/st explicit `.param` direction 与 function availability

- `Ld::ExplicitScalar` 与 `St::ExplicitScalar` 保持单一 runtime state-space variant，并分别将
  `.param` 加入允许值；bound `.param` load 只接受 input parameter，store 只接受 return
  parameter，方向错误报告独立的 `ParameterDirectionMismatch`，不叠加 target 诊断；
- `ResolvedAddress` 保存 `Unknown/Entry/Device` enclosing function provenance；generated
  address `OperandView` 仅从 bound declaration identity 提取可选 input/return direction，
  不根据 spelling 或未知 register/immediate/standalone address 猜测；
- YAML operand 新增 typed `parameter` constraint，声明 direction 与
  `function_availability`；normalizer 要求它只用于 `kind: addr`，伴随引用 active
  state-space modifier 的 expression，且 modifier 必须允许或 fixed 为 `.param`；
- common checker 只在 runtime 选择 `.param`，且实际 state space 为 unknown 或 `.param`
  时应用该 constraint：known wrong direction 优先；device-function `ld.param` 与所有
  `st.param` 按 YAML 检查 PTX 2.0 / SM 20，entry input `ld.param` 保持 explicit PTX 1.0 /
  SM 0 baseline；standalone `ld.param` 的 unknown context 不加门槛，standalone `st.param`
  仍检查 2.0 / 20；
- `ResolvedSymbolRef::address_availability` 语义未扩大，direct device parameter memory
  address 继续保持为空；function provenance 只服务当前 dereference instruction 语义；
- Python 回归覆盖 schema/normalizer/resolved model/generated descriptor，C++ 回归覆盖
  entry/device/unknown context、direct/register address、正确与错误 parameter direction、
  target 双诊断、known non-`.param` exact mismatch 以及 runtime field/location；
- function-local call-argument `.param`、`::entry/::func` qualifier、call
  adjacency/predication 仍留到后续 call-context 工作，不在本切片内。

## 已完成：ld/st 14-type scalar family

- `Ld`/`St` 的 variant 与 operand pattern 统一重命名为
  `GenericScalar`/`ExplicitScalar`；四种 form 继续复用既有 address-space allowlist、
  runtime exact-match、parameter direction 与 function availability；
- YAML 复用一个不含 `.f64` 的 13-type set，并由每个 variant 追加 `.f64`，最终覆盖
  `.b8/.b16/.b32/.b64`、`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64`、`.f32/.f64`；
  `.b128` 未加入 scalar memory family；
- load destination 与 store source 均以 `modifier(type)` 派生 operand type；generated
  runtime `WithLocs<ScalarType>` 保留实际 type 与 modifier 位置，checker 复用公共
  fundamental-type compatibility，并由下述静态 policy 控制 register width；
- explicit `.f64` 通过 modifier-value availability 要求 SM 13；generic variant 已要求
  SM 20，因此 generic `.f64` 不重复附加较低门槛；
- Python model/generator 回归覆盖 14 个 allowed value、动态 operand expression 与 explicit
  `.f64` availability；C++ 回归覆盖 14-type generic/explicit load/store resolution、runtime
  type/location、代表性 bound register compatibility、same-width float/integer mismatch，
  以及 explicit/generic `.f64` target 行为；
- legacy `.v2/.v4` vector memory form 已在后续切片完成；`.b128` 与 memory consistency
  仍待后续。

## 已完成：ld/st legacy cache operator

- 仅在现有四个 scalar `ld/st` variant 上增加单一 optional runtime `cache` modifier field，
  不为每个 cache spelling 复制 variant，也不引入 state-space×cache 的交叉规则；
- load 允许 `.ca/.cg/.cs/.lu/.cv`，store 允许 `.wb/.cg/.cs/.wt`；所有显式 cache value
  都通过 modifier-value availability 建模 PTX 2.0 / SM 20 门槛；
- 省略 cache 时，resolved IR 保存 `CacheOperator::Unspecified` 且 `locs` 为空；
  该值是 source-absence sentinel，不是可拼写 syntax value，也不直接代表 PTX 的硬件默认语义；
- PTX 的 effective omission behavior 仍按 ISA 生效：省略时 `ld` 等效 `.ca`、`st` 等效 `.wb`；
  IR 保留 `Unspecified`，是为了区分源码 provenance，避免把 omission 伪装成显式 modifier，
  同时避免 sentinel 触发 cache value availability；
- normalizer/resolved model/backend domain/generator/C++ resolver/checker 已贯通
  `CacheOperator::{Unspecified,Ca,Cg,Cs,Lu,Cv,Wb,Wt}`，checker 仅对显式 cache 执行
  modifier-value availability，omitted sentinel 保持静默；
- Python/C++ 回归覆盖合法 `ld/st` cache 值、`ld.wb`/`st.ca` 拒绝、resolved field value 与
  location、omitted sentinel + empty loc、显式 cache 的 PTX1/低 SM 双诊断、PTX2/SM20
  通过，以及 normalizer 对不可拼写 `unspecified` syntax value 的直接拒绝；
- legacy `.v2/.v4` vector memory form 已在后续切片完成；memory consistency qualifier 与
  跨 modifier 规则仍待后续。

## 已完成：ld/st wider-register type policy

- YAML operand 新增 `register_width: same_width|equal_or_wider`，默认为
  `same_width`；normalizer 拒绝在非 `kind: reg` 或没有 type expression 的 operand 上使用
  非默认 policy，避免静态 constraint 静默失效；
- normalized/resolved typed model、backend domain、generated operand descriptor 与 C++
  `OperandDescriptor` 已贯通 register-width policy；它是静态 checking fact，不进入 runtime
  Resolved IR field；
- `base::scalar_types_compatible` 接受显式 `ScalarTypeSizePolicy`，默认保持 same-width；
  `EqualOrWider` 仅放宽为 declared register size 大于等于 instruction size，之后仍遵守：任一
  侧 bit type 可兼容、fundamental signed/unsigned integer 互容、float 只接受 exact
  type/size、integer/float 不兼容；wider actual 当前只覆盖到 64-bit，`.b128` 在 declaration
  type availability registry/checker 完成前明确拒绝；
- scalar `ld` destination 与 `st` source 的 generic/explicit descriptor 选择
  `EqualOrWider`；后续 legacy vector memory element 也复用该 policy。immediate、
  special register 以及 `mov/add/sub` 保持 `SameWidth`；
- Python 回归覆盖 policy normalization、非法无效配置、resolved model/backend/generated
  descriptor；C++ base/checker/module 回归覆盖 wider integer、bit/float matrix、narrow
  rejection、float/integer rejection、PTX 8.3/SM 70 下 wider actual `.b128` rejection，以及非
  memory opcode 的 same-width 行为；
- 本 wider-register 切片没有增加 instruction-specific checker rule，也没有扩展 vector 或 memory
  consistency qualifier。

## 已完成：ld/st legacy braced vector memory form

- `mov` 的 vector payload 语义已从 mov-specific 名称改为通用
  `ResolvedRegisterVector`/`reg_vector`/`RegisterVector`，pack/unpack 行为与诊断保持回归；
- YAML 只新增四个 memory vector variant：`ld_generic_vector`、`ld_explicit_vector`、
  `st_generic_vector`、`st_explicit_vector`；`.v2/.v4` 作为 required runtime `vector`
  modifier value，不按 arity 复制 variant；
- `vector.arity: {expr: modifier(vector)}` 已贯通 schema、normalizer、resolved model、
  descriptor、resolver 与 checker；descriptor 保存 `vector_arity_modifier_field_id`，实际
  braced operand arity 会与 runtime field 对齐检查；
- vector type policy 改为数据驱动：`mov` 使用 aggregate policy，memory vector 使用 element
  policy；memory vector element 复用 `EqualOrWider` register-width policy，不允许 `_` sink；
- generic/explicit baseline、14-type family、cache operator、address allowlist、`.param`
  direction/function availability、explicit `.f64` SM13 与 generic PTX2/SM20 规则继续复用
  既有 scalar 数据；
- 本切片只覆盖 legacy braced operands，memory vector payload 最多 128 bit（`.v2` 到
  64-bit type、`.v4` 到 32-bit type）；`.v4` 64-bit 与 `.v8`、modern sink、`.b128`、
  vector-variable shorthand、consistency qualifier 或 alignment 规则均仍未实现。

## 已完成：小模块的 source-bearing interface library

- `base`、`cst`、`syntax`、`binding` 与 `semantic` 改为 CMake interface library，
  由最终实体 target 统一编译这些小模块的实现源；
- 实现源通过 `target_sources(... INTERFACE ...)` 传播，而不是作为
  `add_library(... INTERFACE ...)` 的私有 source 丢失；
- include path 与依赖使用 `INTERFACE` usage requirements，模块测试和
  `resolved_ir` 等消费者得到同一组源码与编译依赖。

## 已完成：resolved_ir CMake codegen 归属

- `gen_all.py` 的输出发现、生成命令、输入依赖和 topology reconfigure tracking 已迁入
  `cmake/generate_ptx_frontend.cmake`，仅由 `submod/resolved_ir` include；
- 生成文件位于 `submod/resolved_ir` 自身 binary directory，`resolved_ir_codegen` target
  负责生成，`resolved_ir` 消费生成源码及 public/private include path；
- `gen_all.py` 当前保持原子调用；包括 syntax descriptor 在内的全部输出均实现或依赖
  generated Resolved IR 类型，因此不按文件名伪拆到更早的 submodule；
- 顶层 CMake 仅保留工程配置、子模块与 facade target，不再拥有 resolved IR codegen。

## 已完成：英文项目 README

- 根目录 `README.md` 说明项目定位、当前 PTX/Resolved IR 覆盖边界、frontend pipeline、
  构建测试命令、source-tree CMake 接入方式、最小 parse/resolve 示例与代码生成流程；
- 明确 pre-1.0 API、未启用 install/export、非完整 PTX validator/code generator，以及已添加
  [MIT license](LICENSE) 文件，避免对未实现能力作出承诺；
- README 示例已通过独立 source consumer 编译和运行验证。

## 下一步评审与调整

原计划的方向正确，但依赖顺序需要拆开：

- `bra` 已有专用 AST operand 与完整 label binding，并且是单一平坦 operand layout；它不依赖
  `call` 所需的 group/variadic layout algorithm，应先独立接入 generated dispatch/checker；
- address/symbol Resolved IR 应和首个实际消费它们的 opcode 一起落地，避免先建立没有 descriptor
  使用者的悬空表示；
- `call` 的参数组、可选返回组和 target-set/prototype 才真正需要非 `Flat` layout，应在
  flat control-flow 闭环稳定后单独设计。

`bra`、special-register consumer 与首个 address/symbol consumer 已完成闭环；剩余顺序是：

1. 为 `ld/st` 增加 modern vector form、alignment 与其余跨 modifier 规则；已完成
   PTX ≤9.2 的 omission/`.weak`/`.volatile`/scoped
   `.relaxed/.acquire/.release` 及 PTX 8.2 scalar `.mmio.relaxed.sys` consistency
   qualifier。legacy `.v2/.v4` 明确不接受 mmio；static address-alignment checking 与
   modern vector extension 仍保留为后续工作；
   function-local call-argument `.param` 及相关 call-context 规则留到 `call` 阶段；
2. 为 `call` group/variadic operand 增加非 `Flat` descriptor layout algorithm，再将 `call`
   接入统一 dispatch/checker；
3. 表示 `.calltargets/.callprototype/.branchtargets` 及其余 module/function directive。

## 验证结果

- YAML validation：6/6；
- Python full unittest discovery：57/57；
- `gen_all.py` 全量生成：通过，generated artifacts 10 个；
- Debug `test_resolved_ir` standalone：108/108；
- Debug clean-first full build：121/121 steps；CTest：181/181；
- cached/unstaged `git diff --check`：通过。
