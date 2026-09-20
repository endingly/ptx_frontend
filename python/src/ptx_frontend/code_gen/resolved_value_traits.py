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
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.ir.resolved_value_policy import resolved_modifier_value_policy
from ptx_frontend.spec.model import CodegenUnit


@dataclass(frozen=True)
class ResolvedValueTraits:
    """C++ generation metadata for one semantic modifier value kind."""

    cpp_domain: CppDomain | None
    descriptor_member: str


_RESOLVED_MODIFIER_VALUE_TRAITS: dict[
    ResolvedValueKind,
    ResolvedValueTraits,
] = {
    ResolvedValueKind.BOOL: ResolvedValueTraits(
        cpp_domain=None,
        descriptor_member="bool_value",
    ),
    ResolvedValueKind.SCALAR_TYPE: ResolvedValueTraits(
        cpp_domain=CppDomain.SCALAR_TYPES,
        descriptor_member="scalar_type",
    ),
    ResolvedValueKind.ROUNDING_MODE: ResolvedValueTraits(
        cpp_domain=CppDomain.ROUNDING_MODES,
        descriptor_member="rounding_mode",
    ),
    ResolvedValueKind.COMPARISON_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.COMPARISON_OPERATORS,
        descriptor_member="comparison_operator",
    ),
    ResolvedValueKind.BOOLEAN_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.BOOLEAN_OPERATORS,
        descriptor_member="boolean_operator",
    ),
    ResolvedValueKind.CACHE_OPERATOR: ResolvedValueTraits(
        cpp_domain=CppDomain.CACHE_OPERATORS,
        descriptor_member="cache_operator",
    ),
    ResolvedValueKind.EVICTION_PRIORITY: ResolvedValueTraits(
        cpp_domain=CppDomain.EVICTION_PRIORITIES,
        descriptor_member="eviction_priority",
    ),
    ResolvedValueKind.PREFETCH_SIZE: ResolvedValueTraits(
        cpp_domain=CppDomain.PREFETCH_SIZES,
        descriptor_member="prefetch_size",
    ),
    ResolvedValueKind.VECTOR_ARITY: ResolvedValueTraits(
        cpp_domain=CppDomain.VECTOR_ARITIES,
        descriptor_member="vector_arity",
    ),
    ResolvedValueKind.MEMORY_STATE_SPACE: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_STATE_SPACES,
        descriptor_member="memory_state_space",
    ),
    ResolvedValueKind.MEMORY_CONSISTENCY: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_CONSISTENCIES,
        descriptor_member="memory_consistency",
    ),
    ResolvedValueKind.MEMORY_SCOPE: ResolvedValueTraits(
        cpp_domain=CppDomain.MEMORY_SCOPES,
        descriptor_member="memory_scope",
    ),
    ResolvedValueKind.MBARRIER_PHASE_TYPE: ResolvedValueTraits(
        cpp_domain=CppDomain.MBARRIER_PHASE_TYPES,
        descriptor_member="mbarrier_phase_type",
    ),
    ResolvedValueKind.MBARRIER_LAYOUT: ResolvedValueTraits(
        cpp_domain=CppDomain.MBARRIER_LAYOUTS,
        descriptor_member="mbarrier_layout",
    ),
    ResolvedValueKind.ASYNC_PROXY_KIND: ResolvedValueTraits(
        cpp_domain=CppDomain.ASYNC_PROXY_KINDS,
        descriptor_member="async_proxy_kind",
    ),
    ResolvedValueKind.PROXY_KIND_PAIR: ResolvedValueTraits(
        cpp_domain=CppDomain.PROXY_KIND_PAIRS,
        descriptor_member="proxy_kind_pair",
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
    *,
    backend: CodegenUnit,
) -> str:
    """Convert one semantic modifier value to its configured C++ expression."""

    traits = resolved_modifier_value_traits(kind)

    if resolved_modifier_value_policy(kind).python_type is bool:
        if type(value) is not bool:
            raise ValueError(f"unsupported modifier value {value!r} for {kind.value}")
        return "true" if value else "false"

    if resolved_modifier_value_policy(kind).python_type is str:
        if not isinstance(value, str) or traits.cpp_domain is None:
            raise ValueError(f"unsupported modifier value {value!r} for {kind.value}")
        return cpp_value(traits.cpp_domain, value, backend=backend)

    raise AssertionError(f"unsupported Python value type for {kind.value}")


def modifier_default_cpp_expr(
    kind: ResolvedValueKind,
    value: str | bool | int,
    *,
    backend: CodegenUnit,
) -> str:
    """Convert a supported optional-modifier default to C++."""

    if not resolved_modifier_value_policy(kind).supports_default:
        raise ValueError(f"unsupported modifier default {value!r} for {kind.value}")

    try:
        return modifier_value_cpp_expr(kind, value, backend=backend)
    except ValueError as error:
        raise ValueError(
            f"unsupported modifier default {value!r} for {kind.value}"
        ) from error


def modifier_value_default_cpp_expr(
    kind: ResolvedValueKind,
    *,
    backend: CodegenUnit,
) -> str:
    """Return the neutral C++ descriptor value for one modifier kind."""

    traits = resolved_modifier_value_traits(kind)

    if resolved_modifier_value_policy(kind).python_type is bool:
        return "false"

    if traits.cpp_domain is None:
        raise AssertionError(f"{kind.value} has no configured C++ domain")

    return cpp_default(traits.cpp_domain, backend=backend)


def modifier_descriptor_default_members(
    *,
    unselected_value_expr: str | None = None,
    backend: CodegenUnit,
) -> dict[ResolvedValueKind, str]:
    """Return C++ expressions for every unselected modifier descriptor member.

    ``None`` selects the neutral enum values required by modifier-value
    descriptors. Callers producing ``FieldView`` values pass ``std::nullopt``
    so unselected optional members remain disengaged.
    """

    if unselected_value_expr is not None:
        return {
            kind: unselected_value_expr
            for kind in _RESOLVED_MODIFIER_VALUE_TRAITS
        }
    return {
        kind: (
            "false" if traits.cpp_domain is None else cpp_default(traits.cpp_domain, backend=backend)
        )
        for kind, traits in _RESOLVED_MODIFIER_VALUE_TRAITS.items()
    }


def modifier_descriptor_members(
    kind: ResolvedValueKind,
    value_expr: str,
    *,
    unselected_value_expr: str | None = None,
    backend: CodegenUnit,
) -> dict[ResolvedValueKind, str]:
    """Return descriptor members with one semantic value selected.

    ``unselected_value_expr`` changes inactive members from their descriptor
    neutral values to a caller-supplied expression such as ``std::nullopt``.
    """

    resolved_modifier_value_traits(kind)
    members = modifier_descriptor_default_members(
        unselected_value_expr=unselected_value_expr, backend=backend,
    )

    if kind not in members:
        raise AssertionError(
            f"{kind.value} selects an unknown modifier descriptor member"
        )

    members[kind] = value_expr
    return members


def modifier_value_descriptor_members(
    kind: ResolvedValueKind,
    value: str | bool | int,
    *,
    backend: CodegenUnit,
) -> dict[ResolvedValueKind, str]:
    """Convert one semantic modifier value into descriptor member expressions."""

    return modifier_descriptor_members(
        kind,
        modifier_value_cpp_expr(kind, value, backend=backend), backend=backend,
    )
