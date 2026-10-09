# Multimem frontend coverage (PTX ISA 9.3)

The frontend recognizes the seven `multimem` source families in the fixed
[CUDA 13.3 PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
§§9.7.9.13, 9.7.9.15, 9.7.9.26.4.4–5, and 9.7.14.8. They share one
canonical `multimem` opcode and resolve into exact, owned, typed instruction
classes. The public `ResolvedAddress` retains the original address expression,
its source locations, and declaration binding when available. A global address
or symbol does **not** prove that the runtime address is a multimem mapping.

| Source family | Checked operands and suffixes | PTX / target floor | Completion |
| --- | --- | --- | --- |
| `multimem.ld_reduce` | Register destination, global/generic multimem address; weak omission or explicit `.weak`, or `.relaxed/.acquire` with a scope; integer/float operation, type, vector, and optional accumulation precision according to the ISA tables | 8.1 / SM 90; `.acc::f32` 8.2; FP8 and `.acc::f16` 8.6 on the listed architecture families | None |
| `multimem.st` | Global/generic multimem destination and scalar or register-vector source; weak omission or explicit `.weak`, or `.relaxed/.release` with a scope | 8.1 / SM 90; FP8 8.6 on the listed families | None |
| `multimem.red` | Global/generic multimem destination and scalar or register-vector source; `.relaxed/.release` and `.cta/.cluster/.gpu/.sys` are independently optional | 8.1 / SM 90 | None |
| `multimem.st.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`; `b/u/s8–64`, `f32/f64` | 9.3 / SM 100 | None |
| `multimem.red.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`, `.add.{u32,s32,u64}` | 9.3 / SM 100 | None |
| `multimem.cp.async.bulk` | `.global.shared::cta.bulk_group` only; 32-bit byte size, optional 16-bit `.cp_mask` operand; default weak, explicit `.weak` or `.relaxed.scope...b128` | 9.1 / SM 90; mask SM 100; explicit ordering 9.3 on SM 90a, SM 100f/110f family targets | Bulk group |
| `multimem.cp.reduce.async.bulk` | Same direction and byte-size contract; operation/type table including required `.noftz` for half/bfloat add; optional paired `.relaxed.scope` | 9.1 / SM 90; explicit ordering 9.3 | Bulk group |

The ordinary integer reduction table restricts `add` to `u32/u64/s32`,
`min/max` to `u32/u64/s32/s64`, and `and/or/xor` to `b32/b64`.
Floating `multimem.red` permits only `add`. Floating `ld_reduce` permits
`add/min/max`, but `f32/f64` only with `add`; `.acc::f32` applies to half and
bfloat types and `.acc::f16` to FP8 types. The scalar/v2/v4/v8 type lists
are the intersection of the manual's operation, vector, and accumulation
tables, not an unrestricted product. Bulk reduction permits `add` on
`u32/s32/u64/f32/f64/f16/bf16`, `min/max` on `u32/s32/u64/s64/f16/bf16`,
`inc/dec` on `u32`, and bitwise operations on `b32/b64`.

The checker uses shared typed operand, modifier, and reference traversal.
For known declarations it checks global vs shared source spaces, scalar or
vector source types, byte-address alignment, and bound metadata consistency.
Scalar integer/bit store and reduction inputs accept registers or integer
literals with use-site narrowing. `f32/f64` scalar inputs accept the existing
typed floating literals. For scalar FP8 `e4m3x4/e5m2x4` stores, integer
literals supply narrowed raw 32-bit bits and `0f` literals copy their raw
32-bit pattern; decimal floats and `0d` literals are rejected. This bounded
packed-literal support follows CUDA 13.3 `ptxas` probes rather than an explicit
multimem literal rule in the manual. Scalar `f16x2/bf16x2` and all vector
inputs remain register-only; load destinations remain registers.
Bulk destinations and sources need 16-byte alignment; an immediate byte size
must be divisible by 16. A register size remains a runtime obligation. The two release-async families
require a register base (with an optional immediate offset); a symbol-only
address is rejected even if declared global.
A written `.cp_mask` requires its final byte-mask operand. Release async
store/reduce forms have **no** named completion mechanism; bulk copy/reduce
uses `BulkGroup`, with group commit/wait provided by the existing bulk-group
instructions. The frontend does not prove completion ordering, memory-range
bounds, live multimem mappings, cross-device visibility, or actual GPU effects.

Three examples in the fixed manual conflict with its Syntax and legality tables.
The frontend follows the normative tables: `multimem.red...max.f64` is rejected
because floating `red` only permits `add`; the displayed bulk-reduction example
without `.shared::cta` and with an extra `[mbar]` is rejected because the
specified form has exactly three operands and `bulk_group` completion.
The displayed copy example `multimem.cp.async.bulk.relaxed.cta.global.bulk_group.b128`
also omits the required `.shared::cta` source qualifier and is rejected.
For `multimem.red`, the manual independently defaults omitted semantics to
`.relaxed` and omitted scope to `.sys`; the owned source representation retains
omission as typed `Omitted`/`None` values with no source ranges. Source with
only one qualifier is therefore accepted. The separately recorded `ptxas` 13.3.73 probe rejects
those one-qualifier spellings. Assembler behavior does not override this
frontend source contract.

The canonical inventory and negative boundary tests live in
[`test_multimem_contract.py`](../../python/tests/spec/test_multimem_contract.py)
and [`test_multimem_coverage.cpp`](../../submod/resolved_ir/test/test_multimem_coverage.cpp).
[`multimem_consumer`](../../examples/multimem_consumer/main.cpp) exercises
installed public headers. These tests cover frontend acceptance and source
lifetime; they do not execute multimem instructions on a GPU.
