# C++ Resolved IR 设计

## 向量数据搬运集成

下列完整 module 回归链同时验证 parsing、binding、resolution 与 checking。
原始 source 和 AST 销毁、module copy/move 后，还会验证 owned IR。这是
frontend 证据，不代表 GPU 执行、broadcast/truncation 语义或 assembler parity。

| 支持链路或边界 | 保留的契约与兼容性 |
| --- | --- |
| Named v4 f32 LD → scalar component → named/brace MOV → ST | Canonical lane 关联同一真实声明 identity；`.xyzw`/`.rgba` 为普通 register 的显式 selector，named projection 保留完整 identifier range，不伪造 selector。普通 `.w` 不会新增 hardware `%tid.w`。 |
| v2 u32 constant-expression MOV → alias/overlap MOV → ST | Folded typed value、parameterized register member 和 source form 在所有权转移后保留。同 lane 的 destination alias 仍为重复；source 重复和 source/destination overlap 合法。 |
| Named v2 f64 NC/LDU → MOV → ST | Memory 与 MOV 保留各自 type policy。既有 availability 不混用：NC 需 PTX 3.1 / SM 32，generic LDU 需 PTX 2.0 / SM 20，explicit-global LDU 需 PTX 2.0 并叠加 f64 SM 13，f64 MOV 需 SM 13。 |
| Named-array constant address → vector LD → MOV → ST | 声明 `.v4 .f32 A[]` 的 index stride 仍为标量四字节，不是十六字节。`A[4]` 贡献十六字节，可满足 v4 alignment；动态 index 将已知对齐降为 `gcd(base alignment, 4)`，仅保证四字节对齐时同一 vector access 被拒绝。 |
| 邻接的不支持组合 | Atom/red 保持 brace-only；scalar bit pack/unpack 接受总宽严格匹配的 named ordinary aggregate，并保留 sink/B128 门槛；floating instruction type 与 scalar-to-scalar B128 仍不支持。现代 256-bit memory 使用合法 scalar brace lane，不依赖超宽普通 named 声明，也不扩宽 vector MOV。 |

Owned revalidation 检查完整 source form、component origin/order、实际声明宽度、
type/arity 和 source owner，并对被篡改 payload 提供带位置的失败。这些集成检查
补充各 family 的独立回归，不意味着完整 ISA cross-product 或稳定 1.0 API。

## 显式 vector MOV

既有 `mov_v4_u32` / `MovV4U32` 语义 identity 现覆盖 `.v2`/`.v4` MOV，类型为
`b/u/s16`、`b/u/s32`、`f32`、`b/u/s64` 和 `f64`，总宽不超过 128 bit
（因此不接受 `.v4` × 64）。PTX 1.0 baseline 与 f64 SM 13 门槛保留。
这是逐 element-width MOV，不是 scalar bit-aggregate packing。

公共 API 迁移：`MovV4U32::dst` 现为 `WithLocs<ResolvedRegisterVector>`；
`src` 为 `WithLocs<ResolvedMovVectorSource>`。后者区分 `ResolvedMovValueVector`
（有序 `ResolvedMovVectorLane` register/immediate/scalar-special 值，加公共
`ResolvedVectorSource`）与既有 `ResolvedVectorSpecialRegisterRef`。
typed reference callback 为 `IReferenceObserver::mov_vector_source`。
generic `RegOrImm`、`ResolvedValueVector` 与 scalar `ResolvedMovSource` 没有扩宽。

Named/brace source 与 destination 可自由组合，但 arity 必须精确相同，逐 lane
沿用 scalar MOV 同宽类型兼容规则，不使用 memory widening。Named base/lane 的
真实 identity、member、canonical 顺序和完整 identifier range 复用下文完整
named provenance；只有完整 container 才授权 implicit projection。Brace destination
允许 sink，但至少保留一个真实 register，且不能重复物理 destination（alias 同 lane
也算重复）。Source 重复和 source/destination overlap 合法。Brace source 接受普通
scalar/显式 component register、numeric literal/constant expression 和 scalar special
read；每个 special lane 保留真实 registry identity、component、历史 scalar MOV
类型兼容与 PTX/SM/capability 门槛。

Whole hardware vector 仍为独立 width-four intrinsic-u32 read，只兼容 u32/s32/b32
并保留原 availability；不伪造第四个 scalar hardware selector。Sink、predicate、
nested whole vector、address、function 与 symbol 不能作为 source lane。
`mov.v2.u32 V, 1` 等 bare scalar vector source 仍是 frontend 不支持的子集，
不宣称 ISA-invalid 或 broadcast/truncation 语义。Scalar `.b16/.b32/.b64/.b128`
pack/unpack 接受 brace 或总宽严格匹配的完整 ordinary named vector，并保留 B128
PTX 8.3 / SM 70 门槛；floating instruction type 与 scalar-to-scalar B128 仍不支持。其他 family fence 与现代 256-bit
brace memory form 不变。Source 释放及 module copy/move 后，owned validation
重联完整 base/lane 声明与 source owner。

## 具名数组地址

专用 address-source MOV 与通用 address operand 接纳 `A[index]`，包括 LDNC/LDU、
atomic/reduction、cache、async-copy、barrier、tensor-map、response object 与 fabric
本地 memory 地址。接纳策略按 typed binding 决定：明确要求 register-only 的 async
数据地址仍受限制，不影响同一指令的通用 barrier-object 地址。Tensor operand、fabric
handle、texture/surface access 保留独立语法。生成的 source 与 owned 检查采用相同的
role policy；既有 target、state-space、alignment 和 control 规则继续适用。
具名形式不与 `.unified` suffix 组合。已知 unified storage 的 LD 仍需既有
bracket-qualified 路径，ST 仍因只读约束被拒绝；MOV 可以取其地址（包括具名
indexing）而不进行解引用。

`ResolvedAddress::named_index` 拥有整数 index 的 bits/signedness 或精确绑定的
标量 register、可选整数 displacement、源码 operator、标量字节 stride 和 range；
它与原有 byte offset 互斥。`A[1]` 按声明的标量 base element 大小缩放：
u32、v2.u32 和多维 u32 数组均为四字节，u64 为八字节。Instruction data type、transfer
granule 与 opaque object 大小不决定该 scalar stride。array shape 来自按
SymbolId 关联的 owned storage/parameter record；缺少此上下文的 standalone
resolve 明确失败。负数或越界常量不会仅因数组 bounds 被拒绝。

动态整数/bit register 保留声明的位宽与 signedness，包括窄类型和 s64；不做
运行时求值、cast、promotion、截断或假定 sign extension。只有常量贡献先被
检查和缩放，再应用 MOV signed-64 或 memory signed-32 字节域。direct-minus
与 plus-negative 的常量贡献相同，但源码运算保持不同。动态对齐为
`gcd(base alignment, scalar stride)`，再由常量字节贡献降低。owned validation
重新关联声明 shape/register identity，复查缓存 stride/displacement 和 range，
不依赖 Syntax AST 生命周期。

本地 ptxas CUDA 13.3 V13.3.33 探针确认按标量 element 缩放并接纳负数/越界常量。
该编译器拒绝动态 direct-minus 拼写而接纳 plus-negative；frontend 有意同时
接纳两者。这些是编译器观察，不是 GPU 执行结果或运行时位宽/符号规则。

## 指令常量表达式求值与转换

拥有源码树的 constant-expression operand 先经共享 semantic evaluator 求值，
再按数值 use site 转换。整数 source bits 和 signedness 保留至折叠之后；浮点表达式
在 f64 域求值，最终 f32 narrowing 复用 round-to-nearest、ties-to-even 的位转换。
operand-specific narrowing、严格 representability、predicate truth 和 fixed control
均检查求值后的值。symbolic/deferred expression 不能提供指令数值 operand。
call expression argument 在 `ResolvedCallLiteral::source_value` 保留拥有自身数据的
typed numeric source，直到 formal 确定目标 type；释放 AST 后，owned-module
validation 仍重新检查这一转换。CST/AST source identity 保留 operation tree；
Resolved IR 无需新增通用 expression tree。

## 当前验证边界

下文 entry point 契约描述已建模检查，不表示穷尽 PTX 验证。
header/target、storage 和 ABI 检查及其保留边界见[当前覆盖范围](converage/coverage_status.md)。
完整源码上下文不等于 validator 完整；运行时行为仍在本层范围之外。

## 状态与边界

本文描述当前实现的 Resolved PTX IR，而不是一个未来的 CFG、SSA 或后端 IR
设计。frontend 的核心数据流为：

```text
PTX source -> Token stream -> Syntax AST -> symbol binding -> Resolved IR -> checker
```

Syntax AST 忠实保存源码拼写、modifier 顺序和 `SourceRange`；Resolved IR 则记录已经
选定的指令 semantic form、已解析的 operand 值与诊断位置。二者是独立 frontend 边界；
公共 API 兼容仍处于 pre-1.0。
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
`ResolvedIndirectCallee` 为 non-predicate `.reg` indirect target 或已绑定的 function-local
`.callprototype`/`.calltargets` label 提供 descriptor-independent identity；所属
`ResolvedFunction` 另行拥有对应的有序 metadata payload 与 normalized ABI，operand 因而保持紧凑，
但 metadata 不会丢失。final 类 `CallDirect` 现有三个额外的 `IndirectCall` layout
（target/metadata、target/input/metadata、return/target/input/metadata），均要求 PTX 2.1 / SM 20；
normal module indirect call 会保留已绑定的 target 与 metadata identity，并通过 metadata-indexed
canonical signature 复用 direct-call ABI contract，不会创建第二套 indirect-call model。

operand-level `ResolvedIndirectCallee` `std::variant` 另含 `ResolvedCallTableRef`，
其 ID 是真实 storage variable，而非 metadata label。
`ResolvedModule::call_tables` 只拥有被消费的表：scope/owner、declaration provenance、
有序显式 slot 的 byte offset 与 bound/canonical function ID、精确 initializer 和 target-token
range，以及完整公共 signature。省略的 sparse tail 不产生 slot。owned validation 由真实
storage、initializer reference 和 canonical function contract 重建表，关联词法 flist use 与
prior declaration occurrence，拒绝 missing/extra/stale contract 及一致篡改的 cache field。
visitor 暴露 active `ResolvedIndirectCallee` operand alternative；
不保留借用的 AST 数据，也不证明 runtime pointer membership。

活跃的公共入口是
`<ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>`，
聚合手写 foundation、module container 和生成的 final 语义形式类。逐 opcode
窄头位于 `model/<category>/<opcode>.gen.hpp`。model 头提供 owned data 和
只读 descriptor、完整的拥有值诊断与 selector 声明，不要求完整 Syntax AST。
调用独立指令 resolver 时另行包含语法解析头；module 或绑定上下文解析需包含
`ptx_resolved_ir_resolution.hpp`。解析入口位于
`ptx_resolved_ir_resolution.hpp`；检查通过虚函数 `Instruction::check`
及手写支持头 `ptx_resolved_ir_checker_support.hpp` 提供。

公共基础头只前向声明 `Opcode` 和 `InstructionKind`。每个 final form 仍暴露
带类型的 `Form::opcode` 与 `Form::kind` 常量。数值分配集中在各 opcode 的
`identity/<category>/<opcode>.gen.hpp` 中，model 和形式分片头引用其中的具名常量。
宽生成聚合头包含完整具名枚举；
窄 consumer 若直接写 `Opcode::Name` 或 `InstructionKind::Name`，需包含
`ptx_instruction_catalogue.gen.hpp`。8/8/16 位值采用固定 category 前缀和
normalized opcode/form 顺序编号。插入操作可重编号受影响 category 或 opcode
中的后续成员；整数值和生成的 C++ layout 不承诺稳定二进制 ABI。

公共层还提供了一个与具体 opcode 无关的边界：

```cpp
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic>
resolveInstruction(const syntax_ast::AstInstruction& ast);

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveModule(const syntax_ast::AstModule& ast);
```

`ResolvedFunction::body` 是 `unique_ptr<Instruction>` 的 vector。每个已建模
语义形式各有独立 final 类及不可变 kind；活跃 API 没有 opcode owner 包装或
instruction union，选定的小型 operand-layout payload 仍可使用强类型 variant。精确类查询对 final 类使用 `dynamic_cast`，不能仅凭 opcode
身份；module 实现为此提供受约束的私有辅助函数。复制 `ResolvedFunction` 时克隆所有非空指令及全部元数据，保留空槽供校验
拒绝。借用的指针在 vector 扩容与函数移动后仍有效，在指令销毁或替换后失效。
内部 `IReferenceObserver` 回调同步借用强类型 foundation 值及位置 span；校验
期间禁止重入修改载荷。源码位置、symbol identity、Call literal 归一化以及
resolution-only 与最终校验的不同保证继续适用。

Matrix 数据形式通过 `matrix_descriptor()` 暴露不可变的类级 topology；可变 operand
字段保留源码选定的值与位置。可变 logical-form tag 与 `matrix_logical_index` 查找均不属于
此契约。Shared matrix descriptor 保留已绑定寄存器身份，而运行时 bits 仍不透明。
另行提供的 [`TensorMapKnownFacts` 查询](tensor_map_known_facts.md)接收调用方提供的
descriptor 事实。其
`project_tensor_known_access_context(const Instruction&, const checker::Context&)`
适配器复制选定的强类型 TMA access 事实，不保留 AST：非 tensor 形式不返回 access 或
诊断；tensor metadata 损坏时仅返回诊断；target 不可用时保留复制的 access，并把
availability 标为 false。它不解码原始 descriptor 字节。

各模块入口的成功契约明确区分如下：

| 入口 | 成功含义 |
| --- | --- |
| `resolveModuleOnly(ast)` | binding、declaration semantics、指令解析、call ABI/staging 检查通过；不运行末尾的 instruction/directive checker。 |
| `resolveAndValidateModule(ast)` | 解析与末尾检查通过，每个受检区域都有已识别 source target 和 PTX version；缺少上下文时报错。 |
| `resolveModule(ast)` | 保留兼容行为：解析并检查上下文可用的区域，仍接受 targetless fragment。 |
| `validateModule(ast, module, policy)` | source 对应关系和显式策略下的 instruction/directive 检查通过，默认 `RequireCompleteContext`；module 必须已经通过解析。 |
| `validateModule(module, policy)` | 不遍历 AST，只重新验证 owned header、declaration/member identity、control metadata、typed call literal、operand layout 与可用 source-region checker rule。手工构造或修改 public IR 后必须调用它。 |
| `checkModuleAvailability(ast, module)` | `AvailableContext` 策略的兼容包装；虽然历史名称是 availability，实际在有上下文的区域运行完整 instruction checker。 |

resolve-only 仍会在源码提供相关 version/target 时检查声明可用性，不是绕过非法声明的入口。
binding、声明形状/类型规则、operand resolution 与 call ABI/staging 属于解析阶段。
末尾的 generated checker 负责其余指令约束（包括不依赖 target 的 layout/type 关系），
以及 PTX/SM/profile availability。因此 resolve-only 不保证所有 target-independent
指令约束都已通过；全部验证保证均限于当前建模的指令和声明子集。
每条 `.target` 替换 active source context，未知 target 也会清除此前已识别的 target。
函数头、嵌套 body declaration 和局部 call prototype 使用所属函数所在的 source region，
不与 lowering 阶段的 deployment target 选择混用。
显式 validation catalog 包含不具有现代 capability 或 family feature 的历史 `sm_10`、
`sm_11`、`sm_12`、`sm_13` 与 `sm_20`，以一致检查 legacy source context 和 PTX 6.0
的 `sm_20`/`sm_30` 声明边界；不会因此接受任意数字形式的 target spelling。
支持范围限于 catalog target，不代表所有历史 GPU profile。

`ResolvedSourceTargetRegion::map_f64_to_f32` 拥有有效 programmer option，仅从该 region
有序 `target_options` 中的精确 `map_f64_to_f32` spelling 解码。每条 `.target` 都重置
该 option；即使前一区域启用了它，省略仍表示 false。AST-backed 与 owned validation
均把它传给 `checker::TargetInfo::map_f64_to_f32`。owned validation 拒绝 Boolean 与
保留 spelling 之间的矛盾。此 option 独立于硬件 capability，不改变 f64 指令 identity、
storage 或 operand width。PTX 1.4 起，`div.rn.f64`、`rcp.rn.f64` 与 `sqrt.rn.f64`
仅在启用此 option 时接受精确 generic `sm_10`、`sm_11` 或 `sm_12`；既有 `sm_13`
及更高 target 的 availability 无需此 option。其他 f64 form、directed rounding 与
f32 form 保持各自的 gate。前端保存并验证 source option，不执行 lowering 或 emulation。


`RequireCompleteContext` 还要求模块起始依次为 `.version`、`.target` 和可选
`.address_size`，之后才能出现其他 module item。Version 或 address size 不得晚置；
后续 `.target` 仍可开启合法的 source region。只有 header 的模块同样检查，空模块则
缺少完整上下文。`ResolvedModuleHeader::source_order` 保存 typed traversal event，
因此销毁源码和 AST 后仍可检查；缺失或不一致的顺序 metadata 不能证明完整上下文。
`AvailableContext` 与 `resolveModuleOnly()` 保留 fragment 和乱序 header 的兼容行为。
AST overload 在替换 source configuration 时检查传入 AST 的顺序。

`.target` 的每一项均分类：首项必须为 architecture spelling，后续仅允许
`texmode_unified`、`texmode_independent`、`debug`、`map_f64_to_f32`。Parser 和两个
validator 都拒绝未知 option 或额外 architecture 项。词法合法但未识别的 target
仍可解析，在 validation 阶段产生 `UnknownTarget`。每个显式 catalog 中的
`sm_<number>[a|f]` profile 都接受 `compute_<number>[a|f]` 同义形式，architecture、
flavor、enabled family feature 和 capability 相同。词法解析及 owned source region
保留实际书写的 prefix；语义 availability 和显式 checker target 比较通过显式 catalog
rename 对应关系使用 validation-feature architecture 与 flavor。`TargetIdentity::operator==` 仍比较 source spelling，source provenance
一致性检查保持精确。未知 number 或未支持 flavor（包括 `compute_102a`、
`compute_102f`）不会获得 catalog profile。Texture mode 继续检查重复与模块级冲突；
此契约不增加 DWARF 内容检查或浮点指令转换。
源码形式依据归档的 [PTX ISA 9.3 target directive](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)。


两个 validation policy 都检查 header compatibility，包括只有 header 的模块和每个后续
target region。`.version` 至少为 PTX ISA 1.0；显式 `.address_size` 至少要求 2.3，
省略时早期版本仍保留 32-bit 默认值。两种显式 texture mode option 都要求 1.5，
`debug` 要求 3.0。`AvailableContext` 仅延后依赖缺失 version 或 target 的检查，
已有上下文的矛盾仍被拒绝。replacement-AST validation 使用传入 AST 的 header；
owned validation 在 source 销毁后重新检查保留值、option spelling、provenance 和
traversal event。仅修改 owned provenance 不能把显式 address-size event 变成隐式值。

已有 target catalog 是每个精确 profile 引入版本的唯一来源。
`base::TargetProfile::minimum_ptx_version` 暴露 base 拥有的 `PtxIsaVersion` 分量，
独立于 `checker::PtxVersion`。`supports_map_f64_to_f32` 仅对精确 generic SM10/11/12
及其 compute synonym 为 true；其他已识别 profile 即使缺少 version 也拒绝此 option。
此规则依据归档 target 表及 legacy option 继承，并由 CUDA 13.3 `ptxas` 拒绝 SM13 及
更高 target 上该 option 的结果佐证；不增加指令模拟。未知 target key 仍产生
`UnknownTarget`，不猜测 minimum 或 option permission。

固定的 [PTX ISA 9.3 target 表](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
规定以下最低版本（`sm_` 和 `compute_` prefix 相同）：

| 精确 architecture/flavor 后缀 | 最低 PTX ISA |
| --- | --- |
| 10, 11 | 1.0 |
| 12, 13 | 1.2 |
| 20 | 2.0 |
| 30 | 3.0 |
| 35 | 3.1 |
| 32, 50 | 4.0 |
| 37, 52 | 4.1 |
| 53 | 4.2 |
| 60, 61, 62 | 5.0 |
| 70 | 6.0 |
| 72 | 6.1 |
| 75 | 6.3 |
| 80 | 7.0 |
| 86 | 7.1 |
| 87 | 7.4 |
| 89, 90 | 7.8 |
| 90a | 8.0 |
| 100, 100a | 8.6 |
| 120, 120a | 8.7 |
| 100f, 103, 103a, 103f, 120f, 121, 121a, 121f | 8.8 |
| 88, 110, 110a, 110f | 9.0 |

`ResolvedFunction::declaration_scope` 标识一次声明：prototype 和 definition 可以共享
`SymbolId`，但具有不同 scope。binding 保存声明 range，提供 `functionScope(range)`；
解析、storage 收集和声明检查通过此关联查找，不再配对独立遍历的下标。
`instruction_ranges` 与 `instruction_opcodes` 为每条展平指令拥有一条记录；
`source_target` 与 `source_version` 保存原始源码上下文，`source_identity` 拥有不依赖位置的
语法身份，用于检查对应关系。
`ResolvedModule::source_identity` 还覆盖 module declaration、alias 和 address size，
避免改变 global 类型或 initializer 后静默复用另一份源码的指令绑定。

验证通过 `ModuleSourceMismatch` 明确拒绝缺失、额外、含糊或结构不同的函数/指令关联。
仍允许使用另行解析、函数体等价但行号变化并替换 target/version 的 AST；
影响解析语义的 directive 仍须匹配。验证会重新绑定传入 AST 并重复 declaration semantics，
包括新 source context 下的声明可用性。重复等价声明必须能唯一识别对应 occurrence，不会按顺序静默配对。
指令诊断使用 IR 拥有的原始 range，directive 诊断使用传入 AST 的位置。
严格验证缺少上下文时返回 `MissingValidationContext`。

`ResolvedModule::header` 拥有 targetless prefix 以及每个后续 `.target` region 的有效 version、
有序 source target option 与 address-size 值。每个值记录 `Missing`、`Explicit` 或 `Defaulted`
provenance。source target 的顺序只定义 source availability，不是 deployment 或物理后端 target。
省略 `.address_size` 时拥有 PTX 规定的 32-bit 值并标为 `Defaulted`，绝不依赖 host。
invalid、duplicate 或互相矛盾的 header directive 以 invalid range 保留，并由 owned validation
拒绝；`ResolvedFunction::source_region` 选择同一份 owned context。

Function 拥有 normalized signature、linkage、canonical/alias identity、`.noreturn` 与
ABI-preservation contract、numeric resource value、cluster dimension（包括省略尾维推导为一）、
`.blocksareclusters` 与 language value。entry resource 是 source launch contract，不是 occupancy
计算或物理分配。function-local `.branchtargets` 拥有有序展开后的 bound label（`L<2>` 为
`L0`、`L1`，不是两个 `L`），显式重复 label 保留为不同 logical entry；`.calltargets` 拥有
有序 bound/canonical function 及共享 signature；`.callprototype` 拥有
signature 与 ABI/noreturn suffix。上述记录及其 source range 在 AST 销毁后仍有效。

`ResolvedFunctionAttribute::values` 已从 source spelling 存储迁移为可选的 typed
`ResolvedUnifiedId` `unified_id` payload。对 `.attribute(.unified(uuid1, uuid2))`，
`unified_id->upper` 是 UUID `uuid1`（upper 64 bits），`unified_id->lower` 是 UUID `uuid2`
（lower 64 bits）；不发生 byte-order 或 host-address conversion。function attribute 与 storage
declaration 使用同一个 named value type。malformed source UUID token 仍保留为 declaration
diagnostic，而 AST-free validation 会拒绝缺少该 typed payload 的 `.unified` attribute。

成功的 direct、alias 或 metadata-backed call 会将 module literal 保存为 formal-driven 的
`ResolvedImmediate`。没有 module call contract 的 standalone instruction 可保留没有 value 的
`ResolvedCallLiteral`；consumer 不得猜测 type。已声明的 external module call 仍保留 signature
与 formal-typed literal；实际 linking 或 relocation 仍延后处理。resolved/binding data 使用
`base::DeclarationStateSpace` 与 `base::LiteralCategory` 表示语义值；旧有
`AstStateSpace`、`AstImmediateKind` alias 继续保持 source compatibility，但不要求保存 Syntax AST。

instruction range/opcode 与全部 owned record 都是 semantic provenance。frontend 保留 `.language`
以及 function ABI/resource contract；`.file`、`.loc`、`.section`、`.pragma` 属于 syntax/debug 或
advisory metadata，不作为可执行 resolved payload；owned symbol table 另保留 `.file` 声明 payload 以验证 debug identity。generated C++ struct layout 或 binary ABI 不保证稳定。
其他原始 module directive 仍需要 AST；owned model 是已声明 subset 的 semantic handoff，不是完整源码
序列化契约。

`ResolveDiagnostic` 拥有 message 和 source range 的值。模块解析保留原阶段的
`binding_kind`、`declaration_kind` 或 `checker_kind`，以及主位置 `range` 和 binding
或 declaration semantics 提供的 `previous_range`。`stage()` 由类型化类别推导出
`Binding`、`DeclarationSemantics` 或 `Checking`。转换而来的诊断恰有一个类别字段；
resolver 原生错误的三个类别字段均为空，阶段为 `Resolution`，目前没有更细的错误码。
调用者无需解析 message，即可检查转换诊断的原始类别与关联位置；源码和 AST 销毁后
这些信息仍然有效。诊断顺序和原有提前返回边界不变。既有 aggregate 字段保持原顺序，
新增可选类别字段追加在末尾。

`resolveInstruction` 根据指令数据库生成，分发到 `resolveAdd` 等逐 opcode
函数并构造精确的 final 语义形式类。调用者无需手写 opcode 分派。
`resolveModule` 先建立
`SymbolTable`，再为每个 function scope 构造显式 `ResolveContext`；返回的
`ResolvedModule` 拥有 symbol table，`ResolvedFunction` 以函数 `SymbolId` 标识。每个
function 以唯一的 `parameter_declarations` 表拥有通过验证的 `.param` 声明。
按 `ParameterDeclarationRole::EntryInput` 筛选即可按源码顺序取得 entry header input。
`ResolvedFunction::label_positions` 以已绑定的 `SymbolId` 和 source-order 的 instruction
boundary 记录每个 function label；boundary 基于递归展平的 body，首条 instruction 前为零、
连续 label 共用一个 boundary、末尾 label 为 `body.size()`。standalone `resolveInstruction`
与逐 opcode resolver 不要求声明上下文，继续服务单指令工具。directive 与 declaration 仍由
Syntax AST/symbol table 保存，不复制成未解析的 Resolved IR 字符串字段；刻意保留的
例外是拥有值的、已规范化 parameter 与 storage metadata。owned symbol table 另按源码顺序
保留 `.file` 声明的原始 filename、可选数值 metadata token 及 range，在 source/AST 销毁后仍可用。
两个 validation overload 均重新检查规范化 index 唯一性、首次 symbol association、payload
边界、声明覆盖与 basic/inline `.loc` file-reference target。replacement source 独立重新绑定
其 debug metadata，合法的替换 filename 或 index 不必与原始内容相同。`.debug_str` identity
也验证 `.loc` metadata；`.file`、`.loc`、`.section` 与 `.pragma` 不产生可执行 Resolved IR node，
也不附着到 instruction。

存在的 `file_size` 是经过检查的 unsigned64 单个整数 token，包括零（unavailable）与
UINT64_MAX。Decimal、前导零 octal、binary、hexadecimal 及可选 `u`/`U` suffix 复用
既有 checked integer-magnitude decoder；overflow、符号、浮点值、表达式与非整数叶
均拒绝，不做截断。仍拥有原始 text/range 与缺省状态，不增加 decoded-size cache。
Current symbol table 与 supplied/rebound 的 size 在可选 context 门槛之前独立检查；
合法 replacement metadata 不能遮蔽非法 current data，而各自合法但不同的 `.file`
dataset 仍保持 debug-neutral source identity。Timestamp 独立的 `time_t` domain，以及
既有 index、filename、optional-pair、range 与 reference 规则均不变。

`ResolvedModule::pragmas` 按源码遍历顺序拥有 `ResolvedPragma`：`placement`
（`Module`、`EntryHeader` 或 `Statement`）、`scope_id`、拥有式 raw `AstSyntax`
string leaf 与整条 directive 的 `range`。Module 使用 module scope；header 使用实际
entry declaration occurrence；statement 经 lexical block 到达实际 function definition，
不使用 canonical symbol 的首个 prototype scope。Pragma string 不创建名字，也不定义
frontend backend/loop/execution 解释。空的 quoted content 合法；保留的空 list、非 quoted
leaf、非法 scope/placement 或 whole/leaf range 均拒绝。已知 current 与 supplied module
range 各自包含自身 record；全零 owner range 延后 containment。检查 list 内顺序，但不排序 record
或将其与历史文本关联；grouped spelling/range 不替代 CST comma/trivia 的精确序列化。

Copy、assignment 与 move 将整个 vector 和 module table 一同拥有；function-only copy
不会取得 module advisory metadata。两个验证 API 均对 current record 做 intrinsic 检查，
supplied AST projection 独立检查并采用其版本执行 PTX 2.0 门槛；合法 neutral replacement
可删除 pragma 并降至 PTX 1.5，不受旧 presence 否决。AST-free availability 使用当前
module version。缺少 target 也不跳过已知旧版本；只有未知 version 在 `AvailableContext`
中延后。合法 replacement 不能遮蔽非法 current data，反向亦然。此 public layout 新增
要求 consumer rebuild；显式 public `ptx_syntax_sections.hpp` value dependency 提供
`AstSyntax`，module-only header 仍保持 `AstModule` 与 `AstInstruction` incomplete。

显式 `.file` timestamp/file-size metadata（包括 `0, 0`）要求 PTX ISA 3.2 或更高版本；
普通 `.file` 不受此门槛限制。此检查基于可选字段是否存在，不要求 SM 或 debug option，
即使没有可识别 target，已知的旧版本也会被拒绝。`AvailableContext` 在 version 缺失时延后检查；
严格验证沿用现有的上下文完整性要求。owned validation 使用保留的 header version 与 file record；
AST-backed validation 使用传入 AST 的 version 及独立重新绑定的 file metadata，
因此替换 metadata 与 version 会一起检查。参见 [PTX ISA 9.3 §11.5.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-file)。

`ResolvedFunction::parameter_declarations` 拥有通过验证的 `.param` 声明：先按列表顺序
保存 return formal，再保存 input formal，最后按词法遍历顺序保存 body-local declarator
（含嵌套 block）。每个 `ResolvedParameterDeclaration` 保留 `symbol_id`、`scope_id`、role
（`EntryInput`、`DeviceInput`、`DeviceReturn`、`BodyLocal` 或 `LegacyEntryInput`）、基础 `scalar_type`、有效字节
`alignment`、`explicit_alignment`、`vector_width`、从外到内的 `array_extents`、经溢出检查的
`byte_extent` 与可选 pointee property。scalar 的维度列表为空；受支持的 unsized device input
array 仅外轴为空，保留并检查已知内轴，byte extent 为空。所属 function 与 symbol table 即使在局部同名遮蔽时仍
保留词法 identity。Source text 与 Syntax AST 销毁后仍可检查这些值。Non-pointer 没有
pointer property；未写 pointed state space 的 pointer 为 generic，省略 pointee alignment
时默认为四字节。该唯一 parameter table 不包含 `.reg` formal 或 `.callprototype`
signature。声明验证、unsupported form、版本边界及迁移契约见
[参数覆盖矩阵](parameter_declarations.md)。这些字段均不描述参数打包偏移或运行时分配。

module resolution 还负责不能放入 generated single-instruction checker 的 direct 与 metadata-backed
indirect-call ABI 以及
call-context 工作：它取得 canonical prototype/definition signature，检查 return/input actual
和按 formal 定型的 literal，并执行 function-local `.param` 的 qualifier、predicate 与 staging
adjacency 约束。generated checker 仍只负责一个 resolved instruction 及 target-aware descriptor
规则。

完整 module 的 direct call 要求实际绑定的 device-function SymbolId 在调用前已有
declaration/definition。共享 source/owned pass 通过保留的 lexical event 与 declaration
occurrence 检查 call 内唯一、正坐标、非空 target range；未知或歧义 order 会拒绝。
兼容的先行 prototype 与现代 self-recursion 合法；alias 使用自身 identity 的可见性，
不借用更早的 canonical target。Copy/clone 后的 owned call 可在该 event 改指向另一
兼容且已声明的函数，不按 diagnostic spelling 重联；later-only target 与 malformed
location 拒绝。Source association 独立检查。Instruction fragment 保留 partial-context
解析，indirect register/metadata/table call 继续使用各自规则。

`<ptx_frontend/semantic/ptx_function_contract.hpp>` 提供不依赖 Syntax AST 的 canonical
function-signature contract。其 parameter contract 使用 semantic state-space 与 pointer-space
enum、`ScalarType` 和 typed vector shape，以及只为诊断保留的 invalid spelling；数值字段是
optional tagged value：omitted、已验证 constant 或 invalid structural key。direct-call ABI
直接消费这些 normalized value，不再重解析 alignment text 或旧 array-extent string protocol；
invalid structural data 绝不会被静默替换为 default。

call-staging 邻接性按 owned 执行指令序列检查。变量/debug directive、metadata 声明、
裸 label 与词法 brace 均透明。嵌套 block 中的 transfer 可使用仍可见的同一 outer
object；不同 shadowed 声明或超出作用域的 actual 仍非法。任何插入的执行指令都会
打断邻接性，transfer 不可带 predicate，call 本身仍可带 predicate。解析及 AST-free
validation 从 byte-address 与 named-array 形式（包括 symbolic index）推导相同的
绑定 object/member identity；offset 不改变 call 必须使用的本地 object。本检查不证明
运行时 index bounds、逐字节初始化覆盖或任意 incoming control path 的正确性。

Parameter record 也保留 fundamental `.f16x2` 与合法 `.param` v2/v4 shape，
包含 lane-aware extent、自然 vector alignment 和 AST-free padded entry limit。
[参数声明 contract](parameter_declarations.md) 区分 packed scalar dtype、显式 vector lane
与 numeric packed-literal reference gap。

owned symbol table 还保存有序 `.loc` 事件，不生成可执行 instruction，也不在 function
内复制 payload。共同 validator 分别检查 current owned records 与重新绑定的 replacement
records。`inlined_at` triple 必须匹配 module lexical 顺序中更早的 **base** triple；
当前 base 在检查其引用之后才加入。nested block、function 边界与 target region 不重置序列。
物理 range 只检查归属及单项字段顺序，不用于事件排序。记录依实际 function-definition
occurrence scope 关联 body，而非 canonical function 最早的 owned scope。
两个 dataset 都必须自身合法；不同的合法 debug dataset 仍符合 debug-neutral source identity。
数字 triple 以 unsigned magnitude 比较，支持不同 radix/suffix、零 line/column 与完整
uint64；这是 frontend capacity，不是 DWARF 位宽声明。file index 必须独立绑定 `.file`，
function name 必须绑定当前 typed `.debug_str` identity。basic `.loc` 要求 PTX 1.0，
inline attribute 要求 PTX 7.2，无架构下限。缺少 version 时 `AvailableContext` 可推迟
availability 判断，但不能推迟 numeric、identity 或 precedence 错误。
这不表示 instruction attachment、解码 line table 或 string-address execution。

私有 iterative SCC check 在每次 validation 重算 current canonical local-definition
call relation。Direct cycle 是 syntactic recursion；依赖实际 consumed finite target-set/table
alternative 的 cycle 是 statically possible recursion。每个参与 caller 的 selected context
均需已知 PTX 2.0 / SM 20；source-aware 以 supplied context 检查同一 current owned graph，
原始 acyclic AST 不能遮蔽 coherent current callee edit。Copy/clone 不保留陈旧 graph state。
Call identity/ABI/table join 先于建图，edge collection 不遍历 non-call instruction 的 lane。
不保留 public graph/cache。External/prototype/dynamic open edge 不提供 whole-program
acyclic 证明，`AvailableContext` 延后也不是 acyclic certificate；不建模 CFG 或 runtime
stack/linker execution。

Canonical symbol、storage occurrence record 与 function contract 中的 current weak
linkage 对已知版本要求 PTX 3.1，不依赖 target context。每个 Weak symbol（包含
symbol-only function）与 storage record 使用既有全部 region 版本相等约束保证的 common
module version；function occurrence contract 仍使用各自准确的 selected region；function contract 或 canonical symbol
任一 weak fact 均足够，包含未使用 prototype。Source-aware validation 在 supplied context
下检查 current fact，并独立检查每个 supplied weak occurrence；weak qualifier 仍是
nonneutral source identity。只有缺少 version 可在 `AvailableContext` 中延后，strict context
与 unknown-target 诊断保留。Copy 保留现有 typed fact；不新增 cache-equality、历史 qualifier
registry、public layout 或 linker simulation。

Current retained `.branchtargets` 与 `.calltargets` 均要求 PTX 2.1 / SM 20，
包含未使用和 nested declaration。它们复用下面的 minimum-only 机制：AST-free 使用
准确的 selected-region context，source-aware 对 current record 和独立 supplied typed
node 使用 supplied active context。缺少 full context 时也拒绝已知失败的 prerequisite。
Target-list payload 仍属于 nonneutral source identity；不新增 CFG、execution、public layout
或 member/signature policy。

即使 unused 或 nested，current retained call prototype 也会检查 availability。
base 要求 PTX 2.1 / SM 20，`.noreturn` 要求 PTX 6.4 / SM 30，两个 ABI-preservation
suffix 均要求 PTX 9.0 / SM 80。AST-free validation 只采用 function 已验证的 selected
region；source-aware validation 以 supplied active context 分别检查 current prototype
字段与独立 supplied prototype syntax，version/target retarget 不增加历史 payload 限制。
这四个 conjunctive minimum-only rule 分别判断已知 version 与 recognized profile：
即使另一事实缺失，已知失败仍拒绝。`AvailableContext` 只推迟未知 prerequisite；strict
missing-context 与 unknown-target 诊断保持。每个 target event 清除此前 recognized
profile，不猜 version/target number，也不引入 generic partial-DNF 解释。
普通 function/instruction availability 保留 full-context gate；count、variant、identity
与 signature 合同不变，不证明 physical register preservation。

## Named memory-vector source

普通 `ld`/`st`、`ld.global.nc` 和 `ldu` 的 data operand 在既有 brace 外接受普通
named `.reg .v2`/`.reg .v4`。声明宽度必须等于 arity，实际声明总大小不超过 128 bit；
memory equal-or-wider element 规则不变。现代 256-bit vector、async store、
atomic/reduction、matrix 与 texture operand 保留各自的 brace 限制。
scalar bit pack/unpack 也按下述 exact aggregate-width policy 接受 named ordinary vector。

通用 checker 边界要求 effective scalar `Register` operand 的
`ResolvedRegisterRef::vector_width` 为空；完整 vector 不可冒充 scalar。显式 component
仍为 scalar：effective width 为空，component metadata 保留所属声明宽度。真正的
`Vector` operand 仍通过专门检查保留 whole base 与有序 lane provenance。AST-free
copy 与 instruction clone 同样适用这些规则。

`ResolvedRegisterVector::elements` 保留各 lane；owned `ResolvedVectorSource` 区分
`BraceList`/`NamedVector`，保存完整书写范围，仅 named form 拥有真实 whole
`WithLocs<ResolvedVectorRegisterRef>`。有序 lane 共享其 SymbolId、parameterized
member 和声明 element type，使用 `NamedProjection` origin、canonical lane 与实际
声明宽度；有效 `vector_width` 为空，没有 selector。base/lane 位置都复用准确的 whole
identifier range，不伪造 `.x` 或 selector range。

只有完整合法 named container 加明确 opt-in descriptor 才授权投影；scalar/brace
consumer 仍拒绝。Reference traversal 包含 wholebase 与每个 lane；AST-free owned
validation 重联声明和来源 owner。实际 brace 总保存 fullrange；历史手工 standalone
brace 可省略，但提供的畸形范围必须拒绝。Owned validation 要求完整 provenance 与
instruction/lane containment，named provenance 始终不可省略。Data-source opt-in
data-source admission 与通用 array-address role policy 独立。不宣称 GPU 执行或
assembler parity。

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
`ResolvedImmediate` 保存 use-width bits 和 `ScalarType`。integer form 还会
保留求值后的 64-bit source bits 与数值上的 signed-negative 性质，因此
fixed-control checker 不必重新解释 literal 文本，也不会信任已经窄化的值。
原 integer source bits 存在时，在确认 use-width bits 与 source 一致之后，true negative
flag 要求原 word 的 bit63 为1。Bit63 为1且 flag 为 false 可以表示 unsigned source。
负的原始值可以窄化为正的 use-width bits；destination type 与窄化后的符号均不能重建
source signedness。没有 source cache 的 scalar legacy input 保留既有行为。

`AstImmediateKind` 保留 lexer 对 literal 的分类。二进制 `0b`/`0B` 拼写保留为 `BinaryInteger`。整数 decimal/octal/binary/hex（包括可选 `U`
后缀）先在 PTX 64-bit signed/unsigned source domain 中求值；unary minus 保留该 source
type，而 unsigned negation 按该宽度回绕。ordinary data use 随后保留 target width 的低位。
`WARP_SZ` 是 source 定义的 signed integer constant `32`，也可用于普通
instruction-immediate position；它不是对 target physical warp width 的查询。generated
operand descriptor 为每个 semantic use 独立选择截断或严格 target-width
representability；fixed scalar type 只表达 provenance。generated range、exact-value 与
multiple-of control 比较保留的 source bits，而无约束的 control 显式选择严格 conversion。
按 formal parameter type 检查的 call literal 与 address offset 继续执行严格的 target-width
representability 检查。signed `-0` 在数值上是 zero，而 floating negative
zero 保留其 IEEE sign bit。decimal float 支持转换至 `F32`、`F64` 及等 source width 的 `B64`；
单个 leading `+` 只在 decimal decoding 时规范化，而 leading `-`、signed zero 与
exponent sign 保持通常的 floating semantics。exact-F32 `0f` literal 拒绝括号与两个 unary sign，包括 lexer 合并的 signed spelling；
exact-F64 `0d` literal 通过公共 numeric conversion 接受 unary sign 并保留 negative zero。
`0f<8 hex>` 与 `0d<16 hex>` 分别作为 `F32` 与 `F64` 的原始 IEEE bit pattern。
原本允许 constant 的 scalar data slot 也接受 unsigned direct exact-F32
进入 `B32`，以及 decimal/exact-F64 literal 或受支持的 binary64 expression 进入 `B64`。
这一规则依据 source precision：exact-F32 进入 `B64`、binary64 source 进入 `B32`、
floating source 进入 `B8`/`B16`/`B128` 均被拒绝。typed immediate 保留 bit destination 与
raw payload，不伪造 integer-source provenance；owned call literal 重验保留 lexical
source kind 与 spelling。integer-only control 和 register-only slot 仍遵循各自 role
要求。exact-F32 operator 仍被排除，包括未选中的 branch。
其他浮点格式需要其明确的量化规则后再加入，不能静默按整数处理。

`ResolvedRegisterRef` 拥有完整源码拼写与 `ResolvedRegisterClass`。在 module resolution
中，它还保存 declaration `SymbolId`、可选 parameterized member index 和声明
`ScalarType`；因此 named register（如 `%tmp`）与 `name<count>` member 都有稳定身份。
numbered-register index 仍只是可选便捷属性，不能单独充当身份。普通 `.xyzw/.rgba`
选择保留相同 base 身份与 parameterized member，并拥有 `ResolvedRegisterComponent`：
canonical lane、实际声明宽度、base spelling/range、完整 component range，以及书写的
selector spelling/range。有效 shape 是 scalar，因此 `vector_width` 为空；
`declared_type` 仍是声明的 scalar element type。`same_register_storage` 比较
base/member/lane，故 `V.x` 与 `V.r` 同址，而 `V.x` 与 `V.y` 不同。
Scalar 与 brace 校验要求合法 `ExplicitSelector` 来源，并同实际 operand 位置关联。
`NamedProjection` 元数据不由这些 source form 接受，也不伪造书写 selector。
Owned validation 重新关联 base identity、声明 shape/type 与源码范围。Call、address
base 与 named-array index 不接受新增 component 语法或移植的 component reference。
Fabric handle 保留独立的 identifier-only 语法，拒绝两种 component origin。
Hardware component 仍是独立注册表 domain，只保留既有 selector。
无 binding context 的
standalone resolver 保留旧边界：只接受 numbered register，并令 symbol/type 字段为空。
Owned-module validation 会按结构遍历生成的 operand reference，包括寄存器备选值和嵌套
operand 容器。每个已绑定寄存器缓存的 scalar type、vector width 与 register class 都会
同 owned declaration 比对。public IR 的绑定被修改后，不能靠旧的寄存器元数据通过校验；
诊断优先使用 operand 自有位置。
同一结构遍历也会将每个已绑定地址符号缓存的声明类型与保证的地址对齐，同其 owned
declaration 精确比对，包括无对齐值的情况。operand 不能虚报更强的对齐，也不能在
改绑其他符号后沿用旧元数据。
instruction 的可选 execution predicate 作为 opcode 外层公共字段
`std::optional<WithLocs<ResolvedPredicate>>` 保存；module resolution 要求其绑定到 `.pred`
register，standalone resolution 则接受 numbered `%pN`。`ResolvedBranchTarget` 同样区分两种
边界：module resolution 保存当前 function label 的 `SymbolId`，standalone resolution 保存
源码 spelling 而令 identity 为空。

公开 IR 重新校验会独立于所选 opcode variant 与 operand layout 检查 execution predicate，
包括没有 operand 的指令。已知的非 predicate register class、非 `.pred` declaration type
或 vector shape 均非法；standalone 缺失的 declaration metadata 仍保持 unknown。
Owned-module validation 还会依据 function 的自有 symbol table，要求 guard identity
实际指向 scalar `.reg .pred` declaration，而非信任缓存的 register metadata；合法的
register-valued function formal 与 parameterized predicate-register member 同样适用。
普通与否定 guard 采用相同约束。诊断优先使用 guard 保留的位置，否则回退到 instruction
range；这些检查都不要求原始源码或 syntax AST 继续存活。

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

`mov.pred` 使用独立 variant，因为 destination 是 `ResolvedPredicate`，而 source 是
`ResolvedPredicateSource`，与分类后的 scalar source 结构不同。module resolution 要求 destination 绑定到未取反的 `.pred` register，但接受 plain 或
negated predicate source，并保留其 negation 与稳定 `SymbolId`。source 也可以是保存规范化
Boolean 值的 `ResolvedPredicateConstant`，或同时保留 special-register reference 与
negation 的 `ResolvedPredicateSpecialRegister`。整数常量以非零为 true，再应用可选的
`!` 取反；常量不伪造 `SymbolId`。SETP 使用的 canonical `pred_source` shape 接收
predicate register 和整数常量及其取反形式；MOV 的 `pred_or_sreg` 还接收 predicate
special register。两种 shape 都不接收浮点常量。standalone resolution 仍接受无需声明
上下文的 numbered predicate register。

Predicate constant、logical `!` 与 WGMMA scale-d numeric control 必须先具有 integer
result source，再进行 truth/range conversion。`B64` conversion carrier 不证明 integer
origin：decimal/exact-F64 literal 与 floating-valued expression 在这些 role 中仍不合法，
即使 payload 为零或一。返回 integer 的 floating comparison 及对该 integer result 的
`!` 仍合法。等宽 floating `B64` bit-data operand 保留独立 compatibility rule。owned
predicate constant 仅保存 canonical Boolean；validation 不重建 normalization 已抹去的
source syntax。

`MovScalar` 负责 scalar 动态 type modifier：`.b16/.u16/.s16`、
`.b32/.u32/.s32/.f32` 与 `.b64/.u64/.s64/.f64`，并保留既有的
`.b16/.b32/.b64` pack/unpack layout。`MovB128PackUnpack` 是具有 fixed `.b128` 的独立生成
public variant，只用于 vector pack/unpack。`ResolvedRegisterVector` 保存 2/4 个可选
`ResolvedRegisterRef`，空元素表示 destination-only `_` sink。resolver 与 checker 都要求
bit-size instruction type、vector 总位宽等于 instruction 位宽，并拒绝 source sink、全 sink
destination 与 sub-byte element。`MovB128PackUnpack` 携带 PTX 8.3 / SM 70 availability。

既有 aggregate layout 在 brace 外接受普通 named `.reg .v2`/`.v4` vector；arity 来自
已绑定声明，而非隐式 MOV modifier。named source metadata 保留真实 whole base 与
canonical `NamedProjection` lane，identifier range、declaration type、`SymbolId` 和
parameterized-member identity 完全一致；不伪造显式 component selector 或 brace syntax。
layout selection 仅在 descriptor 授权的 aggregate/scalar alternative 处使用 declaration
shape。unknown name 与 context-free input 不会被推断为 named aggregate；既有 scalar
行为与诊断保留，包括无需声明的 scalar symbol-address source。width/type 与 target error
不会重试其他 layout。scalar 与带 vector modifier 的普通 vector MOV 保留既有 layout。
source form、opcode、type 与 source identity 在 owned module copy 和 instruction clone 后保留。

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
`ResolvedSymbolRef` 的 variant。已绑定的 register base 必须是宽度不超过 64 bit 的
integer 或 bit-size declaration：floating declaration 和 `.b128` 会在投影至 checker
之前被拒绝。这既保留了 PTX 对较窄 integer/bit declaration 的地址 extension/truncation，
也不会把已知的 floating register 当作未知地址。没有 declaration 的 standalone resolution
缺少类型事实，因此仍将 address base 延后处理。其可选 offset 保留加减 operator 和解析后的
magnitude。带方括号的 memory address 使用 PTX 的 unsigned 32-bit immediate base，以及在
应用该 operator 后的 signed 32-bit offset domain：`-2147483648` 由 subtraction magnitude
`2147483648` 表示。未加方括号的 `mov symbol+offset` 保留 symbol-address 与 relocation
consumer 使用的独立 signed 64-bit addend domain。共享 IR 仍将 offset magnitude 保存为
signed 64-bit value；32-bit 规则是 source form 的 legality check，而不是对 relocation domain
的缩窄。32/64-bit integer 或 bit-size `mov d, symbol+offset` 使用未加方括号且限定为
addressable data-symbol 或 formal-parameter base 的地址值；
scalar 与 braced-vector `ld`/`st` 要求方括号解引用，覆盖 register、immediate 与
bound-symbol base。每个 opcode 使用 `GenericScalar`、`ExplicitScalar`、`GenericVector`
与 `ExplicitVector` variant；runtime type field 接受 `.b8/.b16/.b32/.b64`、
`.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64` 与 `.f32/.f64`，并包含 `.b128`；selected
`.b128` value 要求 PTX 8.3 / SM 70，`.b128` 搭配 `.sys` 要求 PTX 8.4。vector variant
额外要求 runtime `.v2/.v4/.v8` field，register-vector operand
descriptor 将期望元素数链接到该 field，而不是按 arity 复制 variant。memory vector 使用
element type policy：每个 register element 都按 instruction type 检查，允许
`EqualOrWider` register width。legacy payload 最多 128 bit；generated cross constraint
另加入 PTX 8.8/SM 100 的精确 256-bit `.v8` × 32-bit 与 `.v4` × 64-bit form，地址已知时
要求 global，并允许部分 sink。默认 register-width policy 为 `SameWidth`，
保持 `mov/add/sub`、immediate 与 special-register 的既有行为；`ld` destination、
`st` source register descriptor 及适用的 `cvt` data operand 选择 `EqualOrWider`，
因此声明 register 位宽可大于等于
instruction type。通过 size 检查后，任一侧为 bit type 即兼容，fundamental signed/unsigned
integer 互相兼容，float 只接受 exact type/size，integer/float 仍不兼容。这同时覆盖声明
register 最宽为 `.b128` 的 wider load destination、store source（包括 store truncation）
与适用的 conversion operand，遵循 PTX ISA 9.3 Tables 27/28。中央 declaration semantics
将 `.b128` 归类为 fundamental，同时拒绝 instruction-only declaration format 和超过
128 bit 的 register vector。声明资格不会绕过 instruction、format、PTX-version 或 SM
gate：selected `.b128` memory instruction 仍要求 PTX 8.3 / SM 70。
无 size suffix 的 `isspacep` 地址 source role 使用 fixed u64 use type 与
`WordOrDoubleword`：普通 B/S/U32、B/S/U64 register 均合法，不要求与 module address
size 相等；更宽、floating 和 packed container 仍被排除。此有限 address-role 规则
不限制通用 data carrier。
既有 `mov` vector consumer 的 exact `.b128` compatibility 不受影响。当前 memory subset 见
[LD 覆盖](converage/ld_coverage.md) 与 [ST 覆盖](converage/st_coverage.md)。

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

普通 coherent `ld`、`ld.global.nc` 与 `st` 的 `.L2::cache_hint` 可以带或省略
末尾的 64-bit register `cache_policy`；未写 hint 却提供 policy 仍非法。现有 exact
form class 与 `InstructionKind` identity 保持不变：原有 policy-present `default`
layout 仍为 index 0，新增 `without_policy` 为 index 1。生成的 public
`cache_policy` member 现在是 `std::optional<WithLocs<ResolvedRegisterRef>>`，
consumer 通过 `cache_policy->value` 访问已提供的 policy。两种 layout 的固定
`cache_hint` field 都保持 true，并保留相同的 payload、address、modifier、PTX
version 与 target 限制，包括 modern-vector gate。
`examples/memory_cache_hint_consumer` 的 installed example 在 source text 与
AST 销毁后验证 scalar/vector coherent load、noncoherent load 与 store 的两种 layout。

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

每个 final 形式的虚函数 `Instruction::check` override 使用公共 checker 检查：

- 每个 projected dynamic modifier value 是否属于已选 variant 生成的 semantic domain。此检查
  不依赖 source location 或 modifier 在源码中是否出现：省略 optional modifier 时检查该字段声明的
  default；越出 domain 的编辑后 value 即使没有 provenance 也仍然非法，诊断 range 回退至
  instruction range。
  `ModifierValueDomainMismatch` 表示 value 不在该 domain 内。
- variant、已选 operand layout 与实际 modifier value 的最低 PTX 版本、SM 版本与 target family；
- layout tag 的范围；
- layout tag 与必需/禁止 optional 字段的一致性；
- operand 字段 ID、resolved shape，以及由结构化 descriptor 约束的 immediate 或已绑定
  register 声明类型。
- special-register intrinsic 元数据，以及由当前 instruction width 选择的上下文类型兼容与
  availability；该选择只产生临时检查视图，不改变 Resolved IR。
- static generic state-space allowlist、explicit modifier-derived constraint 与已知
  bound-symbol effective address space 的匹配，并检查 allowlist entry availability；
  register、immediate 与 standalone base 的未知 space 不推断。
- 由 generated operand constraint 描述的 explicit `.param` input/return direction 与
  function-context availability；方向错误优先于上下文 availability。

调用单条 instruction 的虚函数 `check` 时，由调用方提供 `checker::Context::target` 与
`instruction_range`；编辑字段没有保留 source provenance 时，后者是稳定的 diagnostic range 回退。

semantic-domain membership 与 target availability 是两个独立问题。domain 由 normalized variant
modifier values 及各 optional field 自身的 default 得出，不从 availability entry 推断，也不会笼统
接受 enum sentinel。availability 保持现有 source-presence 行为，因为省略的 default 不必与显式
spelling 具有相同的 PTX 或 SM 要求。因此 legal value 可以通过 domain membership，但仍因 target
availability 被拒绝。

生成的 vector projection 可接收调用方手工构造或修改的公开 IR，无需另行预验证
向量长度。`OperandView::vector_arity` 保留原始 width/元素数量，对固定容量元素数组
的写入始终受容量限制。公共 checker 在读取元素前，以 `InvalidVectorOperand`
拒绝零长度或超过容量的向量；容量内的数量仍须通过指令自身约束。超长 payload
不会因窄化或截断而变成合法向量。这项保证仅涵盖向量投影长度，不代表覆盖全部
非法 IR invariant 或跨指令约束。

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
`submod/resolved_ir/include/ptx_resolved_ir_checker_support.hpp` 与生成的
`ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp`。

direct-call ABI、function-local call-argument `.param` memory、带限定的 `::entry`/`::func`
form，以及 call adjacency/predication constraint 均由 module resolution 覆盖。indirect-call
metadata 与由生成的 `MovScalar` type domain 拒绝的 scalar `.b128` 仍不在本切片范围内。
wider `.b128` carrier 使用中央 register declaration rule 与所选 instruction 的 width policy。legacy scalar/vector `ld/st` cache operator、PTX 8.8 modern memory vector、static
memory-address alignment 与 memory consistency qualifier 已纳入本切片。

## Owned Video 操作数

`ResolvedVideoOperand` 拥有带位置的 `RegOrImm`、可选带位置的 `VideoSelector` variant、书写的寄存器取负 Boolean 及可选 minus range。Identifier 位置与外层 selector/negation range 分离，用于绑定诊断。Source 数组保留书写的高到低 digit 顺序，索引连接后的 A+B carrier。`video_default_selector` 和 `video_effective_selector` 提供默认值，不虚构书写来源。生成 form 公开静态 `video_lanes`/`video_operation`。`video_mad_interpretation` 独立于书写 dtype 推导 product/input-C/final 符号；负数常量不变成寄存器取负控制。公开 helper 位于 `<ptx_frontend/resolved_ir/ptx_video.hpp>`。Checker operand view 仅在同步检查期间借用 owned Video payload；模块引用遍历使用内部 carrier 的 identifier 位置。


## 转换指令的 special-register source

普通 `cvt`（PTX 9.3 第 9.7.9.22 节）拥有 `ResolvedCvtSource`，仅包含
register、immediate 与 special-register。标量读取位置保留原有的 immediate 接受范围；
destination、向量 register source 与 `cvt.pack` 保持原有契约，不接受 symbol/address value。
checker 在读取 metadata 前验证 intrinsic identity、索引族边界与 component 选择；
展示用 spelling 不决定语义。special source 应用 operand 的 register-width policy，
包括较宽整数声明的截取，integer/float 不兼容规则仍然生效。CVT 按 source 使用宽度
声明与 MOV 相同的历史 16-bit thread/grid component 与 16/32-bit grid-ID 读取兼容性；
intrinsic identity 与当前 metadata 保持不变，有效类型与 availability 仅作用于当前检查。

## 历史浮点控制省略形式

`1.0 <= PTX < 1.4` 支持十三个保留 source omission 的 exact form：
`div.f32`、`rcp.f32`、`sqrt.f32`、`rsqrt.f32`、`sin.f32`、`cos.f32`、`lg2.f32`
与 `ex2.f32` 的解释为 `FloatingPointDefault::ApproximateFlushToZero`；
`div.f64`、`rcp.f64`、`sqrt.f64` 与 `mad.f64` 为 `RoundNearestEven`；`rsqrt.f64` 为无 FTZ 的
`Approximate`。对应 final class 暴露 `inline static constexpr floating_point_default`，
没有 source mode/rounding/FTZ member；DIV 也省略 `.full`。resolution 不插入 explicit
modifier，也不改写 opcode。

FP32 form 保持既有 all-target rule。FP64 form 要求 SM >= 13；`div.f64`、`rcp.f64` 与
`sqrt.f64` 额外接受启用 `map_f64_to_f32` 的精确 generic `sm_10`、`sm_11` 或 `sm_12`。
该 option 不会让 `rsqrt.f64` 或 `mad.f64` 在这些 target 上可用。`RcpImplicitF64` 与 `RcpRnF64` 分离，静态 RN 默认解释不伪造写入的控制。原生 SM13 source header 要求 PTX1.2+，映射 SM10/11 支持 PTX1.0，SM12 要求 PTX1.2。历史 CUDA 13.3.33 assembler 差异仅作为证据，不覆盖固定版本 manual 的规范。既有 implicit `mad.f32`
保持自身 contract。

availability metadata 在 plain requirement 和各 DNF clause 中可指定 exclusive 上界
`ptx_before: "1.4"`。省略上界表示无上界；格式错误、空或逆序 interval 均被拒绝。
plain 上界失败在相应 source range 报 `UnsupportedPtxVersion`；DNF 失败保持
`UnsupportedAvailability`。selection 不依赖 version；validation 使用当前 standalone
context 或当前 source/owned/replacement module version。copy 与 clone 保留 omitted
identity 和解释；升级至 PTX 1.4 后重新验证会失败，不会自动转换为 explicit form。

WMMA 源码拼写保留 PTX 6.3 的 `.aligned` 边界。PTX 6.0 或 6.1 引入的
156 个 FP16 输入、FP16 或 FP32 累加形式具有独立的 `*_implicit_aligned`
variant 与 `*ImplicitAligned` 最终类：load 72 个、store 36 个、multiply-accumulate
48 个。它们保留原始 PTX 下限和 SM70 要求，并要求 PTX `< 6.3`。现有现代类
要求显式 `.aligned`，PTX 下限为 `6.3` 或各自更晚的原始下限。历史类没有
`aligned` 字段、别名项、默认值或伪造的源码范围。两种拼写均保留不可变的
`MatrixFamily::WMMA_LOAD`、`WMMA_STORE` 或 `WMMA_MMA` topology：这些 family
蕴含参与 warp 的一致执行要求。此执行要求与按字节计算的地址对齐不同；
fragment、layout、address 和 stride 检查保持不变。

独立指令解析在没有版本上下文时选择实际写出的拼写。模块验证和指令可用性
检查根据当前自有上下文或替换上下文应用半开版本区间。`shared::cta`、整数、
subbyte/bit、BF16、TF32、FP64 及其他 PTX 6.3 或更晚引入的形式没有历史对应项。
来源为
[CUDA 13.3 所附 PTX 9.3 WMMA 描述](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-ld)。

现有 96 个 FP16 输入 WMMA MMA 形式（48 个显式 `.aligned` 形式和 48 个隐式
对应形式）暴露可选布尔 `satfinite`，默认值为 `false`。选中的 `.satfinite` 值
要求 PTX `< 6.5`，并与形式原始下限和对齐区间取交集：隐式形式支持 PTX
6.0/6.1 至 6.2，显式形式支持 PTX 6.3 至 6.4，均要求 SM70 或更新目标。
NVIDIA 在 PTX 6.4 弃用浮点 `.satfinite`，并在 PTX 6.5 移除。省略此限定符
保留形式原有可用性，包括现代 PTX 版本。饱和将正/负无穷映射为最大正/负
有限值，将 NaN 映射为正零。类型化 `satfinite.value` 与源码位置在自有解析、
复制及克隆后保留；当前上下文检查拒绝移除版本中的使用，不改变形式身份。
整数饱和以及 BF16、TF32、FP64 和其他 MMA family 均保持不变。这些是语法及
静态验证契约，没有验证 GPU 数值执行。参见
[CUDA 13.3 PTX 9.3 WMMA MMA 参考](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)。

Header numeric parameter record 保留完整有序 `array_extents` 与检查后的
`byte_extent`，须与自身 signature 逐轴一致；不同声明/调用之间 ABI 使用总元素数。
Name-only `ResolvedOpaqueEntryParameter::array_extents` 替换原 `is_array` 与
`array_extent`，保留有界轴而不推导 opaque 字节数。Function copy 独立拥有这些向量，
并独立 clone body instruction，见[公共 parameter migration](parameter_declarations.md)。

普通 register-vector operand 与固定 cardinality 的 vector/tensor tuple 都由 scalar
register leaf 组成。无论 arity 来自 modifier、allowed-arity 列表或固定 tuple bounds，
该角色对 element 和 aggregate policy 均适用。即使 element dtype 匹配，owned
whole-vector register 也不能替换单个 leaf。检查定位到对应 lane range，缺失时回退到
operand／instruction range。显式 scalar component 与真实 named-vector projection 的
effective vector width 仍为空，按既有 source 契约保持合法。外层 named MOV/memory
vector root 保留自身完整 vector shape；它不是 scalar leaf。Value-vector source 保留
既有 scalar-register、immediate 与 provenance 检查。该规则不改变 cardinality、
dtype/carrier policy、named-source 许可或 immediate range。

[Block-scaled MMA scale selector](converage/mma_scale_selector_coverage.md) 使用 U16
逻辑值与封闭的 B16/S16/U16 register carrier。两个 scalar leaf、byte mask、独立 A/B
thread 上限及 unsigned literal conversion，不因 whole-vector shape 或运行时 register 内容而改变。

选定的 multimem data vector、SULD destination 及根 `st.async` v2/v4 source 与 brace 一样接受普通 named register vector。没有 vector modifier 时，Element binding 唯一的固定 arity 决定 lane 数。既有 whole-base identity 与 canonical `NamedProjection` lane 一起独立拥有。MM/SULD 要求 element 同宽；根 `st.async` 保留较宽 carrier 规则。Named 声明仍仅限 v2/v4 且总宽不超过 128 bit；MM v8 仍仅接受 brace。SUST value-vector source 使用下述 source 契约。

`ResolvedValueVector` 现在除有序 `RegOrImm` element 外还拥有 `ResolvedVectorSource source`。默认仍为 BraceList 且无 whole base；完整 brace source 保留混合值、显式 component 及完整 brace range。选定的 78 个 SUST v2/v4 binding 也接受普通 named 声明。Named source 仅以 `source.whole_base` 作为 whole declaration authority；每个 lane 都是从真实 SymbolId/member/type/width 及完整 identifier range 派生的 canonical General-register `NamedProjection`。Immediate、sink、special register 及独立 scalar projection 不能组成 named source。`valid_value_vector_source` 复用 register-vector provenance 校验；未要求完整 provenance 时保留 legacy standalone 空 brace range 的兼容性。Owned module 必须保留完整 source，并在 syntax 销毁后重新连接 base 与每个 lane。

公共 ABI 迁移：必须重新编译 `ResolvedValueVector` 与 `checker::OperandView` 的 consumer。后者新增借用的 `const ResolvedValueVector* value_vector`，仅在同步 check 期间有效且指向实际 owned payload。生成的 projection 在 named permission 或 type check 前核对该 payload 的 arity、lane kind 与 register/immediate pointer。Copy、equality、assignment 和 instruction clone 包含 owned source；`IReferenceObserver::value_vector` 签名不变。其他 value-vector permission 保持默认 false，但不据此笼统断言所有对应 ISA role 都禁止 named syntax。RegOrImm domain 与 numeric conversion 规则未改变。

`checker::OperandDescriptor::mbarrier_state_token_form` 现为 `std::optional<MbarrierStateTokenForm>`，默认缺省，替代原来无条件的 Register 默认值。该公共 member 类型及默认值迁移要求重新编译 consumer。缺省表示非 token descriptor；全部 93 个生成的 typed-token binding 显式设置既有 Register、RegisterOrSink 或 Sink policy，包括 Register input。既有 optional register payload 与 observer 签名不变。私有 common checker 在约束 register presence 前核对实际 Register shape、sink flag、借用 reference 及 optional declared-type cache。允许 sink 的 policy 要求 write destination。其他 scalar type/width/component/declaration 与 availability 规则仍独立；不会全局强制要求未知 declaration type。没有 token policy 的普通 register/predicate/SHFL sink 保持原有规则。调用方提供的缺省选择非 token 检查，不构成 token 认证。Owned copy 与 clone 在 source 销毁后执行相同 form 约束；此 policy 或 B64 carrier 都不证明运行时 phase 内容、进展或 token 真实性。

标量 `Register` view 声明其为 General register。公共 checker 会拒绝实际借用 reference 或 class cache 中已知的非 General class，也拒绝该声明下已知的 predicate type 事实。register-vector 的 cardinality 合法后，同一规则适用于其标量 Register leaf，并以 lane range 定位诊断。真实 named-vector whole base 不是标量 leaf；General 显式 component 与 canonical `NamedProjection` lane 保留既有规则。Predicate 与 special-register view 保留各自的 shape 契约。未知 General type 及没有实际事实的 legacy view 仍由既有检查延后判定：此规则不新增必需 SymbolId 或 declared type，不要求所有 type cache 相等，也不新增通用 instruction-only declaration eligibility 检查。经上下文转换的 numeric type 仍遵循既有转换规则。class 验证不证明寄存器运行时内容。

Mbarrier register carrier 的类型按 role 区分，而书写的 instruction modifier 仍为 `.b64`。Opaque arrival/arrival-drop output 与 opaque `pending_count` state input 接受恰好 64 bit 的 fundamental B64/S64/U64/F64 声明。非 parity test/try-wait state input 的语义类型为 U64，接受 B64/S64/U64，拒绝 F64 及所有其他宽度。Frontend 保留每个实际 declared type、SymbolId、member/component 及 range；不做 numeric floating conversion，也不证明运行时 token 内容或先前操作。对齐的 shared-memory `.b64` object 与 register carrier 是不同对象。CTA RegisterOrSink、cluster 强制 Sink policy 及 availability 仍独立。

Report-value output 保留 frontend 带限定的 logical B8 model，并接受同宽 B8/S8/U8 carrier。固定 PTX 9.3 文本没有无歧义地把该异构 report operand 对应到规范 width/type；较新的官方 uint8 示例支持保留的 model，但不关闭固定参照的缺口。Report carrier 测试证明的是保留的 model，不是无条件的 ISA width 认证。Opaque F64 的接受来自已采纳的固定参照类型解释；辅助 assembler 的拒绝记录为工具差异，不构成新的 frontend 类型限制。

标量 F16 数值常量在 use site 转换为 IEEE binary16。Decimal、已求值浮点表达式、exact F64 bits 与裸 unsigned exact F32 literal 均只执行一次 nearest、ties-to-even 舍入。Binary64 bits 直接转为 binary16；exact F32 输入先精确拓宽。Signed zero、渐进 subnormal、溢出为带符号 infinity，以及保留 sign／高 payload 并 quiet 的 NaN 均遵循确定的前端 literal policy，与指令 rounding、FTZ、saturation 无关；这不模拟 CVT 结果。Integer origin 与浮点常量仍有区别。Exact F32 的 sign、括号及运算仍拒绝；Bit16 不继承 numeric half 转换，F16/F16x2 data initializer 仍禁止。

现有 11 个 generated class 中的 20 个字段从 `WithLocs<ResolvedRegisterRef>` 迁移为 `WithLocs<RegOrImm>`；公共 consumer 必须重编译，并在 `.value` 上使用 `std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>`。Variant 名称、add/sub 共享的 F16/BF16 domain、selector 顺序及所有 width／availability 规则不变。

| Generated class | 迁移字段 |
| --- | --- |
| `AddMixedF32`, `SubMixedF32` | `src` 各自 |
| `FmaMixedF32F16` | `src1`, `src2` |
| `SetHalfF16F16`, `SetHalfF16F16Boolean` | `src1`, `src2` 各自 |
| `SetHalfBf16F16`, `SetHalfBf16F16Boolean` | `src1`, `src2` 各自 |
| `SetHalfIntegerF16`, `SetHalfIntegerF16Boolean` | `src1`, `src2` 各自 |
| `SetpF16`, `SetpF16Boolean` | `src1`, `src2` 各自 |

Canonical 当前 F16/BF16 immediate 是低 16 位 word，没有 integer-source provenance 或 integer-negative flag。普通 instruction IR 保留当前 typed word，因此合法范围内的一致修改可以有效，无需恢复原 literal。Shared mixed BF16 字段即使 written BF16 numeric converter 尚待独立实现，也接受 canonical typed BF16 word。Call 保留原 lexical input 或转换前的 F64 expression value，并在 owned validation 中重算 half word。所有其余条件合法的标量 Reg/Param input 复用该转换；现代小宽度 Register-formal ABI 限制、vector no-splat 与 missing-context 要求仍独立。四个 scalar F16 SETP source 还需要单独 typed-policy delivery 实现 explicit raw Bit16 route；当前 numeric 路径不表示 raw integer 在规范上非法。Packed numeric 构造及 alternate-format converter 仍是独立范围。

普通 `cvt.pack.sat` 的 S32 input 除同宽 B32/S32/U32 register 外，也接受 integer constant 与 expression。既有 64-bit evaluator 将每个 input 窄化为低32位 S32 word，并保留原64位 integer bits／sign metadata；因此 `0x100000001U` 的 use word 为1，不要求 target-range。前端不把 input 预先饱和到 destination lane 宽度，也不预计算 packed result。Floating-valued input 仍不兼容 S32；最终 category 为 Integer 的 expression（包括 FP comparison）走 integer 路径。独立 B32 carry source 保留既有 literal／precision 契约，包括同宽 unsigned direct0f bit data；这不使 register-only stochastic `rbits` 接受 literal。Carry topology、destination width 与 target 边界不变。

公共 ABI 迁移：`CvtPackSatU8S32B32`、`CvtPackSat16S32`、`CvtPackSatSmallS32B32` 的 `src1`／`src2` 从 `WithLocs<ResolvedRegisterRef>` 改为 `WithLocs<RegOrImm>`。Consumer 必须重编译，并通过 `std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>` 检查 `.value`。六个字段原位迁移；class、modifier domain、selector 顺序不变。Copy／assignment／clone 保留 typed word、source cache、declaration identity 与 location。普通当前 IR word／reference 修改按当前事实核对；source association 仍是独立的 retained-syntax 检查。

三个 stochastic quad class `CvtRsSatfiniteF8x4F32`、`CvtRsSatfiniteF4x4F32`、`CvtRsSatfiniteF6x4F32` 的 `src.value` 改为 `ResolvedValueVector`，替代 `ResolvedRegisterVector`；consumer 必须重新编译并检查各 `RegOrImm` alternative。Brace source 保留完整 written range 和 lane 顺序，numeric floating source 复用公共 F32 conversion。Integer-origin source 仍不兼容，exact F32 constant 保留裸 unsigned literal 规则。此转换只生成 source F32 word，不执行 stochastic conversion、saturation 或 packed result。Named V4 F32/B32 source 以 `source.whole_base` 为唯一声明 authority，使用 canonical `NamedProjection` lane，并遵守既有 General class、arity、binding 和 128-bit 声明限制。Copy／clone 拥有 provenance 与 lane。借用的 `OperandView.value_vector` 必须指向实际 payload，且 lane pointer、type、range 和 descriptor permission 一致；默认 false 的 named permission 仍有效。Destination carrier policy 与独立 register-only `rbits` 规则（包括 widening policy）保持不变。

普通 register declaration eligibility 根据实际 physical declaration type 与中央 `ScalarTypeMetadata::register_declaration_usage` 分类判断。已知 General-register reference type 必须为 Fundamental，包括经 explicit component、named projection 或 vector register leaf 访问的情形；standalone 缺失声明事实仍保留既有 defer 行为。Logical BF16／TF32／packed instruction format 与 canonical typed immediate word 独立于 physical register declaration。Contextual operand type cache 不替代实际声明 type。Owned validation 另外检查 Register state space 中所有 concrete 普通 `SymbolKind::Variable` 声明，包括未使用声明；缺失或未知 concrete type 均不能证明 eligibility。Predicate 声明与 native scalar F16x2 仍为 Fundamental。保留的 source identity 记录历史 syntax association，不使当前已变更 binding table 免于声明合法性检查。Copy 必须置于拥有相关 symbol table 的 module 中验证。

### 拥有式 debug section 值

`ResolvedModule.sections` 持有源码顺序的 section fragment。公共 `ResolvedDebugSection`
alias 复用 `syntax_ast::AstSectionDirective` 及其小型 value-only variant；所有 string/range
都复制，不借用 AST/CST/source，也不保存 numeric cache。默认 module copy/assignment/move/swap
包含非空整数与 relocation payload。此次 CST/AST/Module 公共迁移改变源码和二进制布局，消费者须重编译。

共同 checker 解码已经 token 化的 magnitude，按 B8/B16/B32/B64 原始区间
`[-2^(w-1), 2^w-1]` 验证，完整保留 UINT64_MAX 与负 2^63；U 后缀不放宽范围。
手册 `.b16 -65535` 示例与显式区间冲突，按区间拒绝。B32/B64 符号数据分别持有未知链接地址的
single symbol、signed byte addend（包括 checked `label-integer` 简写）以及同一逻辑 section 内
唯一已知 label/section base 的差。同名 section 合并 locality 而保留 fragment 顺序；链接地址与
relocation 结果仍未知。不推导 string、任意表达式或 symbol/integer 混合列表语法。

已知 PTX 门槛为 section 2.0、label addend 3.2、B16 data 6.0、label definition 7.2、
label difference 和负数 7.5；负零按数学零处理。缺少上下文沿用既有 validation policy。
AST-free 检查重验当前 owning record；source-aware 检查按 supplied effective context 独立验证
当前与 supplied section，并重绑定其 debug label。合法 section 数据变化不导致 executable
source-identity mismatch，合法 replacement 也不能掩盖非法 current payload。
这只证明有限源码语法及静态 locality，不证明 DWARF 解码、链接字节值、target-debug DWARF recognition
或 GPU 执行。

### Target debug 缺失检查与 recognition 边界

只要文件内任一 `.target` 指定 `debug`，缺失条件就适用于整个文件，不依赖 section 与
前后 target region 的位置。已知 `debug` 且 owning modern section record 与 legacy record 同时为零是确定错误；即使
`AvailableContext` 缺少 version 也拒绝。`.file`、`.loc` 或保留的 debug-string symbol identity
不能替代上述 debug serialization record。既有 PTX 3.0 option gate 与完整 context 要求保持独立。
Source-aware validation 对 current owned options/modern sections/legacy records 和 supplied AST options/modern sections/legacy records
分别复用私有缺失规则；合法一侧不能掩盖另一侧的确定缺失或非法 typed payload。
合法且不同的数据集仍保持 section-neutral source association。不增加 presence cache、recognizer enum 或 decoder。

**D-DEBUG-DWARF-PRESENCE-RECOGNITION 仍是 reference boundary，不是 code PASS。**
固定的 [target 条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#ptx-module-directives-target)
和 [section grammar](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-section)
没有给 arbitrary name、empty section、label-only section 或非空 typed data 定义充分 recognition
条件。`.debug_` 前缀、whitelist、非空 payload 或强制 `.debug_info`/`.debug_abbrev` 配对都会发明该条件。
配对 witness 只是一项结构对照，不是普遍最低要求或 decoder。因此，非零且结构合法的 section record
验证成功并不证明实际找到了 DWARF information。有限 debug-off/on 边界测试明确记录此限定；
不声称完整 recognition、链接或二进制真实性。

current owned `.language` value 是不依赖 context 的 intrinsic data：optional 缺省表示
省略/default `ptx`；engaged vector 必须非空，且每个 raw token 都须通过统一的公共
semantic decoder。多个值与重复项保留准确顺序及 spelling。两种 module validation API
即使缺少 version/target，也会在 owning function range 重查 current value，合法
supplied source 不能掩盖 current 非法 token。合法 current name/code/list 修改或移除
optional 不要求与历史 payload 相等。replacement AST identity 仍保存 raw 顺序：
将 `"ptx"` 改为 `3` 或重排合法 list 仍可触发既有 source-association mismatch；
redeclaration 则继续比较 ordered decoded aliases。presence 要求 PTX 9.3、适用于所有
 target；已知过旧 selected/supplied version 即使无 target 也拒绝，只有 version 缺失
才可在 `AvailableContext` 下推迟 availability。domain 从不推迟，strict context 要求不变。
新 decoder 是 additive exported semantic function，client 须使用匹配的 installed
header/library；不增加 payload layout 或 backend language execution 保证。

Owned entry-input `.ptr` 的可用性从既有 parameter record 推导，在要求完整 target
context 前先检查 PTX 2.2 门槛。Coherent 版本 retarget 同时更新所选 region 与 function
metadata；AST 销毁后的 copy 与 instruction clone 仍执行同一检查。带源码验证则将
supplied effective version 应用于当前 pointer presence，不把历史缓存版本作为否决条件。
既有 pointer type、shape、alignment 与 pointee-space 检查保持独立；可用性诊断采用
既有 function-range fallback。

Parameter modifier metadata 使用精确的 typed identity
`(MemoryStateSpace::Parameter, ParameterAddressQualifier)`。domain 与
availability descriptor 追加默认值为 `Default` 的 `parameter_qualifier`；
`ModifierValueView` 追加默认 engaged `Default` 的 optional 成员。
原有 positional aggregate initializer 的含义保持不变，但公共布局的 ABI
发生变化，consumer 必须使用匹配的头文件和库重新构建。未知投影
（`nullopt`）、无效 enum 或带 qualifier 的非 Parameter space 均独立于 target
和拼写 presence 被拒绝。非默认 Parameter qualifier 在清空 location 后仍具有
语义 presence，因此普通 Parameter 不会命中 8.3 的 qualified availability row。

生成的 view 读取与同一 normalized modifier binding 关联的当前直接 address，
或选中的直接 symbol；嵌套 symbol 的默认值不构成第二份 written qualifier。
固定形式使用唯一 canonical identity，register alternative 不需要虚构 qualifier
payload。关联的真实 written qualifier 必须一致。现有 typed parameter-address
preservation policy 可从固定普通 modifier 派生 Entry frame qualifier；该 frame
事实单独验证，不改变固定 modifier identity。既有 frame、readonly、declaration、
source identity 以及 Shared/atomic qualifier guard 继续生效。

两个 `validateModule` overload 均在缺少 version/target 导致 instruction 检查跳过
之前验证 current-owned Parameter address。精确 variant/layout operand descriptor
提供封闭 address-space domain 与当前关联 state-space 的相等约束。空 domain
仍不施加限制，也不推断 runtime register pointee。Device formal 的地址
materialization 继续遵循既有 policy，可得到 Local space。已知 entry input
不能提供显式要求可写的 operand 或 Return parameter operand。
`OperandAccess::Read` 表示 operand value access，不代表 memory effect；MOV
地址获取和 tensor-map 读取仍合法。

`detail::IReferenceObserver::operand_context` 追加为默认 no-op virtual callback。
非空 descriptor 为紧随其后的顶层 address-bearing payload callback 建立 bracket，
nullptr 重置。descriptor 与 span 仅在同步 bracket 内借用；collector 复制所需
scalar/domain 事实，不保留借用指针。原 payload callback 和顺序保持不变。
已有派生 source observer 无需新增 override，但 virtual slot 改变 ABI，observer
与 producer library 必须使用匹配头文件重新构建。


## 历史 entry body input

PTX 1.0–1.3 entry body `.param` 在唯一 `parameter_declarations` 表中保留
`LegacyEntryInput` role，以及 scope/group/array identity。Signature 仍只描述语法
header ABI，空 header 不等于没有 host input；lexical metadata 顺序不分配 launch
地址或 packing。只有 entry body role 歧义才要求 version 并报告
`MissingValidationContext`，不按 SM 猜 role。已 typed module 缺 context 时保留 role。

`ResolvedSymbolRef::parameter_role` 从 actual record 同步派生，structural kind 仍为
`CallParameter`。Address/MOV/tensor 共用 input/return/call-slot 方向映射。两个
validator 都在 context skip 前重连当前 record/cache，检查 readonly、entry frame、
direct call-actual 排除、shape/scope 和 logical capacity。Legacy LD 不要求 call
return staging，MOV 保持 `.param`；现代 BodyLocal staging 不变。Checked logical
总量使用 per-member bytes 乘 compact count，检查32764-byte已知 global ceiling，
不重复乘 axes/vector，也不套用1.4的256-byte header cap。
`D-H03-LEGACY-ENTRY-EXACT-AGGREGATE-LIMIT` 对精确历史 physical launch packing
仍 qualified。详见[parameter contract](parameter_declarations.md)的窗口与边界。


共用 current operand-domain pass 将 actual `EntryInput` 与 `LegacyEntryInput`
record 都识别为 readonly entry object，在 context skip 前使用当前 typed
layout/field requirement，覆盖非 LD/ST consumer。MOV 读取地址仍合法；明确要求
writable operand 时拒绝。Legacy readonly 保留原 label 与 instruction range，
仅合并相同 local cause，不按 opcode 推测 writer，也不新增历史指令权限。

## SM 可用性的数值独占上界

plain availability 与每个 `any_of` clause 支持可选的 `sm_before: 70`。它与 `sm: 30` 共同表示 `30 <= SM < 70`；省略下界时为零，省略上界时无界。字段必须是严格的 uint32 整数且大于下界；typed normalization 与 rendering 拒绝零、布尔、浮点、字符串、显式 null、溢出及空或逆序区间。JSON Schema 检查整数及范围约束，typed 边界还拒绝整数值浮点数。最多六个有序 alternative 和 `any_of` 不得混合 plain field 的规则保持不变，不引入 target 交集求解器。

`AvailabilityClause::sm_before` 和 `AvailabilityDescriptor::sm_before` 在 `ptx_before` 后追加默认缺省的 `std::optional<uint32_t>` member；此 aggregate 布局变化要求重新编译公共 consumer。谓词直接比较具体 `TargetInfo::sm_version`，不依赖 profile 拼写或 flavor；既有 exact-target、family、capability 和 mapping 条件仍为合取。variant、layout、modifier、operand-value、parameter-address 和 memory-vector 的 plain 失败均产生有位置的 `UnsupportedSmVersion`；DNF 失败仍为 `UnsupportedAvailability`。

旧式 SHFL 使用 `any_of: [{ptx: "3.0", ptx_before: "6.4", sm: 30}, {ptx: "3.0", sm: 30, sm_before: 70}]`。source module 仍要求已知 profile 及合法 profile/version header。`AvailableContext` 中缺少 version 或已知 target 时继续推迟整个 instruction availability 检查，即使其他已知事实看似不兼容；成功不构成完整 ISA 认证。严格验证报告 `MissingValidationContext`；显式未知 profile 仍为 `UnknownTarget`。不新增 partial DNF evaluator 或 opcode 特定 context 规则。

## 历史101 source profile 与验证 feature

`sm_101`/`compute_101` 及 A 形式从 PTX8.6 起支持，F 形式从 PTX8.8 起支持；PTX9.0/9.3 仍接受，rename 不引入上界。110 source profile 仍从 PTX9.0 起支持。`TargetIdentity` 保留 architecture101、flavor 和精确书写；`same_target_identity` 比较 source architecture/flavor，因此101A与110A仍不同。

新增 base 函数 `target_feature_architecture(identity)` 读取显式 private catalog 数据：三个101 key 投影到 feature architecture110，其他 key 保持原 architecture；未收录的 typed key 不会因此被识别。公共 `TargetProfile` 与 `TargetInfo` 布局不变。source/owned/semantic factory 用此 feature level 作为 numeric SM，同时保留 source identity，并传递既有110基础能力 `cluster`、`aggregate_smem`、`reserved_smem`、`graph_exec`。Generic101 没有 family；101A/F 仅启用 canonical `sm_110f`，不继承100f/103f，也不支持 FP64 mapping option。此有限对应基于公布的 rename 与 family 关系，不是物理硬件或 driver 保证。

availability 的 singular 与 span exact-target 要求通过该 helper 比较 validation-feature architecture 与精确 flavor；此关系不同于 source identity 和存储 key 相等。手动提供的 `TargetInfo::sm_version` 仍是 caller 的具体 feature 事实，checker 不会覆盖它。source profile 引入版本、instruction/PTX 门槛、capability 与 shape 条件仍独立。

历史指令数据仅修改已采用的231个 parent：219个保留100A clause并加入101A，仅原有110F分支时加入101F；12个 block-selector parent 追加8.8历史101A/F clause。854个原110条件及其版本门槛全部保留，DNF上限仍为六，25个受限 cluster gather4/im2col-W parent 不变。TCGEN 闭合 row 保留 kind、scaled-D 与显式 scale-vector 限制：历史 written block16/32 门槛为8.8，100-only vector 和 scaled-D 路径仍限定100。生成的 operational query 先验证精确 source profile 及其最低版本，再匹配 feature；未知 word 与地址仍为未决义务，不从实时 register 推断事实。

两个 module validator 均独立检查当前 function 的 `source_region`、`source_version`、精确 `source_target` 与当前 owned region 一致。合法 replacement AST 不能掩盖矛盾 cache。101/110 context 自身一致且当前 payload 在 supplied effective context 下合法时，允许通过 supplied header retarget；保留的 availability 不成为历史否决。缺少完整 instruction context 时继续既有 `AvailableContext` 推迟及严格 `MissingValidationContext` 政策。


### 已弃用的最大 CTA 提示

Entry header 从 PTX ISA 1.3 起接受 `.maxnctapersm ncta`，适用于所有已识别
source target，包含 PTX 2.0 及后续版本；弃用不表示版本上界。独立的 AST/owned
`MaxNctaPerSm` kind 表达优化提示：仍可执行额外 CTA，因此不是 runtime occupancy
硬上限。`.maxntid` 可省略，`.minnctapersm` 可作为独立 resource 同时存在。

完整 unsigned integer token 经检查后进入现有 `uint32_t` resource value，允许
零及 `UINT32_MAX`，溢出不截断。该范围仅为 frontend 已建模容量；公开语法未指定
此位宽，检查不证明硬件 occupancy 或 driver 可执行性。CST/AST 存活时保留书写的
radix、suffix 和 range；owned contract 仅保留 kind、normalized values、explicit
count 和 range，不增加平行 raw spelling 字段。

两个 validator 都检查当前 payload shape、entry placement 和已知 range containment。
已知 PTX 版本低于 1.3 时，即使 target 不完整也拒绝；`AvailableContext` 缺少版本时
只延迟 availability，intrinsic 检查继续。Source-aware 路径采用 supplied effective
version。当前 numeric value 的一致修改合法；替换 AST 中书写的 resource kind 或
value 仍受现有 source association 规则检查。

源码依据：[PTX ISA 最大 CTA 指令](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#performance-tuning-directives-maxnctapersm)。


### 历史调试序列化

大小写敏感的 `@@DWARF` marker 在 module、function body 或 nested block statement
位置引入一条闭合 production；支持所有已识别 source target 的 PTX ISA **[1.2, 2.0)**，
不授予 function header 位置。`.byte` 为 unsigned hexadecimal list（0..255），
`.4byte` 为 0..UINT32_MAX，`.quad` 为 0..UINT64_MAX；list 必须非空。后两者还支持
单个普通或单前导点 symbolic label，保留为未链接值，不要求普通 PTX symbol binding。

`0x`/`0X` 以及可选 `U`/`u` 后缀继承现有 PTX integer token；后缀是派生选择，并非
历史语法的显式措辞。一元正负号、decimal/octal/binary 数值、symbol/numeric 混合 list、
addend、difference、label definition 和任意 host assembly 不在模型中。支持公开例子
的 `.section name, "", @progbits` header；将 name 泛化为普通或单前导点 identifier
来自该例子，flags 仍须恰为空字符串，type 仍须恰为 `progbits`。

空白、注释和换行分隔 token，不结束 record。一个结构完整的 payload 加可选分号
结束 record，后续 token 返回普通 PTX grammar；错误恢复保留后续 marker、block
和 function。Lossless CST 保存注释及 source text；owning AST value 保留精确
marker、word/header spelling 和 record 内 range，`@`、`progbits` 是独立 leaf。

`ptx_syntax_legacy_dwarf.hpp` 仅含小型 owning value，不引入 module、instruction
或 expression AST 定义。Module/body variant 复用同一个 `AstLegacyDwarfRecord`；
`ResolvedModule::legacy_dwarf_records` 是唯一有序 `{value, scope_id}` stream。
Scope 来自模块 symbol table 的实际 declaration occurrence 和 block；canonical
function 的 preferred owned scope 不能代替实际 definition occurrence。Metadata 不生成
executable node、label slot 或 instruction provenance index；module/function source
identity 忽略它，普通程序结构仍影响 identity。

Current 和 supplied dataset 独立检查 typed word、range、scope ownership 及格式窗口。
Source-aware 对两者采用 supplied effective version，AST-free 使用当前 module context。
已知越界版本即使缺少完整 target 也拒绝；`AvailableContext` 缺少版本时仅延迟格式
窗口，intrinsic 检查继续，Strict 缺失 context 仍拒绝。合法当前 metadata 修改和合法
不同 dataset 无需旧 source byte equality；跨格式替换须移除或重建当前不兼容格式数据。

`.target debug` 的确定缺失为每个 dataset 独立满足 **modern section 为零且 legacy
record 为零**。未知 linked label、header 或非空 byte 仅避免确定缺失，不能认证实际
DWARF recognition、解码、linked address 或 binary authenticity。已知完整 legacy
context 无法同时满足 pre-2.0 格式及 PTX-3.0 debug option；缺版本测试仅是 partial
context control。

依据：[历史 debug marker](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives-atatdwarf)
及[历史格式窗口](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#debugging-directives)。
