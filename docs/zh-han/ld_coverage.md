# LD 覆盖情况

本文记录 frontend 接受的 PTX 9.3 `ld` 与 `ld.global.nc` form。machine-readable
authority 是 `python/code_gen/resources/ptx_spec/data_movement_and_conversion.yaml`；
本文描述 public boundary，不表示 simulator execution 或 GPU conformance。

规范依据为 NVIDIA PTX ISA 9.3 的 [load instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld)
和 [non-coherent global load](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-ld-global-nc)。

## Contract

`ld` 接受 generic 以及显式 `.const`、`.global`、`.local`、`.param`、`.shared`
addressing，包含文档规定的 `.param::entry` 与 `.param::func` direction/context
rule。scalar type 包括至 `.f64` 的 integer、bit-size 和 floating form，以及 PTX 8.3 /
SM 70 起的 `.b128`。`.b128` 搭配 `.sys` scope 要求 PTX 8.4。destination 保留 PTX
的 equal-or-wider register rule；address 可静态确定时会检查 alignment。

限定 shared form `.shared::cta` 与 `.shared::cluster` 保留其 state-space identity。
它们要求 PTX 7.8；`::cta` 要求 SM 30，`::cluster` 要求具 cluster capability 的 SM 90。
省略 shared sub-qualifier 时保留 PTX 的 `::cta` default，不会伪造 cluster address。

Weak（含省略）、volatile、relaxed 和 acquire 是不同的 semantic alternative。
Relaxed/acquire 必须带 scope，并允许已知的 global/shared address；volatile 还会在 PTX
9.1 允许 local address。MMIO 要求 global address 与 `.sys` scope；其合法 semantic
alternative 和 target minimum 由生成的 memory-consistency descriptor 表达，而不是由
opcode-specific checker branch 表达。Cache operator 与 cache-control hint 的 semantic
限制不同，具体见下文。

Global cache form 覆盖文档规定的 cache operator、L1 eviction priority、L2 eviction
priority、L2 prefetch size，以及带必需 `b64` cache-policy operand 的 `L2::cache_hint`。
L1 eviction 要求 PTX 7.4 / SM 70；L2 prefetch 的 64B/128B 要求 PTX 7.4 / SM 75，
256B 还要求 SM 80；cache policy 要求 PTX 7.4 / SM 80；L2 eviction priority 要求
PTX 8.8 / SM 100，且仅允许 256-bit vector。Vector form 保留
legacy 128-bit layout 和 PTX 8.8 / SM 100 的 256-bit `.v8` 32-bit、`.v4` 64-bit
layout；只有 modern layout 允许 partial load sink。
这些 control 对 generic spelling 也适用，但 provenance 未知或已知为 global 时才会接受；
已知 local、shared、const 或 parameter address 会被拒绝。Eviction、cache hint 与 prefetch
可搭配 weak、relaxed、acquire load semantic；不含 eviction/cache hint 的 prefetch 还允许
volatile。`.ca`/`.cg`/`.cs`/`.lu`/`.cv`
cache operator 则是独立的 weak-only branch：可在 PTX 允许处配合 cache hint/prefetch，
但不能配合 eviction control 或 ordered/volatile/MMIO semantic。

`ld.global.nc` 仅为 explicit-global，并覆盖完整的 scalar/vector type 与 cache-control
family，包括 `.b128`、cache policy、prefetch size、L1/L2 eviction priority。它与普通
global load 保留相同的 global-only modern-vector limit 和 target minimum，不会静默退化为
coherent `ld.global`；其基础 scalar/vector form 要求 PTX 3.1 / SM 32。
其 cache-operator branch 在 `.nc` 之前，且与 L1/L2 eviction branch 刻意保持 disjoint；
每种 cache-hint form（包括与 prefetch 组合）都要求 cache-policy operand，单独 prefetch
不要求该 operand。普通 LD 接受 canonical L1-before-L2 顺序，也接受 PTX 示例中的
L2-before-L1 alias；NC 只接受 L1-before-L2。

当 address 引用带 PTX `.attribute(.unified(...))` 的 declaration 时，已接纳的普通 `ld`
form 必须拼写 `[address].unified`；suffix 本身要求 PTX 8.0 / SM 90 及 global 或 generic
address。该 qualifier 在 CST、Syntax AST、Resolved IR 和 checker
operand view 中均为 typed payload；validation 在 source/AST release 后使用 owned declaration
identity。反之，已知 non-unified declaration 及语法不允许的 LD form 都会拒绝 `.unified`。

## 有意保留的边界

CUDA 13.1 `ptxas` 在 PTX 8.0 / SM 90 的探针拒绝不带 policy operand 的 LD/NC cache
hint，因此 frontend 保留两者关联的操作数约束。这仅为编译器证据，不是 PTX 9.3 GPU 验证。

这里仅处理 source acceptance 和 target-aware validation，不分配 unified memory、不实现
ordering、不预测 cache behavior，也不模拟 load execution。专用 C++ LD test、Python model test
与 installed-package consumer 通过 public frontend contract 覆盖 accepted/rejected form。
