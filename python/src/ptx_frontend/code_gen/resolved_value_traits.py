"""C++ backend traits for semantic resolved value kinds.

This module maps typed resolved-IR value kinds to backend-specific metadata.
Resolved IR owns semantic identity; this module owns only C++ representation
details and code-generation capabilities.
"""

from dataclasses import dataclass

from ptx_frontend.code_gen.cpp_backend import (
    CppDomain,
    cpp_default,
    cpp_value,
)
from ptx_frontend.ir.resolved_ir import ResolvedValueKind


@dataclass(frozen=True)
class ResolvedValueTraits:
    """C++ generation metadata for one semantic modifier value kind."""

    cpp_domain: CppDomain | None
    descriptor_member: str
    python_type: type[str] | type[bool]
    supports_default: bool


_RESOLVED_MODIFIER_VALUE_TRAITS: dict[
    ResolvedValueKind,
    ResolvedValueTraits,
] = {
    ResolvedValueKind.BOOL: ResolvedValueTraits(
        cpp_domain=None,
        descriptor_member="bool_value",
        python_type=bool,
        supports_default=True,
    ),
    ResolvedValueKind.SCALAR_TYPE: ResolvedValueTraits(
        cpp_domain=CppDomain.SCALAR_TYPES,
        descriptor_member="scalar_type",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.ROUNDING_MODE: ResolvedValueTraits(
        cpp_domain=CppDomain.ROUNDING_MODES,
        descriptor_member="rounding_mode",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.COMPARISON_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.COMPARISON_OPERATORS,
        descriptor_member="comparison_operator",
        python_type=str,
        supports_default=False,
    ),
    ResolvedValueKind.BOOLEAN_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.BOOLEAN_OPERATORS,
        descriptor_member="boolean_operator",
        python_type=str,
        supports_default=False,
    ),
    ResolvedValueKind.CACHE_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.CACHE_OPERATORS,
        descriptor_member="cache_operator",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.EVICTION_PRIORITY: ResolvedValueTraits(
        cpp_domain=CppDomain.EVICTION_PRIORITIES,
        descriptor_member="eviction_priority",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.PREFETCH_SIZE: ResolvedValueTraits(
        cpp_domain=CppDomain.PREFETCH_SIZES,
        descriptor_member="prefetch_size",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.VECTOR_ARITY: ResolvedValueTraits(
        cpp_domain=CppDomain.VECTOR_ARITIES,
        descriptor_member="vector_arity",
        python_type=str,
        supports_default=False,
    ),
    ResolvedValueKind.MEMORY_STATE_SPACE: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_STATE_SPACES,
        descriptor_member="memory_state_space",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.MEMORY_CONSISTENCY: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_CONSISTENCIES,
        descriptor_member="memory_consistency",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.MEMORY_SCOPE: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_SCOPES,
        descriptor_member="memory_scope",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.MBARRIER_PHASE_TYPE: ResolvedValueTraits(
        cpp_domain=CppDomain.MBARRIER_PHASE_TYPES,
        descriptor_member="mbarrier_phase_type",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.MBARRIER_LAYOUT: ResolvedValueTraits(
        cpp_domain=CppDomain.MBARRIER_LAYOUTS,
        descriptor_member="mbarrier_layout",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.ASYNC_PROXY_KIND: ResolvedValueTraits(
        cpp_domain=CppDomain.ASYNC_PROXY_KINDS,
        descriptor_member="async_proxy_kind",
        python_type=str,
        supports_default=True,
    ),
    ResolvedValueKind.PROXY_KIND_PAIR: ResolvedValueTraits(
        cpp_domain=CppDomain.PROXY_KIND_PAIRS,
        descriptor_member="proxy_kind_pair",
        python_type=str,
        supports_default=True,
    ),
}


RESOLVED_MODIFIER_VALUE_KINDS = frozenset(_RESOLVED_MODIFIER_VALUE_TRAITS)


def resolved_modifier_value_traits(
    kind: ResolvedValueKind,
) -> ResolvedValueTraits:
    """Return C++ generation metadata for one modifier value kind."""

    try:
        return _RESOLVED_MODIFIER_VALUE_TRAITS[kind]
    except KeyError as error:
        raise ValueError(
            f"unsupported resolved modifier value kind {kind.value!r}"
        ) from error


def modifier_value_cpp_expr(
    kind: ResolvedValueKind,
    value: str | bool | int,
) -> str:
    """Convert one semantic modifier value to its configured C++ expression."""

    traits = resolved_modifier_value_traits(kind)

    if traits.python_type is bool:
        if type(value) is not bool:
            raise ValueError(f"unsupported modifier value {value!r} for {kind.value}")
        return "true" if value else "false"

    if traits.python_type is str:
        if not isinstance(value, str) or traits.cpp_domain is None:
            raise ValueError(f"unsupported modifier value {value!r} for {kind.value}")
        return cpp_value(traits.cpp_domain, value)

    raise AssertionError(f"unsupported Python value type for {kind.value}")


def modifier_default_cpp_expr(
    kind: ResolvedValueKind,
    value: str | bool | int,
) -> str:
    """Convert a supported optional-modifier default to C++."""

    traits = resolved_modifier_value_traits(kind)

    if not traits.supports_default:
        raise ValueError(f"unsupported modifier default {value!r} for {kind.value}")

    try:
        return modifier_value_cpp_expr(kind, value)
    except ValueError as error:
        raise ValueError(
            f"unsupported modifier default {value!r} for {kind.value}"
        ) from error


def modifier_value_default_cpp_expr(
    kind: ResolvedValueKind,
) -> str:
    """Return the neutral C++ descriptor value for one modifier kind."""

    traits = resolved_modifier_value_traits(kind)

    if traits.python_type is bool:
        return "false"

    if traits.cpp_domain is None:
        raise AssertionError(f"{kind.value} has no configured C++ domain")

    return cpp_default(traits.cpp_domain)


_MODIFIER_DESCRIPTOR_MEMBERS: tuple[
    tuple[str, CppDomain | None],
    ...,
] = (
    ("bool_value", None),
    ("scalar_type", CppDomain.SCALAR_TYPES),
    ("rounding_mode", CppDomain.ROUNDING_MODES),
    ("comparison_operator", CppDomain.COMPARISON_OPERATORS),
    ("boolean_operator", CppDomain.BOOLEAN_OPERATORS),
    ("cache_operator", CppDomain.CACHE_OPERATORS),
    ("eviction_priority", CppDomain.EVICTION_PRIORITIES),
    ("prefetch_size", CppDomain.PREFETCH_SIZES),
    ("vector_arity", CppDomain.VECTOR_ARITIES),
    ("memory_state_space", CppDomain.MEMORY_STATE_SPACES),
    ("memory_consistency", CppDomain.MEMORY_CONSISTENCIES),
    ("memory_scope", CppDomain.MEMORY_SCOPES),
    ("mbarrier_phase_type", CppDomain.MBARRIER_PHASE_TYPES),
    ("mbarrier_layout", CppDomain.MBARRIER_LAYOUTS),
    ("async_proxy_kind", CppDomain.ASYNC_PROXY_KINDS),
    ("proxy_kind_pair", CppDomain.PROXY_KIND_PAIRS),
)


def modifier_descriptor_default_members() -> dict[str, str]:
    """Return neutral C++ values for every modifier descriptor member."""

    return {
        member: ("false" if domain is None else cpp_default(domain))
        for member, domain in _MODIFIER_DESCRIPTOR_MEMBERS
    }


def modifier_descriptor_members(
    kind: ResolvedValueKind,
    value_expr: str,
) -> dict[str, str]:
    """Return descriptor members with one semantic value selected."""

    traits = resolved_modifier_value_traits(kind)
    members = modifier_descriptor_default_members()

    if traits.descriptor_member not in members:
        raise AssertionError(
            f"{kind.value} selects unknown descriptor member "
            f"{traits.descriptor_member!r}"
        )

    members[traits.descriptor_member] = value_expr
    return members


def modifier_value_descriptor_members(
    kind: ResolvedValueKind,
    value: str | bool | int,
) -> dict[str, str]:
    """Convert one semantic modifier value into descriptor member expressions."""

    return modifier_descriptor_members(
        kind,
        modifier_value_cpp_expr(kind, value),
    )
