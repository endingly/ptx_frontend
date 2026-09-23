# PTX 9.3 `prefetch` and `prefetchu` coverage

The frontend models the documented ordinary and tensor-map prefetch forms in
PTX ISA 9.3 §9.7.9.16. It resolves the address and checks known state-space
provenance and target availability. Prefetching is a performance hint; the
frontend does not execute it or prove cache behavior.

With `ptxas` 13.3.73, a direct `prefetch.L1 [shared_symbol]` probe triggers
internal error C7907. The frontend follows the ISA's documented shared no-op
behavior; representative assembler acceptance probes omit that direct-symbol
case.

| Form | Address contract | Availability |
| --- | --- | --- |
| `prefetch.L1/L2 [a]` | Generic addressing to global or local; shared is a no-op | PTX 2.0 / SM 20 |
| `prefetch.global.L1/L2 [a]` | Explicit global | PTX 2.0 / SM 20 |
| `prefetch.local.L1/L2 [a]` | Explicit local | PTX 2.0 / SM 20 |
| `prefetch.global.L2::evict_last/evict_normal [a]` | Explicit global | PTX 7.4 / SM 80 |
| `prefetch.tensormap [a]` | Generic address; known direct symbols must be global tensor-map storage | PTX 8.0 / SM 90 |
| `prefetch.const.tensormap [a]` | Explicit constant tensor map | PTX 8.0 / SM 90 |
| `prefetch.param.tensormap [a]` | Explicit input parameter tensor map | PTX 8.0 / SM 90 |
| `prefetchu.L1 [a]` | Generic address to uniform cache | PTX 2.0 / SM 20 |

Known address provenance must match an explicit `.global`, `.local`, `.const`,
or `.param` suffix. The suffix-free `.tensormap` form accepts a generic address
register or a directly named global symbol; direct const/param symbols use their
explicit forms, while known local/shared symbols are outside the tensor-map
storage contract. The frontend cannot prove a register's runtime provenance.
The ISA promises a prefetch cache effect only for const/param tensor maps; this
model does not infer one for generic addresses. `ptxas` 13.3.73 accepts direct
global/local/shared symbols for the suffix-free form and rejects direct
const/param symbols; the frontend restricts known local/shared provenance to
the documented storage contract. Tensor-map forms have no `.L1`/`.L2` suffix.
`prefetchu` has only `.L1` and no state-space suffix; a generic address
mapping to constant, local, or shared data performs no operation according to
the ISA. The model rejects unsupported level, space, and eviction combinations.
