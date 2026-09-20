"""Frontend-owned closed PTX semantic value domains.

The specification normalizer consults this catalogue before resolved IR is
built.  It deliberately describes PTX legality rather than the smaller set of
values supported by the configured C++ backend.
"""

from enum import Enum
from types import MappingProxyType

from ptx_frontend.spec.model import ModifierKind


class SemanticDomain(Enum):
    """Closed PTX vocabulary selected by a normalized modifier kind."""

    SCALAR_TYPE = "scalar_type"
    ROUNDING_MODE = "rounding_mode"
    COMPARISON_OPERATOR = "comparison_operator"
    BOOLEAN_OPERATOR = "boolean_operator"
    CACHE_OPERATOR = "cache_operator"
    EVICTION_PRIORITY = "eviction_priority"
    PREFETCH_SIZE = "prefetch_size"
    MEMORY_CONSISTENCY = "memory_consistency"
    MEMORY_SCOPE = "memory_scope"
    VECTOR_ARITY = "vector_arity"
    MEMORY_STATE_SPACE = "memory_state_space"
    MBARRIER_PHASE_TYPE = "mbarrier_phase_type"
    MBARRIER_LAYOUT = "mbarrier_layout"
    ASYNC_PROXY_KIND = "async_proxy_kind"
    PROXY_KIND_PAIR = "proxy_kind_pair"
    SPECIAL_REGISTER = "special_register"
    BOOLEAN = "boolean"


MODIFIER_SEMANTIC_DOMAINS = MappingProxyType({
    ModifierKind.FLAG: SemanticDomain.BOOLEAN,
    ModifierKind.TYPE: SemanticDomain.SCALAR_TYPE,
    ModifierKind.ROUNDING: SemanticDomain.ROUNDING_MODE,
    ModifierKind.COMPARISON: SemanticDomain.COMPARISON_OPERATOR,
    ModifierKind.BOOLEAN_OP: SemanticDomain.BOOLEAN_OPERATOR,
    ModifierKind.CACHE: SemanticDomain.CACHE_OPERATOR,
    ModifierKind.EVICTION_PRIORITY: SemanticDomain.EVICTION_PRIORITY,
    ModifierKind.PREFETCH_SIZE: SemanticDomain.PREFETCH_SIZE,
    ModifierKind.SEMANTICS: SemanticDomain.MEMORY_CONSISTENCY,
    ModifierKind.SCOPE: SemanticDomain.MEMORY_SCOPE,
    ModifierKind.VECTOR: SemanticDomain.VECTOR_ARITY,
    ModifierKind.STATE_SPACE: SemanticDomain.MEMORY_STATE_SPACE,
    ModifierKind.PHASE_TYPE: SemanticDomain.MBARRIER_PHASE_TYPE,
    ModifierKind.MBARRIER_LAYOUT: SemanticDomain.MBARRIER_LAYOUT,
    ModifierKind.PROXY: SemanticDomain.ASYNC_PROXY_KIND,
    ModifierKind.PROXY_PAIR: SemanticDomain.PROXY_KIND_PAIR,
})


SEMANTIC_DOMAIN_VALUES = MappingProxyType({
    # PTX scalar types are intentionally broader than the current C++ map.
    SemanticDomain.SCALAR_TYPE: frozenset({
        "pred", "b1", "b2", "b4", "b6", "b8", "b16", "b32", "b64", "b128",
        "u2", "u4", "u8", "u8x4", "u16", "u16x2", "u32", "u64", "s2", "s4", "s8",
        "s8x4",
        "s16", "s16x2", "s32", "s64",
        "f16", "f16x2", "f32", "f32x2", "f64", "bf16", "bf16x2", "tf32",
        "e4m3", "e4m3x2", "e4m3x4", "e5m2", "e5m2x2", "e5m2x4", "e3m2",
        "e3m2x2", "e3m2x4", "e2m3", "e2m3x2", "e2m3x4", "e2m1", "e2m1x2",
        "e2m1x4", "ue8m0x2", "s2f6x2", "b4x16_p64", "b6x16_p32", "b6p2x16",
    }),
    SemanticDomain.ROUNDING_MODE: frozenset({
        "rn", "rz", "rm", "rp", "rzi", "rni", "rmi", "rpi", "rna", "rs",
    }),
    SemanticDomain.COMPARISON_OPERATOR: frozenset({
        "eq", "ne", "lt", "le", "gt", "ge", "lo", "ls", "hi", "hs", "equ",
        "neu", "ltu", "leu", "gtu", "geu", "num", "nan",
    }),
    SemanticDomain.BOOLEAN_OPERATOR: frozenset({"and", "or", "xor"}),
    SemanticDomain.CACHE_OPERATOR: frozenset({"ca", "cg", "cs", "lu", "cv", "wb", "wt"}),
    SemanticDomain.EVICTION_PRIORITY: frozenset({
        "evict_normal", "evict_first", "evict_last", "evict_unchanged", "no_allocate",
    }),
    SemanticDomain.PREFETCH_SIZE: frozenset({"L2::64B", "L2::128B", "L2::256B"}),
    SemanticDomain.MEMORY_CONSISTENCY: frozenset({
        "weak", "volatile", "relaxed", "acquire", "release", "acq_rel", "sc",
    }),
    SemanticDomain.MEMORY_SCOPE: frozenset({"cta", "cluster", "gpu", "sys"}),
    SemanticDomain.VECTOR_ARITY: frozenset({"v2", "v4", "v8"}),
    # These source-level PTX spaces include forms the C++ mapper does not yet
    # represent, including register and texture spaces.
    SemanticDomain.MEMORY_STATE_SPACE: frozenset({
        "reg", "sreg", "const", "global", "local", "param", "param::entry",
        "param::func", "shared", "tex", "surf", "generic",
    }),
    SemanticDomain.MBARRIER_PHASE_TYPE: frozenset({
        "phase_type::primary", "phase_type::conditional",
    }),
    SemanticDomain.MBARRIER_LAYOUT: frozenset({"layout::v0", "layout::v1"}),
    SemanticDomain.ASYNC_PROXY_KIND: frozenset({
        "async", "async.global", "async.shared::cta", "async.shared::cluster",
    }),
    SemanticDomain.PROXY_KIND_PAIR: frozenset({"tensormap::generic", "async::generic"}),
    # Compatibility rules name the frontend special-register identity without
    # the source '%' prefix.  This is intentionally broader than the current
    # C++ compatibility map; code generation reports an unsupported mapping.
    SemanticDomain.SPECIAL_REGISTER: frozenset({
        "laneid", "warpid", "nwarpid", "smid", "nsmid", "gridid",
        "is_explicit_cluster", "cluster_ctarank", "cluster_nctarank",
        "lanemask_eq", "lanemask_le", "lanemask_lt", "lanemask_ge", "lanemask_gt",
        "clock", "clock_hi", "clock64", "globaltimer", "globaltimer_lo",
        "globaltimer_hi", "reserved_smem_offset_begin", "reserved_smem_offset_end",
        "reserved_smem_offset_cap", "total_smem_size", "aggr_smem_size",
        "dynamic_smem_size", "current_graph_exec", "tid", "ntid", "ctaid",
        "nctaid", "clusterid", "nclusterid", "cluster_ctaid", "cluster_nctaid",
    }),
    SemanticDomain.BOOLEAN: frozenset({True, False}),
})

# Omission sentinels are never spellable modifier values.  Keeping them beside
# the vocabulary avoids leaking sentinel branches through normalizers and
# generators.
SEMANTIC_DOMAIN_DEFAULT_ONLY_VALUES = MappingProxyType({
    SemanticDomain.CACHE_OPERATOR: frozenset({"unspecified"}),
    SemanticDomain.EVICTION_PRIORITY: frozenset({"invalid"}),
    SemanticDomain.PREFETCH_SIZE: frozenset({"none"}),
    SemanticDomain.MEMORY_CONSISTENCY: frozenset({"omitted"}),
    SemanticDomain.MEMORY_SCOPE: frozenset({"none"}),
})

# A flag can default to either Boolean state even where its source spelling
# only declares ``true``.  Generic is a legal state-space token and also the
# historical default for state-space slots that omit it from their value list.
SEMANTIC_DOMAIN_DEFAULT_VALUES = MappingProxyType({
    SemanticDomain.BOOLEAN: frozenset({True, False}),
    SemanticDomain.MEMORY_STATE_SPACE: frozenset({"generic"}),
})


def semantic_domain_for_modifier(kind: ModifierKind) -> SemanticDomain | None:
    """Return the closed PTX domain for ``kind``, if it has one."""

    return MODIFIER_SEMANTIC_DOMAINS.get(kind)


def is_semantic_value(domain: SemanticDomain, value: str | bool | int) -> bool:
    """Return whether ``value`` is spellable in the frontend PTX domain."""

    if domain is SemanticDomain.BOOLEAN:
        return type(value) is bool
    return value in SEMANTIC_DOMAIN_VALUES[domain]


def is_default_semantic_value(domain: SemanticDomain, value: str | bool | int) -> bool:
    """Return whether ``value`` is legal outside a slot's spellable values."""

    if domain is SemanticDomain.BOOLEAN:
        return type(value) is bool
    return value in SEMANTIC_DOMAIN_DEFAULT_ONLY_VALUES.get(
        domain, frozenset()
    ) or value in SEMANTIC_DOMAIN_DEFAULT_VALUES.get(domain, frozenset())
