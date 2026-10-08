# Caller-known tensor-map facts

The frontend exposes a conditional query through the installed
`ptx_tensor_map_known_facts.hpp` header and Resolved IR library. The Python
catalog and query provide the corresponding specification-level result.
`project_tensor_known_access_context` copies a typed context from a selected
exact `Instruction` form and checker context, with diagnostics for malformed
tensor metadata. A non-tensor instruction produces no access or diagnostics.

A tensor-map operand still denotes an opaque 128-byte object. A
`TensorMapKnownFacts` value holds claims independently supplied by the caller;
it neither decodes that object nor tracks `tensormap.replace` across a control
flow graph. The facts own their values and no AST lifetime. The selected
`TensorKnownAccessContext` adapter comes from the selected typed instruction,
not a variant-name string. Its direction, mode, rank, coordinate roles,
optional reduction operation, destination/group topology and actual target
availability remain separate from descriptor claims. Known S32 coordinates and
U16 im2col information are checked *after* instruction-use conversion, with
original literal bits retained when available. A register with unknown runtime
value stays unknown.

The query reports one of `Checked`, `Violated`, `Unresolved`, or
`NotApplicable` for **each** of 22 rule families. A checked row establishes
only a relationship among supplied values. It does not certify map bytes,
address ownership, GPU bounds or synchronization. `Unresolved` names missing
facts or an undocumented relationship; `NotApplicable` means the rule does not
govern this access. Invalid enum, arity, source/bits or contradictory caller
claims yield diagnostics before dependent lookup. Missing and known zero are
distinct.

The families cover encoded element and direction; reduction operation/type;
tiled box bytes and traversal steps; packed fill, operation, geometry and
swizzle restrictions; interleave; swizzle atomicity, destination alignment and
repeating-pattern offset; 96-byte swizzle; 128-byte 8-byte flip; exact
`sm_103a` B6p2x16 store override; exact `sm_120a` cluster exclusions; store
coordinate/corner signs; im2col shape, U16 information and W/W128 profiles;
four-row gather/scatter; and canonical selected-form availability. The
`sm_103a` store rule replaces conflicting generic packed requirements, rather
than combining with them. Target/version results must come from the existing
selected variant and modifier-value availability queries, including Table 63's
W-cluster `sm_110f` family at PTX 9.0. The focused catalog contains no second
target DNF, reduction matrix or Table 33 encoding table.

Element-count dimensions, traversal steps in elements, global and tensor byte
strides, packed `Box-Size[0]`/`Tensor-Size[0]` in bytes, total accessed-box
bytes, MAP-object address, global data base, accessed-box address and shared
destination address are distinct optional inputs. Spatial im2col lower and
upper corners are signed offsets from **opposite edges**, not absolute
endpoints; their arrays have rank-minus-two arity. W width, halo and offset,
pixels-per-column and channels-per-pixel retain separate element-count roles.
No packed element-to-byte formula, replacement-stride mapping or inclusive
corner transform is guessed. An explicit zero dimension or pixel count is
reported as an unclassified geometry obligation where the fixed PTX rule does
not decide it; Driver Encode limits remain API-only.

Table 33 code 15 retains one raw code and opposite load/store interpretations.
The caller supplies a closed Table 33 enum, its original code and the result of
the canonical field-specific projection. The element claim may also state one
of two code-15 semantic interpretations: load/prefetch or store/reduce. An
absent interpretation leaves element identity and dependent packed geometry
unresolved; an opposite interpretation violates the directional identity rule
and cannot select a packed subtype. A rejected projection is diagnosed without
authenticating map bytes. C++ checks enum/code agreement through the existing
Table 33 helper; no second encoding table is introduced. The C++
renderer now separates a small public rule-metadata header from an out-of-line
private query source; both are registered in the standard generation plan.
No-swizzle atomicity use is `NotApplicable` even when a valid raw atomicity code
is supplied. An exact `sm_120a` cluster load with *applied* atomicity is
violated; a raw code alone leaves active use unresolved. Reduction membership
reuses the accepted operation/type table. Exact common U32/S32/U64/S64/F16/
BF16/F32 spellings may be compared with a separately supplied descriptor
interpretation; FTZ, TF32 and B32/B64 have no approved general conversion or
descriptor-code bridge. Unknown active atomicity, runtime peer/mask membership,
barrier completion and descriptor contents remain obligations.

The fixed PTX 9.3 manual supplies no complete raw 128-byte tensor-map field
layout. A full raw-object decoder remains a separate, unresolved scope item and
is **not** satisfied by this caller-known-facts query. Cache-hint/policy
canonical operands remain separate work. See the current
[tensor async coverage](tensor_async_coverage.md) for
delivered instruction forms and tool discrepancies.
