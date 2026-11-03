# Block-scaled MMA selector operands

Dense and ordered-sparse block-scaled MMA retain separate A/B scale selectors,
each written as `{byte_id, thread_id}`. The logical values are U16. Register
leaves admit exactly fundamental `.b16`, `.s16` and `.u16` carriers under the
same-width integer/bit compatibility rule; they can be independently mixed with
each other or valid unsigned immediates. This does not cast/promote declarations
or infer runtime register contents. `.f16`, other widths, predicate/special
registers and instruction-only alternate declarations do not become compatible.
The same-width rule does not import ld/st/cvt widening.

The modeled cohort has 110 variants and 220 typed A/B selector contracts across
dense/ordered-sparse MXF8F6F4 (1X), MXF4 (2X), MXF4NVF4 (2X and 4X). These are
metadata inventory counts, not executed program or numerical coverage counts.
Each selector still requires exactly two scalar register/immediate leaves.
Explicit ordinary vector components retain their declaration SymbolId, optional
compact-member index, scalar lane and source ranges. Whole vectors cannot stand
for one leaf or replace the tuple; nested constructors and other cardinalities
remain unsupported. Genuine named vector roots in other operand families keep
their existing contracts.

Immediate conversion remains unsigned U16 with its original source bits. Byte
selector masks are `0b1111`, `0b0101`, `0b0001` for 1X/2X/4X; A thread IDs are
0–1 and B thread IDs 0–3. Negative, floating and overflowing literals remain
invalid. Signed register carrier permission is not permission for negative
immediate syntax, constant propagation or runtime bounds proof. The separate
sparse metadata selector, fragments, B32 scale-data roles, modifiers, targets and
availability stay unchanged.

Source resolution, source-aware validation and AST-free owned checking share
these typed contracts. Copied/cloned owned payloads permit coherent current
B16/S16/U16 identity retargeting, while wrong width/kind/whole-vector shape or
incoherent cached type/component/member provenance rejects. Source association
continues to check supplied syntax separately. No literal representation,
allocation, GPU execution or pending named matrix-fragment extension is implied.
