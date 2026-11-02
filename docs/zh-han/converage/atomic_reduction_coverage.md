# PTX 9.3 原子操作与归约覆盖范围

前端按照 [PTX ISA 9.3 §9.7.14.5–6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-atom)
解析、生成 owned IR 并检查同步标量与向量 `atom`、`red`，以及两种 `red.async` 的操作/类型组合。检查涵盖操作数
形状与类型、已知地址来源、自然对齐及目标可用性；前端不执行原子操作。

| 操作/类型组合 | `atom` | `red` | 基础 PTX / SM 下限 |
| --- | --- | --- | --- |
| `add/min/max.{u32,s32}`、`inc/dec.u32`、`and/or/xor.b32` | 是 | 是 | `atom.global`：1.1 / 11；`red.global`：1.2 / 11 |
| `exch/cas.b32` | 是 | 否 | 1.1 / 11 |
| `add.u64` | 是 | 是 | 1.2 / 12 |
| `exch/cas.b64` | 是 | 否 | 1.2 / 12 |
| `min/max.{u64,s64}`、`and/or/xor.b64` | 是 | 是 | 3.1 / 32 |
| `add.f32` | 是 | 是 | 2.0 / 20 |
| `add.f64` | 是 | 是 | 5.0 / 60 |
| `cas.b16` | 是 | 否 | 6.3 / 70 |
| `cas/exch.b128` | 是 | 否 | 8.3 / 90；显式 `.sys` 要求 PTX 8.4 |
| `add.noftz.f16x2` | 是 | 是 | 6.2 / 60 |
| `add.noftz.f16` | 是 | 是 | 6.3 / 70 |
| `add.noftz.{bf16,bf16x2}` | 是 | 是 | 7.8 / 90 |
| `v2/v4/v8.{f16,bf16}.{add,min,max}.noftz` | 是 | 是 | 8.1 / 90 |
| `v2/v4.{f16x2,bf16x2}.{add,min,max}.noftz` | 是 | 是 | 8.1 / 90 |
| `v2/v4.f32.add` | 是 | 是 | 8.1 / 90 |

每个标量操作/类型组合及向量操作/类型组合只有一个公开 variant；向量宽度是类型化 modifier。可选后缀彼此独立：`atom` 接受
`.relaxed`、`.acquire`、`.release`、`.acq_rel`，同步 `red` 接受 `.relaxed`、
`.release`；两者的 scope 都可为 `.cta`、`.cluster`、`.gpu`、`.sys`。
省略的语义和 scope 在 owned IR 中分别为 `MemoryConsistency::Omitted` 和
`MemoryScope::None`，ISA 的实际默认值分别为 relaxed 和 GPU。原有显式 global
形式的两种限定符顺序仍被接受。显式 scope 要求 PTX 5.0 / SM 60，显式语义
要求 PTX 6.0 / SM 70，`.cluster` scope 要求 PTX 7.8 / SM 90。这些下限独立
作用，包括只写 scope 或只写语义的形式。

源码中的地址限定符可以省略（generic），或写 `.global`、`.shared`、
`.shared::cta`、`.shared::cluster`。`Atom::address_qualifier` 和
`Red::address_qualifier` 以 `AtomicAddressQualifier` 类型保留这五种写法，
并与地址操作数的有效 state-space 来源分开。Generic 寻址要求 PTX 2.0 /
SM 20，且已知地址必须指向 global 或 shared。普通 `.shared` 的下限为 PTX
1.2 / SM 12；shared 的 64 位 `add` 与原子 `exch/cas` 要求 PTX 2.0 / SM 20。
显式 `::cta` 要求 PTX 7.8 / SM 30，`::cluster` 要求 PTX 7.8 / SM 90。
不带子限定符的 `.shared` 实际使用 `::cta` 地址子空间，但在 IR 中仍与显式
`::cta` 不同。地址子空间与 memory scope 相互独立，例如
`atom.shared::cluster.cta.add.u32` 可被接受。已知地址来源必须与显式
全局/共享限定符一致，checker 也会重新检查修改后的 owned IR。
checker 从每个 variant 的 state-space modifier 推导可写地址限定符：同步向量和
async release 只允许 generic/global；shared-completion `red.async` 只允许
generic 或 `.shared::cluster`。无效枚举值及超出范围的修改都会被拒绝。
所有 `atom`/`red` 方括号地址偏移（包括 `red.async` 的 barrier 地址）都使用 PTX
signed 32-bit 源范围；checker 对修改后的 owned IR 也重新检查该范围。`mov`
地址重定位仍可使用更宽的偏移。

符合条件的标量与向量 `atom`、`red` 均接受 `.L2::cache_hint`。标量形式将其放在
类型之前（half/bfloat 加法的 `.noftz` 之后）；向量形式放在操作名及可选 `.noftz`
之后、`.vN.type` 之前，例如 `atom.global.add.noftz.L2::cache_hint.v2.f16`。该后缀要求末尾附加一个 64 位
`cache_policy` 寄存器，目标下限为 PTX 7.4 / SM 80。owned IR 保留写出的
hint，并为 policy 选择单独的类型化操作数布局。Cache hint 可用于显式
`.global` 或 generic 寻址。已知指向 global 的地址可接受；来源未知的 generic
寄存器地址也可接受，但运行时必须指向 global 内存。显式 shared 与已知指向
shared 的 generic 地址均拒绝。`atom.cas` 没有 cache-hint 形式。无后缀的 policy
操作数或无 policy 的后缀均拒绝。

所有标量 `atom` 的目标都可以是兼容寄存器或 `_` bit bucket。owned IR 用缺失的
`ResolvedRegisterOrSink::register_ref` 保留 bit bucket，指令仍是 `Atom`。
整数和位操作的寄存器目标必须兼容其类型。源操作数可为兼容寄存器或采用
普通窄化转换的整数立即数；`cas` 接受比较值与交换值。浮点 `add` 接受原生
`.f32`/`.f64` 或等宽 `.b32`/`.b64` 寄存器、十进制浮点字面量及 `0f`/`0d`
位模式字面量，不接受整数立即数。Half 和 bfloat 的 `add` 必须写 `.noftz`；
标量 `.f16` 可用 `.f16` 或 `.b16` 寄存器，打包 `.f16x2` 可用 `.f16x2` 或
`.b32`；BF16 形式仍要求精确 `.b16`/`.b32` bit container。
`.b16` CAS 的目标及寄存器源接受兼容的同宽寄存器，`.b128` CAS/交换使用精确
`.b128` 寄存器。CAS 始终有四个操作数，不带 cache hint。地址必须按二、四、
八或十六字节自然对齐。浮点加法
按最近偶数舍入。Global `.f32` 原子操作把次正规输入和结果 flush 为保留符号
的零；shared `.f32` 和 `.f64` 不做此处理。这些是 ISA 行为说明，前端不模拟执行。

向量形式只能访问 global 内存：可显式写 `.global`，或使用 generic 寻址。
已知 shared 地址会被拒绝；来源未知的 generic 寄存器地址可接受，但运行时必须
指向 global。`atom` 的目标和源均为花括号向量，`red` 的源为花括号向量；
`atom` 的目标与源元素数量必须相同。PTX 语法将操作、可选 `.noftz` 和可选
`.L2::cache_hint` 放在 `.vN.type` 之前，例如
`atom.global.add.noftz.L2::cache_hint.v2.f16`；原有的
`.vN.type.operation` 写法仍被接受。`.f16` lane 可用 `.b16`、`.f16`、`.u16`
或 `.s16` 寄存器；`.f32` lane 可用相应的 32 位寄存器。`.bf16` lane
仅接受 `.b16`；打包 `.f16x2` lane 可用 `.f16x2` 或 `.b32`，`.bf16x2`
lane 仅接受 `.b32`。[PTX ISA 9.3 的基本类型规则](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fundamental-types)
对浮点标量操作数更严格，而向量 `atom`/`red` 章节没有明确规定 lane 的声明类型。
这里的整数 lane 支持仅限向量 `.f16`/`.f32`：CUDA 13.3 `ptxas` V13.3.73
使用 `-O0 -arch=sm_90` 成功汇编了 `.f16` 搭配 `.u16`/`.s16`、`.f32`
搭配 `.u32`/`.s32` 的全部八种 `atom`/`red` 组合，见
[可复现的 PTX 源文件](../../../examples/atomic_vector_register_types.ptx)。
这只证明汇编器接受，不代表已验证 GPU 执行语义。每个向量内 bit 类型
lane 是中性的，整数与浮点寄存器 lane 不可混用；目标和源分别检查。
单条指令独立解析且无声明时保留未知 lane 类型；带声明的模块解析会检查上述寄存器类型范围。
目标 lane 可以是 `_`，源 lane 必须是寄存器，目标不能全为 `_`。目标向量中
各个实际写入的 lane 必须指向不同寄存器；参数化声明绑定后的 lane 同样受检，
只读源向量则允许重复 lane。整个访问须按“向量长度 × 元素字节数”
对齐，例如 `v8.f16` 需要 16 字节。原子性逐个标量元素成立，不保证整个
向量作为一个事务具有原子性。Half/bfloat 向量必须写 `.noftz`；
`v2/v4.f32.add` 不接受该后缀。打包类型及 `.f32` 均无 `v8` 形式。
Cache hint 仍使用末尾的 64 位 policy 布局，并遵循相同 global 地址规则。

`red.async` 在 `Red::Async...` 下提供两种彼此独立的类型化模式：

| 模式 | 合法操作/类型组合 | 形式及目标下限 |
| --- | --- | --- |
| Shared completion | `inc/dec.u32`、`min/max.{u32,s32}`、`and/or/xor.b32`、`add.{u32,s32,u64}` | `red.async.relaxed.cluster{.shared::cluster}{.mbarrier::complete_tx::bytes}.{op}.{type} [a], b, [mbar]`；PTX 8.1 / SM 90 |
| Global release | `add.{u32,s32,u64,s64}` | `red.async{.mmio}.release.{gpu\|sys}{.global}.add.{type} [a], b`；PTX 8.7 / SM 100 |

可选地址后缀在 `Red::address_qualifier` 中独立保留：shared completion 可省略为
generic 或写 `.shared::cluster`；global release 可省略为 generic 或写 `.global`。
目标地址 `a` 必须以寄存器为基址，可附带 signed 32-bit 偏移；直接符号或立即数
基址均拒绝；目标与 mbarrier 地址都要求寄存器基址。Shared 模式中已知的目标与
mbarrier 地址必须指向 shared；来源未知的 generic 寄存器地址在运行时必须指向
shared-cluster。Release 模式中已知目标必须指向 global；来源未知的 generic
寄存器地址在运行时必须指向 global。目标要求自然 4 或 8 字节对齐，mbarrier
地址要求 8 字节对齐。`.mmio` 只允许 release `.sys`，不能与 release `.gpu`
搭配。

对于 relaxed 模式，具体的 PTX Conditions 条款规定省略 completion 时默认采用
`.mbarrier::complete_tx::bytes`；显式写出时也只允许这一 mechanism。两种形式都必须
保留第三个 `[mbar]` 操作数。该具体默认规则优先于将 completion 印为必需的示意
syntax，以及描述省略时采用 end-CTA completion 的通用 Qualifiers 文字。
Release 模式仍是独立的双操作数形式，禁止显式 completion。历史 assembler 对
relaxed 省略形式的拒绝仅作为补充工具差异，不决定 frontend grammar。
前端无法证明 shared 目标与已初始化的 barrier 位于同一个远端 CTA，也不执行
reduction/transaction/synchronization 语义；这些仍是运行时义务。

其他同步标量操作/类型组合，如 `.add.s64`、64 位 `.inc/.dec`、带类型化 64 位后缀
的位操作、标量浮点 `.min/.max` 以及 `red.cas`/`red.exch`，仍不受支持。
Multimem 形式不在此覆盖范围内。

C++ 包版本为 0.7.0，consumer 需要使用匹配的已安装头文件与库重新构建。
83 个同步操作/类型及向量 cohort 分支替代原来的 92 个 legacy/`.relaxed.cta` 分支。例如
`atom.global.add.u32` 和 `atom.relaxed.cta.global.add.u32` 现在都使用
`Atom::GlobalAddU32`；读取其 `semantics`、`scope`、`state_space` 字段以及
`Atom::address_qualifier` 可恢复源码写法。
非 CAS 同步标量与向量 variant 现在通过 `operands` 提供 `NoHintOperands` 与
`WithPolicyOperands`，末尾的 policy 寄存器与普通源操作数分开；CAS 字段仍直接保存。
异步归约使用独立的 `Red::AsyncShared...` 和 `Red::AsyncRelease...` 分支，
分别有三个与两个操作数；consumer 应读取保留的地址限定符、completion、scope
及 MMIO 字段，而非假定同步布局。16 个异步分支追加在 `Red` 下。
Python wheel 版本为 0.1.0b3，因为打包的 YAML 和生成模型契约已变化。

十二个 shared class（`RedAsyncSharedIncU32`、`RedAsyncSharedDecU32`、
`RedAsyncSharedMinU32`、`RedAsyncSharedMinS32`、`RedAsyncSharedMaxU32`、
`RedAsyncSharedMaxS32`、`RedAsyncSharedAndB32`、`RedAsyncSharedOrB32`、
`RedAsyncSharedXorB32`、`RedAsyncSharedAddU32`、`RedAsyncSharedAddS32`、
`RedAsyncSharedAddU64`）现提供 instance `WithLocs<bool> completion`，替代 static
constant。读取 `object.completion.value` 获取有效 mechanism（始终为 true），读取
`.locs` 区分书写 presence：省略时 locations 为空，显式形式保留 token。
False 不属于该单值 domain，即使没有 token 也非法。普通双值 optional flag 仍遵循
既有 value/token 一致性规则。Generated C++ layout 变化要求重编译 consumer；四个
release class 以及 shared 的三个 operand payload 保持不变。清空 true completion
的 locations 是合法当前 IR 修改；supplied AST association 独立检查 retained source identity。

[默认值测试](../../../submod/resolved_ir/test/test_red_async_default.cpp) 覆盖全部
48 种 shared modifier product、64 种 release candidate（24 允许、40 拒绝）、
target/version 边界、单值 domain mutation，以及真实 AST 销毁后的 copy/clone。
[安装 consumer](../../../examples/resolved_ir_consumer/main.cpp) 在无 source storage 时
读取全部十二个迁移字段并保留 release control。这些检查不证明 barrier 已初始化或
运行时 completion。


### 标量 F16 atomic 源成员

`AtomGlobalAddNoftzF16::src` 与 `RedGlobalAddNoftzF16::src` 在普通和 cache-policy
两个布局中都使用 `WithLocs<RegOrImm>`。consumer 需要重新构建，并通过 `get_if`
或 `visit` 选择 `ResolvedRegisterRef` / `ResolvedImmediate`；既有类身份、成员范围和
布局顺序不变。cache policy 仍是独立的 B64 寄存器操作数，atomic 目标仍保留
register-or-sink 契约。

只有这四个标量 F16 源角色继承浮点常量使用权限，逻辑 F16 与 SameWidth 保持不变；
实际 B16/F16 general 寄存器及既有合法分量继续可用。数值浮点表达式复用公共
binary16 转换器（`1.5` 得到 F16 字 `0x3e00`），整数来源不获得浮点源权限。
当前规范 F16 字不需要浮点词法历史，但必须只含 low16 位，且不带整数来源缓存或
浮点负号元数据。IEEE 符号位与 NaN payload 位是数据。这是标量数值转换，
不是 packed 原始字、成对值或 splat 构造。

既有同宽 Bit 兼容规则也接受当前规范 B16 字，例如不带整数来源或负号元数据的
`0x3e00`。这是既有 current-data 兼容性，独立于原 AST 拼写；它不增加 Bit literal
来源权限，不改变 F16 producer 或整数源排除规则。其他 Bit flag 与 packed-format
规则均不变。

普通形式仍要求 PTX6.3/SM70，global cache-policy 布局仍要求 PTX7.4/SM80。
地址、对齐、ordering、scope、`noftz`、source identity 及既有
AvailableContext/RequireCompleteContext 规则独立保留。Packed F16x2、BF16/BF16x2、
atomic 向量、compare/swap 及所有其他源权限不变。前端检查不证明 GPU 运行时行为或
opaque cache-policy 内容。

### 标量 BF16 atomic 源成员

`AtomGlobalAddNoftzBf16::src` 与 `RedGlobalAddNoftzBf16::src` 在
`no_hint`、`with_policy` 两个 layout 中均使用 `WithLocs<RegOrImm>`。
Consumer 须以匹配的 header/library 重建，并对 `.value` 使用
`std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>`。
既有两个类的 identity、成员名不变。Atomic destination 仍为 B16/Exact
register-or-sink，cache policy 仍为 B64 register。Address、alignment、qualifier、
modifier order 与 target floor 独立于该 source permission；标量 F16、packed 与
vector atomic cohort 保持各自独立契约。

四个源 key 使用逻辑 BF16、SameWidth 和既有 converter。Numeric Float 源
`1.5`、`1.0+0.5`、`0d3ff8000000000000` 与 `0f3fc00000` 产生既有
BF16 `0x3fc0` golden。实际寄存器 source 仍须满足 intrinsic type 与 bit-compatible
规则，只接受普通标量 B16 声明；SameWidth 不授权 F16/U16/S16、独立 BF16
声明、special register 或整体向量。Integer-origin literal/expression 仍被排除，
不增加 integer-immediate policy 或转换路径。

转换器存储逻辑 BF16；逻辑 BF16 的当前 payload 要求低十六位以及干净的 origin/negative flags，
不要求 Float lexical history。因此 AST 销毁、copy、assignment 或 clone 后，一致的
canonical word 仍可验证；保留的错误高位、type/origin/class/provenance 或 range
事实仍然报错。Source association 独立核对 supplied AST 与 retained syntax，不认证
当前 word 的历史。同步 observer 仅在 traversal 中借用实际成员与范围。此标量
numeric 规则不增加 packed pair/splat/截断授权。前端验证 representation 与
availability，不执行 atomic update、memory ordering、cache 行为或 BF16 算术。

当前寄存器族引用保留真实声明和成员身份：删除必需的族成员索引或使其与当前拼写矛盾均无效。
当前拼写与成员索引一致的重定向、仅删除数字编号便利缓存仍有效；验证不认证旧操作数拼写。
缺少部分上下文时仍检查已知引用矛盾，同时保留现有整条指令检查的延迟规则。
交付证据必须覆盖实际生成的视图及真实借用回调，手工投影视图不能替代这些检查。

本来源许可不改变手工构造当前值的既有 Bit 容器兼容性。
