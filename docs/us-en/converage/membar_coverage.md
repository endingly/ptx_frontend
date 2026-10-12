# `membar` coverage

The frontend models the three plain memory-barrier levels from
[PTX ISA 9.3 §9.7.14.4](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-membar-fence):

| Source form | Minimum PTX | Minimum target | Owned IR scope |
| --- | --- | --- | --- |
| `membar.cta;` | 1.4 | Any | `MemoryScope::Cta` |
| `membar.gl;` | 1.4 | Any | `MemoryScope::Gpu` |
| `membar.sys;` | 2.0 | `sm_20` | `MemoryScope::Sys` |

`membar.proxy.alias;` is also modeled as a distinct, operand-free variant. It
requires PTX 7.5 and `sm_60` and orders accesses through the generic and
virtual-alias proxies in both directions. Its fixed `.proxy.alias` spelling is
retained as two fixed flags.

The four async-proxy spellings are also operand-free and retain their written
space as a typed `AsyncProxyKind` in owned IR:

| Source form | Minimum PTX | Minimum target | Extra capability |
| --- | --- | --- | --- |
| `membar.proxy.async;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.global;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.shared::cta;` | 8.0 | `sm_90` | None |
| `membar.proxy.async.shared::cluster;` | 8.0 | `sm_90` | `cluster` |

These forms order generic-proxy and async-proxy accesses in both directions;
the written state space limits that ordering. The `sm_90` floor is a conservative
inference from the ISA's async-proxy target notes and the existing
`fence.proxy.async` contract; the ISA does not state a separate
`membar.proxy.async` target floor. CUDA 13.3 `ptxas` rejects these `membar`
spellings even at `sm_90`, so assembler acceptance is not claimed. Frontend
selection, owned IR, and target checking are the modeled contract.

Each supported spelling selects a distinct `Membar` variant with no operands. The
source `.gl` token maps to the typed GPU scope, while `.gpu` remains a
different spelling and is not accepted for `membar`. The checker enforces the
PTX and target floors on owned IR. Other suffixes and operands are outside this
coverage. The frontend does not execute barriers.
