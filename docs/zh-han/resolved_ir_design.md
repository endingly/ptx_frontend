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
`ResolvedIndirectCallee` 为 non-predicate `.reg` indirect target 或已绑定的 function-local
`.callprototype`/`.calltargets` label 提供 descriptor-independent identity；所属
`ResolvedFunction` 另行拥有对应的有序 metadata payload 与 normalized ABI，operand 因而保持紧凑，
但 metadata 不会丢失。generated `Call::Direct` 现有三个额外的 `IndirectCall` layout
（target/metadata、target/input/metadata、return/target/input/metadata），均要求 PTX 2.1 / SM 20；
normal module indirect call 会保留已绑定的 target 与 metadata identity，并通过 metadata-indexed
canonical signature 复用 direct-call ABI contract，不会创建第二套 indirect-call model。

公共 model 入口是 `<ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>`，
它依次聚合手写 foundation、生成的 instruction struct 与 `ResolvedInstruction` union、以及手写
module container。`ptx_resolved_ir_module.hpp` 只直接包含 foundation 和生成的 instruction
surface，因此固定 module field 可在不修改 generator 的情况下用 C++ 演进，且 headers 保持无环。
这些头只依赖拥有值的数据和只读 descriptor，不要求完整 Syntax AST、resolver helper 或
instruction checker 实现接口。解析入口位于 `ptx_resolved_ir_resolution.hpp`，
检查入口位于 `ptx_resolved_ir_checker.hpp`；`ptx_resolved_ir.hpp` 保留为兼容聚合头。

公共层还提供了一个与具体 opcode 无关的边界：

```cpp
using ResolvedInstruction =
    std::variant<Add, Sub, Bar, Bra, Call, Mov, Ld /* ... */>;

std::expected<ResolvedInstruction, ResolveDiagnostic>
resolveInstruction(const syntax_ast::AstInstruction& ast);

std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveModule(const syntax_ast::AstModule& ast);
```

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
显式 validation catalog 包含不具有现代 capability 的历史 `sm_13` 与 `sm_20`，以一致检查 PTX 6.0
的 `sm_20`/`sm_30` 声明边界；不会因此接受任意数字形式的 target spelling。

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
`unified_id` payload。对 `.attribute(.unified(uuid1, uuid2))`，`(*unified_id)[0]` 是
UUID `uuid1`（upper 64 bits），`(*unified_id)[1]` 是 UUID `uuid2`（lower 64 bits）；不发生
byte-order 或 host-address conversion。malformed source UUID token 仍保留为 declaration
diagnostic，而 AST-free validation 会拒绝缺少该 typed payload 的 `.unified` attribute。

成功的 direct、alias 或 metadata-backed call 会将 module literal 保存为 formal-driven 的
`ResolvedImmediate`。没有 module call contract 的 standalone instruction 可保留没有 value 的
`ResolvedCallLiteral`；consumer 不得猜测 type。已声明的 external module call 仍保留 signature
与 formal-typed literal；实际 linking 或 relocation 仍延后处理。resolved/binding data 使用
`base::DeclarationStateSpace` 与 `base::LiteralCategory` 表示语义值；旧有
`AstStateSpace`、`AstImmediateKind` alias 继续保持 source compatibility，但不要求保存 Syntax AST。

instruction range/opcode 与全部 owned record 都是 semantic provenance。frontend 保留 `.language`
以及 function ABI/resource contract；`.file`、`.loc`、`.section`、`.pragma` 属于 syntax/debug 或
advisory metadata，明确不作为 resolved payload。generated C++ struct layout 或 binary ABI 不保证稳定。
原始 module directive 仍需要 AST；owned model 是已声明 subset 的 semantic handoff，不是完整源码
序列化契约。

`ResolveDiagnostic` 拥有 message 和 source range 的值。模块解析保留原阶段的
`binding_kind`、`declaration_kind` 或 `checker_kind`，以及主位置 `range` 和 binding
或 declaration semantics 提供的 `previous_range`。`stage()` 由类型化类别推导出
`Binding`、`DeclarationSemantics` 或 `Checking`。转换而来的诊断恰有一个类别字段；
resolver 原生错误的三个类别字段均为空，阶段为 `Resolution`，目前没有更细的错误码。
调用者无需解析 message，即可检查转换诊断的原始类别与关联位置；源码和 AST 销毁后
这些信息仍然有效。诊断顺序和原有提前返回边界不变。既有 aggregate 字段保持原顺序，
新增可选类别字段追加在末尾。

`resolveInstruction` 根据指令数据库生成，并分发到现有的 `resolve<T>` 特化。调用者不再
需要手写 opcode 分派，同时每个 opcode 仍保留强类型结构。`resolveModule` 先建立
`SymbolTable`，再为每个 function scope 构造显式 `ResolveContext`；返回的
`ResolvedModule` 拥有 symbol table，`ResolvedFunction` 以函数 `SymbolId` 标识。每个
function 以唯一的 `parameter_declarations` 表拥有通过验证的 `.param` 声明。
按 `ParameterDeclarationRole::EntryInput` 筛选即可按源码顺序取得 entry header input。
`ResolvedFunction::label_positions` 以已绑定的 `SymbolId` 和 source-order 的 instruction
boundary 记录每个 function label；boundary 基于递归展平的 body，首条 instruction 前为零、
连续 label 共用一个 boundary、末尾 label 为 `body.size()`。standalone `resolveInstruction`
与 `resolve<T>` 不要求声明上下文，继续服务单指令工具。directive 与 declaration 仍由
Syntax AST/symbol table 保存，不复制成未解析的 Resolved IR 字符串字段；刻意保留的
例外是拥有值的、已规范化 parameter 与 storage metadata。`.file` 与
`.debug_str` identity 会在那里验证 `.loc` metadata，
但 `.loc`、`.section` 与 `.pragma` 不产生 Resolved IR node，也不附着到 instruction。

`ResolvedFunction::parameter_declarations` 拥有通过验证的 `.param` 声明：先按列表顺序
保存 return formal，再保存 input formal，最后按词法遍历顺序保存 body-local declarator
（含嵌套 block）。每个 `ResolvedParameterDeclaration` 保留 `symbol_id`、`scope_id`、role
（`EntryInput`、`DeviceInput`、`DeviceReturn` 或 `BodyLocal`）、基础 `scalar_type`、有效字节
`alignment`、`explicit_alignment`、`vector_width`、从外到内的 `array_extents`、经溢出检查的
`byte_extent` 与可选 pointee property。scalar 的维度列表为空；受支持的 unsized device input
array 的维度值及 byte extent 为空。所属 function 与 symbol table 即使在局部同名遮蔽时仍
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

`<ptx_frontend/semantic/ptx_function_contract.hpp>` 提供不依赖 Syntax AST 的 canonical
function-signature contract。其 parameter contract 使用 semantic state-space 与 pointer-space
enum、`ScalarType` 和 typed vector shape，以及只为诊断保留的 invalid spelling；数值字段是
optional tagged value：omitted、已验证 constant 或 invalid structural key。direct-call ABI
直接消费这些 normalized value，不再重解析 alignment text 或旧 array-extent string protocol；
invalid structural data 绝不会被静默替换为 default。

call-staging 邻接性按当前词法 body 的执行指令序列检查。普通变量声明、`.loc` 和
`.pragma` 不打断 call 前的参数 store 或 call 后的返回值 load。标签、嵌套 block 及
call/branch metadata 仍是扫描边界；嵌套 body 使用自身 symbol scope 独立检查。
实际插入的执行指令与带 predicate 的 staging 访问仍然非法。声明所在的位置不会改变
call 必须使用的已绑定参数 identity。

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

`AstImmediateKind` 保留 lexer 对 literal 的分类。整数 decimal/octal/hex（包括可选 `U`
后缀）先在 PTX 64-bit signed/unsigned source domain 中求值；unary minus 保留该 source
type，而 unsigned negation 按该宽度回绕。ordinary data use 随后保留 target width 的低位。
`WARP_SZ` 是 source 定义的 signed integer constant `32`，也可用于普通
instruction-immediate position；它不是对 target physical warp width 的查询。generated
operand descriptor 为每个 semantic use 独立选择截断或严格 target-width
representability；fixed scalar type 只表达 provenance。generated range、exact-value 与
multiple-of control 比较保留的 source bits，而无约束的 control 显式选择严格 conversion。
按 formal parameter type 检查的 call literal 与 address offset 继续执行严格的 target-width
representability 检查。signed `-0` 在数值上是 zero，而 floating negative
zero 保留其 IEEE sign bit。decimal float 目前支持转换至 `F32` 与 `F64`；
单个 leading `+` 只在 decimal decoding 时规范化，而 leading `-`、signed zero 与
exponent sign 保持通常的 floating semantics。raw `0f`/`0d` bit-pattern rule 不变。
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
   候选 variant 内把 spelling 绑定到有序 slot。required/fixed slot 可以通过位置
   消除共享 spelling 的歧义；database 会拒绝涉及 optional slot 的重复 spelling。
   重复占用一个 slot 会被诊断。
2. `selectVariant<T>` 只依据上述 variant-local 绑定选择唯一 variant。`absent`、
   `optional`、`required/fixed` 都按 slot、允许值和规范或显式别名顺序匹配。
   顺序别名不产生新的语义 variant，也不改变 field 绑定。
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

- 每个 projected dynamic modifier value 是否属于已选 variant 生成的 semantic domain。此检查
  不依赖 source location 或 modifier 在源码中是否出现：省略 optional modifier 时检查该字段声明的
  default；越出 domain 的编辑后 value 即使没有 provenance 也仍然非法，诊断 range 回退至
  instruction range。
  `ModifierValueDomainMismatch` 表示 value 不在该 domain 内。
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

单条 instruction 的 `checker::check<T>` 由调用方提供 `checker::Context::target` 与
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
`submod/resolved_ir/include/ptx_resolved_ir_checker.hpp` 与生成的
`resolved_ir.gen.hpp`。

direct-call ABI、function-local call-argument `.param` memory、带限定的 `::entry`/`::func`
form，以及 call adjacency/predication constraint 均由 module resolution 覆盖。indirect-call
metadata、scalar `.b128` 与 wider `.b128` register 所需的 declaration-type availability 仍不在
本切片范围内。legacy scalar/vector `ld/st` cache operator、PTX 8.8 modern memory vector、static
memory-address alignment 与 memory consistency qualifier 已纳入本切片。
