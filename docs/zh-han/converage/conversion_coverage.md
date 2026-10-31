# Conversion 与 Address-Query 覆盖

本文定义 frontend 对 `isspacep`、`cvta`、`cvt`、`cvt.pack`、`prmt`、`mapa` 与
`getctarank` 接受并进行 target-aware validation 的 PTX 9.3 syntax。machine-readable
authority 是
`python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml`。
它是 source acceptance 与 Resolved IR validation contract，不计算 conversion result、
address mapping 或 CTA rank，也不产生 assembler output 或执行 GPU 指令。

规范依据是 NVIDIA [PTX ISA 9.3 data-movement and conversion
chapter](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions)。
下述 target minimum 是已接受 source profile 的一部分，并不推断不同 target spelling 之间的
binary translation compatibility。
带 family 的 availability 按 source profile 启用的 feature family 判断，而不只看 literal
spelling。例如 `sm_121a` 会启用 `sm_120f` family。

## Address-space predicate 与 conversion

`isspacep` 接受 predicate destination 和 generic-address register，syntax 为：

```text
isspacep.{global|local|shared|const|param} p, a
isspacep.shared::{cta|cluster} p, a
isspacep.param::entry p, a
```

该指令没有 size suffix。source 在 frontend 的 equal-or-wider register contract 下可以是
32-bit 或 64-bit fundamental integer/bit container；不能是 floating、sub-word、packed 或
`.b128` operand。unqualified form 分别表示 PTX default `.shared::cta` 与 `.param::entry`，
而不是推断 runtime address provenance。global、local、shared 为 PTX 2.0 / SM 20；const 为
PTX 3.1 / SM 20；param 为 PTX 7.7 / SM 70；explicit `shared::cta` 为 PTX 7.8 / SM 30；
explicit `shared::cluster` 为 PTX 7.8 / SM 90；explicit `param::entry` 为 PTX 8.3 / SM 70。
本 slice 只接受 register operand，variable address 及 variable-plus-offset form 不在范围内。

`cvta` 与 `cvta.to` 覆盖 `.global`、`.local`、`.shared`、`.const`、`.param` 的 unqualified
spelling，以及 `.shared::cta`、`.shared::cluster` 和 `.param::entry`，每个 space 都有
`.u32`/`.u64` 双向 form。global/local/shared 需要 PTX 2.0 / SM 20，const 需要 PTX 3.1 /
SM 20，param 需要 PTX 7.7 / SM 70，explicit shared form 需要 PTX 7.8（`::cta` 为 SM 30，
`::cluster` 为 SM 90），`param::entry` 需要 PTX 8.3 / SM 70。保留 public `GlobalU64` 与
`ToGlobalU64` variant 名称。

forward `cvta.space` 接受相同宽度的 register、selected space 中的 addressable declaration，
或该 declaration 加 immediate offset。因此既有 forward variant 的 public source field 扩展为
`ResolvedMovSource` sum type：它将既有 forward variant 的 API type 从
`ResolvedRegisterRef` 改为 `ResolvedMovSource`，caller 可用 `std::get` 或 `std::get_if` 检查
register alternative。variant 名称保持不变。`cvta.to` 仍只接受 register。declaration-bound address 在 owned
resolution 中保留其 declared space；已知 space 错误的 symbol 会被拒绝，而
`.param{::entry}` 的 symbol source 必须是 kernel input parameter。register source 仍被接受，
frontend 不会推断 runtime pointer provenance。
owned validation 还会将 direct 和 offset symbol identity 与缓存的 declaration space、
parameter role 及 function context 交叉检查。generic MOV 取地址仍保留 device parameter
materialize 为 local 的独立规则。

`mapa{.shared::cluster}.{u32|u64}`、`getctarank{.shared::cluster}.{u32|u64}`，以及它们的
generic shared spelling，保留既有 PTX 7.8 / SM 90 cluster-capability form。frontend 只验证
syntax、physical operand 与 target availability；不会证明 address 属于有效 cluster mapping，也
不会计算 CTA rank。

## `prmt`

已建模全部七种 `prmt.b32` form：unqualified generic form 及 `.f4e`、`.b4e`、`.rc8`、
`.ecl`、`.ecr`、`.rc16`。每种均为 PTX 2.0 / SM 20。destination 与三个 input 均使用
32-bit bit-container contract；每个 input 都可为 register-or-immediate。frontend 不对 selector
immediate 施加 16-bit range：generic mode 使用低 16 bit，specialized mode 使用文档规定的低位。

## `cvt` scalar 与 packed family

### Ordinary scalar conversion

ordinary scalar domain 是 12-by-12 `.u8/.u16/.u32/.u64`、`.s8/.s16/.s32/.s64`、`.bf16`、
`.f16`、`.f32` 与 `.f64` type grid。它使用 PTX integer（`rni/rzi/rmi/rpi`）和 floating
（`rn/rz/rm/rp`）rounding class，以及适用的 `.ftz`、`.sat` flag。typed check 会拒绝虽有
syntax 但 rounding class、loss direction、saturation 或 FTZ endpoint 非法的组合。scalar source
可以是 register-or-immediate；register endpoint 仍保留 PTX width contract。所有 `.f64` direction
要求 SM 13 或更高。

对于 F16 与 BF16 两种16-bit instruction format，已采用的 equal-format-size integer-valued conversion 规则允许两个方向使用 `.rni/.rzi/.rmi/.rpi`：F16 destination 可选 `.sat`，共八种；BF16 destination 不带 `.sat`，共四种。此规则不使任一方向接受 `.ftz`、`.rna` 或 `.rs`。普通 F16-to-BF16 conversion 损失 significand precision，要求显式 floating rounding；保存的默认 `Rn` 不能补足源码中省略的 modifier。已采用的反向 BF16-to-F16 omission 仍允许，可选 F16 `.sat`；两个方向均保留显式 `.rn/.rz/.rm/.rp`。Cross-half form 要求 PTX7.8／SM90。相等的是 instruction-format size，不是 physical carrier width：F16 endpoint 的合法较宽 Bit carrier 与 BF16 endpoint 的 exact Bit16 存储可同时存在。

Generated presence 保留既有“locations 非空 OR value 不等于 default”规则。因此当前非默认 integer-rounding value 即使清空 location 仍为 present；默认 `Rn` 且 locations 为空表示 absent。手动提供的 modifier view 若 `is_present=false`，则忽略保存的 mode。合法的当前 typed edit 独立于保留的 lexical history 检查。Rounding 选择请求的 GPU operation；前端不执行该 operation 或 saturation。Numeric F16 input `1.5` 在所有 mode 下仍为 source word `0x3e00`。此有界 overlapping-clause interpretation 不扩展其他 format pair、qualifier、converter permission 或 availability。

half/bfloat/tf32 cohort 保留各自独立的 syntax 与 availability：

- Plain scalar `f16 <- f32` 接受四种 floating rounding spelling：`.rn/.rz/.rm/.rp`。special
  `.relu` 与 `.satfinite` spelling 使用 `.rn/.rz`：`.relu` 自 PTX 7.0 / SM 80；`.satfinite`
  自 PTX 8.1，PTX availability note 未给出额外 SM minimum。
- `bf16 <- f32` 与 packed `bf16x2 <- f32,f32` cohort 自 PTX 7.0 / SM 80。`f32 <- bf16` 自
  PTX 7.1 / SM 80，其 `.ftz` form 自 PTX 7.8 / SM 90；其余 ordinary BF16 direction 自
  PTX 7.8 / SM 90。
- `f16x2 <- f32,f32` 与 `bf16x2 <- f32,f32` 使用两个 scalar `.f32` source 与 packed
  physical destination。stochastic `.rs` form 额外要求 scalar register 中的逻辑 `.b32` value `rbits`，并仅限 PTX
  8.7 的 exact `sm_100a` 或 `sm_103a`。
- `tf32 <- f32` 保留独立 `.rna` 和 `.rn/.rz` form。base destination 为 PTX 7.0 / SM 80；
  directed ReLU cohort 为 PTX 7.8 / SM 90。`.satfinite` 有独立的较晚 availability，包括
  directed `.rn/.rz` form 的 PTX 8.6 / SM 100。

### 逻辑格式与寄存器载体

InstructionOnly 浮点／定点格式必须使用 Bit 寄存器声明，包括 BF16、TF32、
packed low-bit、UE8M0 和 S2F6。即使物理 operand model 使用 B8/B16/B32，仍检查
选定的逻辑格式。原生 fundamental F16x2 则允许原生声明或 Bit 载体；同宽的任意
signed、unsigned、F32 寄存器不加入该集合。普通 F16/F32/F64 与整数转换保留既有兼容性。

载体 kind 与宽度独立：converted data 允许最宽到 B128 的兼容更宽 Bit 寄存器，
但 BF16/BF16x2 的 source、destination 及 TF32 destination 必须精确同宽。
Intrinsic source 使用其规范 kind 和宽度，因此 B32 intrinsic 不能作为要求精确宽度的
BF16 source。显式向量分量保留有效元素 dtype 与声明身份；完整向量不能充当 scalar data role。

Scale factor 独立编码 UE8M0x2，必须使用 Bit 存储。`rbits` 是随机 bit 数据，
保留普通兼容 S32/U32/F32 载体；两个辅助 operand 均不继承 converted-data 格式。
采用对 CVT 未限定 source-widening note 的解读，两个辅助 operand 也可放宽：factor
使用至少 16 位 Bit scalar 寄存器的低 16 位；`rbits` 使用至少 32 位、原本兼容的 scalar
寄存器低 32 位。B128 载体仍可符合要求。更宽的载体不增加 factor lane 或随机 bit 字段：
两个 factor byte 与 stochastic 分区仍位于这些逻辑 payload 内。BF16/BF16x2 converted-data
的精确宽度例外不限制独立辅助 operand。这是对宽泛 source note 的包含式解释，并非手册
另有辅助 operand 专属许可句。检查保留逻辑 operand 类型与实际声明宽度，不执行截位、
不观察运行时寄存器值，也不构造 packed 数字字面量。

### FP8、low-bit、UE8M0、S2F6、stochastic 与 scaled form

modern form 保留 physical packing，不把 instruction-only type spelling 当作 declaration type：

- FP8 x2 form 使用 `e4m3x2`/`e5m2x2`，并按 syntax direction 使用 `.b16` destination 或
  source container；FP8 destination 强制 `.rn.satfinite`，在 PTX 指定处允许 `.relu`。f32 和
  packed `f16x2` source form 独立保留原始 PTX 7.8 / SM 90 或 PTX 8.1 / SM 89 path；packed
  `bf16x2` source extension 才是 PTX 9.1 的 family-specific `sm_100f`、`sm_110f` 或
  `sm_120f` line；来自 FP8 的 BF16 x2 destination 为这些 family line 上的 PTX 9.2。
- FP4（`e2m1x2`）与 FP6（`e2m3x2`/`e3m2x2`）x2 form 使用对应 `.b8` 或 `.b16` physical
  container。它们强制 `.rn.satfinite` 的 destination form 及反向 `f16x2` form 使用文档的
  A/F target alternative，起点为 PTX 8.6 `sm_100a` 或 family-specific PTX 8.8 `sm_100f`
  path。packed-source FP4 按正常 equal-or-wider register policy 接受 `.b8` destination，
  而 FP6 保留 `.b16` minimum。packed-source 和 BF16-destination extension 保留 `sm_100f`、
  `sm_110f`、`sm_120f` family 上的 PTX 9.1/9.2 gate。
- x4 FP8、FP4、FP6 form 是以四个逻辑 `.f32` input 进行 `.rs` conversion。Brace 可混合 scalar register 与 floating constant／expression；真实 named `.v4.f32` 或 `.v4.b32` 声明（包括 compact member）提供相同的四个有序 lane。它们要求
  scalar register 中独立的逻辑 `.b32` `rbits` value、强制 `.satfinite`，并只允许 PTX 8.7 exact `sm_100a` 或
  `sm_103a` target。
- `ue8m0x2` 支持指定的 `.rz/.rp` f32 与 packed-BF16 input，以及 `.rn` BF16 output form，
  并使用对应 low-bit target alternative。
- `s2f6x2` 为 PTX 9.1。f32 与 BF16 x2 direction 保留强制 `.rn.satfinite`；BF16 x2 result
  direction 保留适用的 optional flag。这些 form 仅限 `sm_100a`、`sm_103a`、`sm_110a`、
  `sm_120a` 或 `sm_121a`。
- `.scaled::n2::ue8m0` spelling 选择带额外逻辑 `.b16` scale-factor register operand 的匹配 layout；未写
  该 spelling 时使用较短的 physical layout。scale factor 不能代替 instruction-only scalar
  type 的 register declaration。

`ScalarType` 与 `RoundingMode` 的新增值追加在既有 public domain 之后。只用于 instruction
syntax 的 low-bit/modern identity 是 instruction-only：它们可作为 modifier，不能作为 `.reg`
declaration type。

### `cvt.pack`

`cvt.pack.sat.{u16|s16}.s32 d,a,b` 有三个 operand。四 operand form 为
`cvt.pack.sat.{u2|s2|u4|s4|u8|s8}.s32.b32 d,a,b,c`。physical `d` 为 `.u32`，`a`/`b` 为
`.s32`，四 operand `c` 为 `.b32` 且可为 register 或 immediate。base form 要求 PTX 6.5 /
SM 72；`.u2/.s2/.u4/.s4` value 要求 SM 75。既有 fixed public name 与 constant 保留，新增
form 由 dynamic modifier value 选择。

## 范围与工具证据

独立的 [installed consumer](../../../examples/conversion_consumer/README.md)
使用安装包的公开转换 API，并在源码和 syntax AST 销毁后验证 owned module。

支持的 conversion scope 即上文列出的 form。任意 kernel input 有 `.ptr.const` attribute 时，
owned module validation 会拒绝该 module 中每个 forward `cvta.const.{u32|u64}`，包括 register
source；不会拒绝 `cvta.to.const`。这是 module-local declaration check，不推断 runtime pointer
provenance，也不覆盖 external/link-time declaration。frontend 不执行 runtime conversion、
address mapping 或 CTA-rank semantic。

记录的探针使用 CUDA 13.1 `ptxas` V13.1.115，其最高接受 PTX version 为 9.1，且会拒绝
PTX 9.3 profile。`sm_120` 的 64-bit forward-source probe 已通过，覆盖 supported space、
symbol-plus-offset form、sub-qualifier 与 `.to` u64 form。32-bit forward 与
minimum-profile probe 受该工具不支持的 32-bit compilation/ABI path 阻塞，因此不是 semantic
evidence。assembler 接受 device-formal `.param` probe，而 frontend 按 CVTA 的
kernel-parameter contract 拒绝它；device-formal `.local` probe 则按预期因 space mismatch
被拒绝。可用
`ptxas --version` 以及 `ptxas -arch=<target> <fixture>.ptx -o <fixture>.cubin` 复现。
ordinary matrix 记录 140 个 accepted case 和四个 8-bit integer-width discrepancy。这些
observation 是 compiler 工具证据，不能替代 PTX 9.3，也不能以该工具版本验证 PTX 9.2 form。

Scalar F16 numeric source 现经公共 binary64-to-binary16 RNE 路径转换：支持 decimal/F64 expression 与裸 unsigned exact F32，不经过 binary32 中间值。Literal 转换保留 signed zero 与 subnormal，与指令 rounding/FTZ/saturation 无关。既有 CVT source kind 及 rounding 规则不变：四个含 F16 source 的 model template 在这些独立 rounding 检查下仍没有合法 positive。不增加 integer-to-F16 literal reinterpretation；Bit16 precision 规则及 exact F32 expression 禁令保留。

普通 `cvt.pack.sat` 的 S32 input 除同宽 B32/S32/U32 register 外，也接受 integer constant 与 expression。既有 64-bit evaluator 将每个 input 窄化为低32位 S32 word，并保留原64位 integer bits／sign metadata；因此 `0x100000001U` 的 use word 为1，不要求 target-range。前端不把 input 预先饱和到 destination lane 宽度，也不预计算 packed result。Floating-valued input 仍不兼容 S32；最终 category 为 Integer 的 expression（包括 FP comparison）走 integer 路径。独立 B32 carry source 保留既有 literal／precision 契约，包括同宽 unsigned direct0f bit data；这不使 register-only stochastic `rbits` 接受 literal。Carry topology、destination width 与 target 边界不变。

公共 ABI 迁移：`CvtPackSatU8S32B32`、`CvtPackSat16S32`、`CvtPackSatSmallS32B32` 的 `src1`／`src2` 从 `WithLocs<ResolvedRegisterRef>` 改为 `WithLocs<RegOrImm>`。Consumer 必须重编译，并通过 `std::get_if<ResolvedRegisterRef>` 或 `std::get_if<ResolvedImmediate>` 检查 `.value`。六个字段原位迁移；class、modifier domain、selector 顺序不变。Copy／assignment／clone 保留 typed word、source cache、declaration identity 与 location。普通当前 IR word／reference 修改按当前事实核对；source association 仍是独立的 retained-syntax 检查。
