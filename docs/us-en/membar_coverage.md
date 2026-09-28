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
retained as two fixed flags; `membar.proxy.async` is outside this coverage.

Each supported spelling selects a distinct `Membar` variant with no operands. The
source `.gl` token maps to the typed GPU scope, while `.gpu` remains a
different spelling and is not accepted for `membar`. The checker enforces the
PTX and target floors on owned IR. Other suffixes and operands are outside this
coverage. The frontend does not execute barriers.
