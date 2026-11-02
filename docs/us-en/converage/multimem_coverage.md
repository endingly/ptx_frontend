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
| `multimem.ld_reduce` | Register destination, global/generic multimem address; omitted semantics or explicit `.weak` with optional scope, or `.relaxed/.acquire` with required scope; integer/float operation, type, vector, and optional accumulation precision according to the ISA tables | 8.1 / SM 90; `.acc::f32` 8.2; FP8 and `.acc::f16` 8.6 on the listed architecture families | None |
| `multimem.st` | Global/generic multimem destination and scalar or register-vector source; omitted semantics or explicit `.weak` with optional scope, or `.relaxed/.release` with required scope | 8.1 / SM 90; FP8 8.6 on the listed families | None |
| `multimem.red` | Global/generic multimem destination and scalar or register-vector source; `.relaxed/.release` and `.cta/.cluster/.gpu/.sys` are independently optional | 8.1 / SM 90 | None |
| `multimem.st.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`; `b/u/s8–64`, `f32/f64` | 9.3 / SM 100 | None |
| `multimem.red.async` | Scalar source, mandatory `.release.{gpu,sys}`, optional `.global`, `.add.{u32,s32,u64}` | 9.3 / SM 100 | None |
| `multimem.cp.async.bulk` | `.global.shared::cta.bulk_group` only; 32-bit byte size, optional 16-bit `.cp_mask` operand; default weak, explicit `.weak` or `.relaxed.scope...b128` | 9.1 / SM 90; mask SM 100; explicit ordering 9.3 on SM 90a, SM 100f/110f family targets | Bulk group |
| `multimem.cp.reduce.async.bulk` | Same direction and byte-size contract; operation/type table including required `.noftz` for half/bfloat add; optional paired `.relaxed.scope` | 9.1 / SM 90; explicit ordering 9.3 | Bulk group |

For ordinary `ld_reduce` and `st`, scope is optional with omitted semantics
(effective weak) or explicit `.weak`, and accepts `.cta/.cluster/.gpu/.sys`.
It follows semantics and precedes state space, operation, and type; for example,
`multimem.ld_reduce.gpu.add.u32` and `multimem.st.weak.sys.u32` are accepted.
Every weak/default public form exposes `WithLocs<MemoryScope> scope`. Omitted
semantics remains `MemoryConsistency::Omitted`, and omitted scope remains
`MemoryScope::None`, both without source ranges. Explicit `.weak` and scope
retain their values and source ranges. Strong ordering still requires an
explicit scope; the `red`, release-async, and bulk-family defaults are separate.

The ordinary integer reduction table restricts `add` to `u32/u64/s32`,
`min/max` to `u32/u64/s32/s64`, and `and/or/xor` to `b32/b64`.
Floating `multimem.red` permits only `add`. Floating `ld_reduce` permits
`add/min/max`, but `f32/f64` only with `add`; `.acc::f32` applies to half and
bfloat types and `.acc::f16` to FP8 types. The scalar/v2/v4/v8 type lists
are the intersection of the manual's operation, vector, and accumulation
tables, not an unrestricted product. Bulk reduction permits `add` on
`u32/s32/u64/f32/f64/f16/bf16`, `min/max` on `u32/s32/u64/s64/f16/bf16`,
`inc/dec` on `u32`, and bitwise operations on `b32/b64`.

Ordinary and release-async multimem data registers have the same width as each
instruction data element; vector lanes obey that rule individually. Integer
signedness may differ and same-size Bit carriers remain eligible. Native
F16x2 permits itself or B32, while nonfundamental BF16/FP8 formats require
matching-size Bit storage; floating kinds retain their own compatibility.
Wider B64/B128 carriers cannot supply narrower data elements. The default
reference type-agreement rule applies: distinct `multimem` instructions do not
inherit the `ld`/`st`/`cvt` wider-register exception through their suffix names.
Address carriers, bulk byte-size inputs and byte masks keep their independent
contracts. Same-width data validation does not prove runtime mappings,
completion protocols or GPU effects.

The checker uses shared typed operand, modifier, and reference traversal.
For known declarations it checks global vs shared source spaces, scalar or
vector source types, byte-address alignment, and bound metadata consistency.
Scalar integer/bit store and reduction inputs accept registers or integer
literals with use-site narrowing. `f32/f64` scalar inputs accept the existing
typed floating literals. For the existing weak/default and ordered scalar FP8
`e4m3x4/e5m2x4` stores, integer literals and evaluated integer results are
rejected. Narrowing an integer to 32 bits does not make it a Bit source for a
logical floating operand. An explicit `mov.b32` preparation followed by a B32
register source remains legal; the frontend does not insert that move.

A bare unsigned direct `0f` literal preserves its entire 32-bit word, including
encoded sign and NaN-looking bits, without floating conversion, splatting or
quieting. The current packed immediate requires at most 32 bits, no
`integer_source_bits` (including optional zero), and `is_negative=false`.
Origin-free current word edits remain legal; these checks validate current
typed data rather than authenticate erased source spelling. Signed, wrapped
and operator-based exact-word sources retain their existing restrictions.
Decimal, `0d` and floating-expression packed constructors remain unsupported:
the one-number-to-four-values mapping is reference-qualified, not a blanket
manual ban on floating literals. Broader packed source permissions are separate
from this four-branch origin repair. The six adjacent scalar `f16x2/bf16x2` source branches remain register-only;
the nine selected floating vector source fields follow the value-vector contract below.
Load destinations remain registers.

Direct and owned checks retain SameWidth, global-address and target/flavor
constraints. A valid supplied AST cannot mask invalid current origin when
instruction checks apply. Incomplete-context `AvailableContext` retains its
existing whole-instruction deferral; success there does not certify skipped
operand invariants.
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
[`test_multimem_contract.py`](../../../python/tests/spec/test_multimem_contract.py)
and [`test_multimem_coverage.cpp`](../../../submod/resolved_ir/test/test_multimem_coverage.cpp).
[`multimem_consumer`](../../../examples/multimem_consumer/main.cpp) exercises
installed public headers. These tests cover frontend acceptance and source
lifetime; they do not execute multimem instructions on a GPU.

The selected v2/v4 data register-vector roles accept ordinary named declarations and brace lists. Instruction-only logical formats use compatible fundamental Bit carriers. V8 remains brace-only; ordinary declarations cannot exceed 128 bits. Named form does not widen the SameWidth element contract.

### Floating value-vector source contract

Exactly nine existing `src` fields use `WithLocs<ResolvedValueVector>`: weak and ordered floating stores, and floating add reductions, at v2/v4/v8. The classes, selector/layout identities, element type domains, arities, defaults, availability and addresses remain unchanged. This is a public field/factory/observer payload migration from `ResolvedRegisterVector`; downstream consumers must rebuild with matching generated headers and libraries. Wrap each old register lane in `RegOrImm`, preserve locations and `ResolvedVectorSource`, and inspect alternatives with `std::get_if`. A former null register element is not an immediate or a newly permitted sink.

All64 selected type branches have typed value carriers. Exactly32 have one-value numeric producers (15 F16/F32 and17 BF16/E4M3/E5M2), and20 have an exact whole32-bit bare unsigned direct `0f` producer (F16x2/BF16x2/E4M3x4/E5M2x4). The12 E4M3x2/E5M2x2 branches have no exact16 lexical producer or adopted numeric packing constructor: literal-source and literal-source-lifetime positives remain Source_N_A. Their real register controls and independently constructed canonical low16 typed words remain valid evidence; source absence is not a typed-data ban. Numeric scalar conversion reuses the existing single-round binary64 policy; packed32 retains every written word bit without splat, quantization or quieting. Integer-valued source expressions remain incompatible with logical Float lanes.

Mixed braces may contain registers and immediates. The50 v2/v4 named branches retain actual General whole-base declaration identity, exact NamedProjection lanes, element SameWidth and the16-byte declaration cap. The14 v8 branches remain brace-only; the existing128-element projection buffer and256-bit generic capacity are unchanged, and this cohort needs at most8 lanes/128 bits. Immediate projection is not a user sink. Synchronous observers receive actual payloads and all lane ranges, including lane8, and must not retain callback references. Copies/assignments/clones reacquire views from their own owners.

Ordinary typed words require the selected logical type, in-width bits and absent integer-origin/negative flags; coherent current edits do not replay erased floating source history. Current payload/pointer/range/provenance checks and retained source-identity checks remain independent. Address alignment, selected type/modifier/target gates and incomplete-context deferral still apply. This contract does not widen the six scalar packed branches, atomic inputs, packed numeric constructors or load destinations, and makes no runtime multimem mapping or GPU execution claim. Current generation, Debug, fresh installed public-consumer and independent actual-diff evidence remain required before delivery completion.
