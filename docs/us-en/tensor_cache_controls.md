# Tensor async cache controls

The canonical tensor `cp` model previously contained 178 original tensor identities and
241 original operand layouts. It adds one cache-hint sibling per identity and
two sibling layouts per original layout, for 356 tensor identities, 723 tensor
layouts, and 473 total `Cp` identities. The original 178 identities and their
layouts remain available.

For an eligible tensor load, store, reduction, or prefetch, the written
`.L2::cache_hint` follows the existing mode, multicast, and CTA-group suffixes.
The final `cache_policy` operand, when present, follows im2col information and
the multicast mask. The three source states are no hint/no policy, written
hint/no policy, and written hint/final policy. A policy without a written hint
is invalid. The hint is an instance-level source fact rather than a fixed
property of a variant. The new sibling inherits the parent's exact target,
mode, rank, destination, multicast, group, completion, coordinate and typed
operand contracts. Existing parent identities and layouts remain available.

The final policy use selects B64 through the ordinary located `reg_or_imm`
carrier and `Narrow` conversion. Scalar General B64, U64 and S64 registers are
eligible; incompatible declared type, non-General class, vector width, or a
missing declaration for a bound identity is rejected. An unbound scalar whose
type is unknown remains an obligation. Integer literals `0`, `1`,
`18446744073709551615`, and signed `-1` retain their original evaluated
64-bit source bits and source sign. The last two produce the same B64 word but
different sign provenance. No policy-bit, eviction-mode or numeric source
range decoder is implied. The policy does not alter instruction completion or
memory-consistency metadata.

Optional im2col information may be absent or present, and multicast masks and
explicit CTA groups can coexist. The equal-arity absent-info-plus-policy and
present-info-without-policy layouts are distinguished by scalar policy versus
vector information shape, not just operand count. The existing accepted
multicast/group suffix alias is preserved; arbitrary suffix permutation is
not introduced. Selected cache information must own its values and source
locations after syntax AST destruction. Owned register class, type, vector,
binding, source bits and sign remain visible to direct and full-module
validation and reference collection. The selected tensor-known-facts context
continues to describe the same direction, rank, destination, target and
availability for a cache sibling.

Formal PTX 9.3 syntax independently brackets the hint and policy and states
that policy requires the hint, so the frontend accepts a hint-only form.
An earlier 32-invocation PTXAS 13.3.73 corpus reported 13 accepted and 19
rejected, plus one dependency skip; its exact inputs are not checked in.
A fresh 26-invocation PTXAS 13.3.73 matrix on this implementation accepted
12 and rejected 14. The five load/store/reduction/prefetch hint-plus-B64-policy
cases assembled; the matching five hint-only and five policy-without-hint cases
were rejected. Additional probes accepted im2col information with policy,
multicast plus CTA group in both supported suffix orders, B64 literals `0` and
`-1`, and U64/S64 policy registers. They rejected an information-present
hint-only form, a missing multicast mask, and a B32 policy register. PTXAS
also rejected an information-omitted im2col load with final policy as
"vector expected," even though the formal optional-information syntax and
the frontend's typed layout permit it. These assembler observations do not
establish cache effects, runtime validity, or a cache-specific target floor.
The [26 exact source modules and observed outcomes](../tensor_cache_controls_ptxas.json)
retain per-case hashes, targets, and replay commands. The earlier 32-invocation
corpus cannot be reproduced from those files.

The direct-class Debug feasibility check used Clang 21, identical configuration
flags and one compiler worker for the largest changed Cp methods shard `_003`.
Current main versus this change measured source size 2,597,968 versus 4,077,717
bytes, object size 7,922,896 versus 10,386,304 bytes, wall time 6.5 versus
10.0 seconds, and peak compiler RSS 445,016 versus 521,224 KiB. Both completed
above a 1,572,864 KiB host MemAvailable guard. Existing dependencies and cache
state were reused, so these are bounded local measurements rather than clean
build timings. A separate patched-library continuation at four workers took
289.8 seconds, peaked at 5,370,380 KiB aggregate compiler RSS, and kept at
least 14,885,684 KiB host memory available without triggering the guard; it
is not a whole-build comparison with main. The historical private Cp partition
is unnecessary in the current direct-class model.

The public `query_tensor_cache_controls(const Instruction&)` query copies the selected written hint, optional located policy, diagnostics, and an applicability indicator from the exact owned form. It does not inspect opaque tensor-map bytes or infer descriptor
content. See [caller-known tensor-map facts](tensor_map_known_facts.md) for the
separate conditional descriptor query, and
[tensor async coverage](tensor_async_coverage.md) for the supported forms.
