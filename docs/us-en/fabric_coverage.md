# Fabric/CFT coverage

The frontend recognizes the six device-side Fabric Transport source families and
the six fabric-proxy fence forms in the fixed [PTX ISA 9.3 (CUDA 13.3)](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fabric-instructions).
They require PTX 9.3 and SM 100 or newer. One canonical `fabric` opcode produces
exact, owned instruction classes. The bracketed CFT handle is a typed value with
named `endpoint` (32 bits), `data_offset` (64 bits), and, for counted forms,
`counter_offset` (64 bits). Each component keeps its register binding and source
location. Current support accepts integer/bit registers of the required width;
the fixed manual does not define a literal domain for handle components.

| Family | Source contract | Completion and topology |
| --- | --- | --- |
| `fabric.try_get` | `.async.shared::cta.relaxed.sys.b128`; `[dst]`, two-field source handle, `size:u32`, `[bar]` | Unicast; `mbarrier::complete_tx::bytes.mbarrier::report::fabric`; writes CTA shared |
| `fabric.try_put` | `.async[.multimem].shared::cta.relaxed.sys.b128`; two-field destination handle, shared source, size, barrier; optional final `.cp_mask` `bytemask:u16`, or counted three-field destination handle | Unicast or multicast; `mbarrier::complete_tx::16B.mbarrier::report::fabric`; reads CTA shared |
| `fabric.try_red` | Same direction and handle choices as put, with `and/or/xor`, `min/max`, or `add` and their exact PTX type sets | Unicast or multicast; `mbarrier::complete_tx::16B.mbarrier::report::fabric`; reads CTA shared |
| `fabric.try_pullred` | `.async.multimem.shared::cta.relaxed.sys.<op>.<type>.sync`; shared destination, two-field source handle, size, barrier, immediate `0xffffffff` member mask | Multicast; `mbarrier::complete_tx::bytes.mbarrier::report::fabric`; writes CTA shared |
| `fabric.submit` | Zero operands; optional `.op_restrict::fetching` | Submits this thread's issued operations; fetching restricts to get/pullred |
| `fabric.wait` | `.sync_restrict::reads`, zero operands | Partial completion of submitted CTA-shared reads, not whole-operation completion |

For `try_red`, bitwise operations allow `b32/b64`; min/max allow
`u32/s32/u64/s64/f16/bf16`; add allows `u32/u64/f16/bf16/f32/f64`.
`try_pullred` has the same bitwise set, min/max also allows `e4m3/e5m2`,
and ordinary add allows `u32/u64/f16/bf16/f32`. Its `.add.acc::f16`
allows `e4m3/e5m2`, while `.add.acc::f32` allows `f16/bf16`. The FP8
min/max and accumulated add forms additionally require the repository's
SM 100f/110f family capability or exact SM 120a/121a targets.
Counted put/red forms cannot combine with `.cp_mask`.
The documented early-counted spelling of unicast `try_put`
(`.async.counted::bytes.shared::cta`) resolves to a second exact source-order
class with the same CFT contract as the canonical counted spelling. There are
43 semantic rows and 44 final source-form classes for the six families.
The manual's shortened `try_red` example omits the required `.relaxed.sys`
qualifiers; the frontend follows the family Syntax definition.

The frontend checks exact suffix and operand presence, widths, register-only
handle fields, source locations, CTA-shared pointer state space, known shared
pointer alignment, immediate size multiples of 16, and the required pullred
member mask. Public `fabric_contract` metadata on each final class records
operation, endpoint topology, shared access direction, completion identity,
fabric reporting, counted mode, and required mbarrier layout v1. The completion
identity is distinct from the status report: mbarrier wait/report handling uses
the existing `mbarrier.try_wait.phase_type::primary` report form; a try
instruction has no status-result operand. A successful operation leaves the
report unchanged; failure can set its predicate and opaque report value.
For get/pullred, `complete_tx::bytes` contributes `size` bytes; for put/red,
`complete_tx::16B` contributes `size / 16` transactions.

At runtime, the data pointer and CFT data offset must be 16-byte aligned;
size is a multiple of 16 and both ranges must be in bounds. A counted
counter is an 8-byte endpoint resource at a 256-byte-aligned offset and must
not overlap destination data. A live local CTA-shared mbarrier must have been
initialized with layout v1. Endpoint existence, multicast membership, counted
and pull-reduction capabilities, operation failure reports, warp agreement,
submit-before-phase-advance ordering, final completion before grid exit, and
resource bounds are runtime obligations. Mixing fabric reports with another
reporting mechanism on one barrier has undefined behavior. Register values and protocol history
are not inferred by the static checker.

`fence.proxy.{generic::fabric,fabric::generic,fabric::fabric}.alias.{acquire,release}.sys`
has no operands. Its ordered proxy pair and acquire/release direction are
retained as typed modifiers. A fence orders proxy accesses; it does not wait
for a Fabric operation or report its status. Host-side logical-endpoint setup
and GPU execution are outside this frontend's coverage.

The [Python contract test](../../python/tests/spec/test_fabric_contract.py),
[C++ coverage test](../../submod/resolved_ir/test/test_fabric_coverage.cpp), and
[installed consumer](../../examples/fabric_consumer/main.cpp) exercise the
source and public static contracts without GPU execution.
