# Matrix reference interpretations

This note records bounded frontend interpretations of conflicting clauses in the
pinned [PTX ISA 9.3 reference](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
These are project engineering interpretations, not NVIDIA-issued errata. Each
interpretation states its own scope; it does not establish whole-matrix coverage
or hardware execution conformance. [中文](../../zh-han/converage/matrix_reference_interpretations.md)

## WMMA `m8n8k4` F64 fragments

For `wmma.load`, `wmma.mma` and `wmma.store` on this shape, each thread has one
64-bit register for A and B, and two 64-bit registers for C and D. The
[fragment table](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment)
lists a conflicting single-register F64 C/D entry. We adopt two: the
[shape definition](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
makes C/D an 8×8 matrix with 64 F64 elements, requiring two elements per thread
across 32 threads. A is 8×4 and B is 4×8, requiring one element per thread.
The [store example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-st)
and [MMA example](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)
use two C/D registers and agree with this capacity argument. This resolves this
particular inconsistency; it is not a general precedence rule for examples.

```ptx
wmma.load.c.sync.aligned.m8n8k4.col.global.f64 {c0,c1}, [p], 8;
wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
wmma.store.d.sync.aligned.m8n8k4.row.global.f64 [p], {d0,d1}, 8;
```

C/D fragments occupy 16 bytes and require 16-byte memory alignment. Leading
dimensions remain layout-dependent element strides, independently checked for
minimum and divisibility. Both row/column layouts, existing memory spaces,
rounding default `.rn` and explicit `.rn/.rz/.rm/.rp` remain available. The base
floor is PTX 7.0 / `sm_80`; `shared::cta` retains its separate PTX 7.8 floor.
Ordinary scalar F64 and compatible B64 data carriers retain their existing rules.
Physical element distribution across lanes/registers remains architecture-dependent.

The canonical rules already express this interpretation. Focused regressions in
[`test_wmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wmma_coverage.cpp)
and [`test_wmma_coverage.py`](../../../python/tests/spec/test_wmma_coverage.py)
check the exact 36 static identities separately from source programs, reject
A/B pairs and C/D singletons/triples, and check independently mutated owned
fragments after syntax owners die. Parser acceptance of a brace pack alone does
not imply semantic acceptance. Owned copies/clones, supplied-source validation,
alignment, scalar width, target and stride controls remain separate checks.
