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
spelling，并对每个 space 提供 `.u32`/`.u64` 双向 form。global/local/shared 需要 PTX 2.0 /
SM 20，const 需要 PTX 3.1 / SM 20，param 需要 PTX 7.7 / SM 70。保留 public `GlobalU64` 与
`ToGlobalU64` variant 名称。explicit sub-qualifier 和 non-register/offset address expression
刻意不在此 `cvta` slice。

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

half/bfloat/tf32 cohort 保留各自独立的 syntax 与 availability：

- Plain scalar `f16 <- f32` 接受四种 floating rounding spelling：`.rn/.rz/.rm/.rp`。special
  `.relu` 与 `.satfinite` spelling 使用 `.rn/.rz`：`.relu` 自 PTX 7.0 / SM 80；`.satfinite`
  自 PTX 8.1，PTX availability note 未给出额外 SM minimum。
- `bf16 <- f32` 与 packed `bf16x2 <- f32,f32` cohort 自 PTX 7.0 / SM 80。`f32 <- bf16` 自
  PTX 7.1 / SM 80，其 `.ftz` form 自 PTX 7.8 / SM 90；其余 ordinary BF16 direction 自
  PTX 7.8 / SM 90。
- `f16x2 <- f32,f32` 与 `bf16x2 <- f32,f32` 使用两个 scalar `.f32` source 与 packed
  physical destination。stochastic `.rs` form 额外要求 `.b32` register `rbits`，并仅限 PTX
  8.7 的 exact `sm_100a` 或 `sm_103a`。
- `tf32 <- f32` 保留独立 `.rna` 和 `.rn/.rz` form。base destination 为 PTX 7.0 / SM 80；
  directed ReLU cohort 为 PTX 7.8 / SM 90。`.satfinite` 有独立的较晚 availability，包括
  directed `.rn/.rz` form 的 PTX 8.6 / SM 100。

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
- x4 FP8、FP4、FP6 form 是来自四元素 `.f32` register vector 的 `.rs` conversion。它们要求
  独立 `.b32` `rbits` register、强制 `.satfinite`，并只允许 PTX 8.7 exact `sm_100a` 或
  `sm_103a` target。
- `ue8m0x2` 支持指定的 `.rz/.rp` f32 与 packed-BF16 input，以及 `.rn` BF16 output form，
  并使用对应 low-bit target alternative。
- `s2f6x2` 为 PTX 9.1。f32 与 BF16 x2 direction 保留强制 `.rn.satfinite`；BF16 x2 result
  direction 保留适用的 optional flag。这些 form 仅限 `sm_100a`、`sm_103a`、`sm_110a`、
  `sm_120a` 或 `sm_121a`。
- `.scaled::n2::ue8m0` spelling 选择带额外 `.b16` scale-factor operand 的匹配 layout；未写
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

独立的 [installed consumer](../../examples/conversion_consumer/README.md)
使用安装包的公开转换 API，并在源码和 syntax AST 销毁后验证 owned module。

支持的 conversion scope 即上文列出的 form。仍未覆盖的 `cvta` boundary 是 explicit
state-space sub-qualifier spelling，以及 variable-address 或 variable-plus-offset operand。
frontend 不执行 runtime conversion、address mapping 或 CTA-rank semantic。

记录的探针使用 CUDA 13.1 `ptxas` V13.1.115，其最高接受 PTX version 为 9.1。可用
`ptxas --version` 以及 `ptxas -arch=<target> <fixture>.ptx -o <fixture>.cubin` 复现。
ordinary matrix 记录 140 个 accepted case 和四个 8-bit integer-width discrepancy。这些
observation 是 compiler 工具证据，不能替代 PTX 9.3，也不能以该工具版本验证 PTX 9.2 form。
