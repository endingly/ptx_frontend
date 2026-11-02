# Matrix fragment coverage

### Named matrix fragments

Eligible MMA fragments, WGMMA destinations/register-A fragments, and WMMA MMA
A/B/C fragments accept an ordinary declared `.reg.v2` or `.reg.v4` whole
identifier where the exact role has two or four physical registers. Element
policy checks each physical lane type; logical matrix element types and existing
cardinality remain independent. The complete declaration must fit within 128 bits.
This does not add named v1/v8/v128 syntax or a legal `.v4.f64` declaration.

Owned IR retains the whole declaration SymbolId and each ordered NamedProjection,
including source ranges. Current validation checks those facts after source/AST
release and after copy, assignment, or clone. A permission flag alone cannot
replace provenance. Existing legal large brace fragments remain supported.

When WGMMA register-A and shared-descriptor forms share identifier syntax, the
private resolver uses actual lexical declaration shape with paired typed
descriptors: whole vectors choose the authorized fragment path and scalar
registers choose the descriptor path. Wrong vector count/type remains an error;
unknown declarations do not prove scalarity. The public syntax-only selector
keeps its context-free contract and can report ambiguity. Selection never chooses
by target availability or by trial resolution. WMMA load/store operands and WMMA MMA destinations,
address/descriptors, scale IDs, sparse metadata, and other excluded roles retain
their existing brace/scalar/tuple rules. GPU scheduling and runtime resource
validity remain outside frontend validation.

The specification loader admits declaration-separable overlap only when every
layout cross-pair proves separation. The added proof uses an IdentifierRef-only
intersection, explicit named Element permission, an exact existing two/four-lane
count and a scalar SharedMatrixDescriptor competitor. Missing facts, Aggregate
policy, generic scalar roles or another shared syntax alternative supply no proof.
Targets and defaults do not resolve overlap. This validation proof preserves the
public selector's exact ambiguity diagnostic at the instruction range.
