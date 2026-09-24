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
lane 仅接受 `.b32`。每个向量内 bit 类型
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
| Shared completion | `inc/dec.u32`、`min/max.{u32,s32}`、`and/or/xor.b32`、`add.{u32,s32,u64}` | `red.async.relaxed.cluster{.shared::cluster}.mbarrier::complete_tx::bytes.{op}.{type} [a], b, [mbar]`；PTX 8.1 / SM 90 |
| Global release | `add.{u32,s32,u64,s64}` | `red.async{.mmio}.release.{gpu\|sys}{.global}.add.{type} [a], b`；PTX 8.7 / SM 100 |

可选地址后缀在 `Red::address_qualifier` 中独立保留：shared completion 可省略为
generic 或写 `.shared::cluster`；global release 可省略为 generic 或写 `.global`。
目标地址 `a` 必须以寄存器为基址，可附带 signed 32-bit 偏移；直接符号或立即数
基址均拒绝；目标与 mbarrier 地址都要求寄存器基址。Shared 模式中已知的目标与
mbarrier 地址必须指向 shared；来源未知的 generic 寄存器地址在运行时必须指向
shared-cluster。Release 模式中已知目标必须指向 global；来源未知的 generic
寄存器地址在运行时必须指向 global。目标要求自然 4 或 8 字节对齐，mbarrier
地址要求 8 字节对齐。`.mmio` 只允许 release `.sys`，不能与 release `.gpu`
搭配。Shared completion 后缀必须显式写出：尽管 ISA Conditions 文字描述了
省略时的默认值，PTX 9.3 语法及 `ptxas` 均拒绝省略。前端无法静态证明 shared
目标与已初始化的 barrier 位于同一个远端 CTA；这属于运行时义务。

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
