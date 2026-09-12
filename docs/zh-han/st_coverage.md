# ST 覆盖情况

本文记录 frontend 接受的 PTX 9.3 `st` form。machine-readable authority 是
`python/code_gen/resources/ptx_spec/data_movement_and_conversion.yaml`；这里是
frontend contract，不表示 simulator execution 或 GPU conformance。

规范依据为 NVIDIA PTX ISA 9.3 的 [store instruction](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st)。

## Contract

`st` 接受 generic 与显式 `.global`、`.local`、`.param`、`.shared` addressing，包含
`.param::func` return-address direction/context check。scalar type 包括至 `.f64` 的
integer、bit-size 与 floating form，以及 PTX 8.3 / SM 70 起的 `.b128`。`.b128` 搭配
`.sys` scope 要求 PTX 8.4。source 保留 PTX 的 equal-or-wider register rule：checker
验证所选 instruction width；PTX store 执行时使用 source 的 low bits。Checker 也保留已知 address alignment 和
write-permission validation。

限定 shared form `.shared::cta` 与 `.shared::cluster` 保留其 state-space identity。
它们要求 PTX 7.8；`::cta` 要求 SM 30，`::cluster` 要求具 cluster capability 的 SM 90。
未限定的 `.shared` form 保留 PTX 的 `::cta` default。

Weak（含省略）、volatile、relaxed 与 release 是不同的 semantic alternative。
Relaxed/release 必须带 scope，并允许已知 global/shared address；volatile 还会在 PTX 9.1
允许 local address。MMIO 要求 global address 与 `.sys` scope。Descriptor 记录合法 MMIO
semantic value 和 target availability，包括 PTX 9.3 / SM 75 的 release+MMIO form，因此通用
checker 不会按 `st` 分支。Cache operator 与 cache-control hint 的 semantic 限制不同。

Global cache form 覆盖文档规定的 cache operator、L1 eviction priority、L2 eviction
priority，以及带必需 `b64` cache-policy operand 的 `L2::cache_hint`。L1 eviction 要求
PTX 7.4 / SM 70，cache policy 要求 PTX 7.4 / SM 80，L2 eviction priority 要求 PTX 8.8 /
SM 100。Vector form 保留 legacy 128-bit layout 和 PTX 8.8 / SM 100 的 256-bit `.v8`
32-bit、`.v4` 64-bit layout；只有 modern layout 允许 partial store sink。当 effective address
space 已知时，modern vector/cache combination 仍保留 global-only requirement。
这些 control 对 generic spelling 也只在 provenance 未知或已知为 global 时接受；已知
local、shared、const 或 parameter address 会被拒绝。L1/L2 eviction 与 `L2::cache_hint`
可搭配 weak、relaxed、release store semantic。`.wb`/`.cg`/`.cs`/`.wt` cache operator 是
weak-only branch：可配合 cache hint，但不能配合 eviction control 或
ordered/volatile/MMIO semantic。组合 eviction qualifier 使用 canonical L1-before-L2 顺序。

PTX 9.3 并没有为普通 `st` 定义 `[address].unified` syntax。因此 frontend 会拒绝该拼写，
而不会从 declaration attribute 虚构 store-side qualifier；它既不分配 unified memory，也不放宽
原有 address-space 或 alignment diagnostic。已知 unified declaration 还是只读 store address，
包括 AST-free revalidation 时也会拒绝。

## 有意保留的边界

CUDA 13.1 `ptxas` 在 PTX 8.0 / SM 90 的探针拒绝不带 policy operand 的 cache hint，
因此 frontend 保留两者关联的操作数约束。这仅为编译器证据，不是 PTX 9.3 GPU 验证。

这里仅处理 source acceptance 和 target-aware validation，不实现 memory ordering、cache
behavior、unified allocation 或 store execution。专用 C++ ST test、Python model test 与
installed consumer 覆盖 accepted/rejected public frontend contract。
