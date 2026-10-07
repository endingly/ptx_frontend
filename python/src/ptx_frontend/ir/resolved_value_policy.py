"""Semantic policies for resolved modifier values.

This module owns PTX-facing value categories, their Python representation, and
whether an optional modifier may supply a default. C++ domain names and
descriptor-member spellings deliberately remain in
``code_gen.resolved_value_traits``.
"""

from dataclasses import dataclass

from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.spec.model import ModifierKind


@dataclass(frozen=True)
class ResolvedModifierValuePolicy:
    """PTX semantic validation rules for one resolved modifier value kind."""

    #: Exact Python type accepted from normalized modifier values.
    python_type: type[str] | type[bool]
    #: Human-readable term retained in value-validation diagnostics.
    display_name: str
    #: Term used when a value is absent from the supported code-generation domain.
    unsupported_value_name: str
    #: Modifier-kind term retained in optional-default diagnostics.
    optional_name: str
    #: Whether source syntax may omit this modifier and use a typed default.
    supports_default: bool
    #: Preserve legacy explicit type diagnostics instead of a generic rejection.
    default_requires_explicit_type_message: bool = False
    #: Preserve whether availability type errors name the type or the domain.
    availability_requires_explicit_type_message: bool = True


_MODIFIER_VALUE_KINDS: dict[ModifierKind, ResolvedValueKind] = {
    ModifierKind.FLAG: ResolvedValueKind.BOOL,
    ModifierKind.TYPE: ResolvedValueKind.SCALAR_TYPE,
    ModifierKind.ROUNDING: ResolvedValueKind.ROUNDING_MODE,
    ModifierKind.COMPARISON: ResolvedValueKind.COMPARISON_OPERATOR,
    ModifierKind.TEST_PROPERTY: ResolvedValueKind.TEST_PROPERTY,
    ModifierKind.BOOLEAN_OP: ResolvedValueKind.BOOLEAN_OPERATOR,
    ModifierKind.CACHE: ResolvedValueKind.CACHE_OPERATOR,
    ModifierKind.EVICTION_PRIORITY: ResolvedValueKind.EVICTION_PRIORITY,
    ModifierKind.PREFETCH_SIZE: ResolvedValueKind.PREFETCH_SIZE,
    ModifierKind.SEMANTICS: ResolvedValueKind.MEMORY_CONSISTENCY,
    ModifierKind.SCOPE: ResolvedValueKind.MEMORY_SCOPE,
    ModifierKind.VECTOR: ResolvedValueKind.VECTOR_ARITY,
    ModifierKind.STATE_SPACE: ResolvedValueKind.MEMORY_STATE_SPACE,
    ModifierKind.PHASE_TYPE: ResolvedValueKind.MBARRIER_PHASE_TYPE,
    ModifierKind.MBARRIER_LAYOUT: ResolvedValueKind.MBARRIER_LAYOUT,
    ModifierKind.PROXY: ResolvedValueKind.ASYNC_PROXY_KIND,
    ModifierKind.PROXY_PAIR: ResolvedValueKind.PROXY_KIND_PAIR,
}


_STRING_POLICIES: dict[ResolvedValueKind, tuple[str, str, str, bool]] = {
    ResolvedValueKind.SCALAR_TYPE: ("scalar-type", "scalar-type", "type", True),
    ResolvedValueKind.ROUNDING_MODE: ("rounding", "RoundingMode", "rounding", True),
    ResolvedValueKind.COMPARISON_OPERATOR: ("comparison", "comparison", "comparison", False),
    ResolvedValueKind.TEST_PROPERTY: ("test property", "test property", "test property", False),
    ResolvedValueKind.BOOLEAN_OPERATOR: ("boolean", "boolean", "boolean", False),
    ResolvedValueKind.CACHE_OPERATOR: ("cache", "cache", "cache", True),
    ResolvedValueKind.EVICTION_PRIORITY: ("eviction priority", "eviction priority", "eviction priority", True),
    ResolvedValueKind.PREFETCH_SIZE: ("prefetch size", "prefetch size", "prefetch size", True),
    ResolvedValueKind.VECTOR_ARITY: ("vector", "vector", "vector", False),
    ResolvedValueKind.MEMORY_STATE_SPACE: ("state-space", "state-space", "state-space", True),
    ResolvedValueKind.MEMORY_CONSISTENCY: ("memory consistency", "memory consistency", "semantics", True),
    ResolvedValueKind.MEMORY_SCOPE: ("memory scope", "memory scope", "scope", True),
    ResolvedValueKind.MBARRIER_PHASE_TYPE: ("mbarrier phase-type", "mbarrier phase-type", "phase-type", True),
    ResolvedValueKind.MBARRIER_LAYOUT: ("mbarrier layout", "mbarrier layout", "mbarrier-layout", True),
    ResolvedValueKind.ASYNC_PROXY_KIND: ("async proxy", "async proxy", "async-proxy", True),
    ResolvedValueKind.PROXY_KIND_PAIR: ("proxy pair", "proxy pair", "proxy-pair", True),
}


_LEGACY_COMBINED_AVAILABILITY_TYPE_KINDS = frozenset(
    {
        ResolvedValueKind.MEMORY_CONSISTENCY,
        ResolvedValueKind.MEMORY_SCOPE,
        ResolvedValueKind.MBARRIER_PHASE_TYPE,
        ResolvedValueKind.MBARRIER_LAYOUT,
        ResolvedValueKind.ASYNC_PROXY_KIND,
        ResolvedValueKind.PROXY_KIND_PAIR,
    }
)


_RESOLVED_MODIFIER_VALUE_POLICIES: dict[
    ResolvedValueKind, ResolvedModifierValuePolicy
] = {
    ResolvedValueKind.BOOL: ResolvedModifierValuePolicy(
        python_type=bool,
        display_name="flag",
        unsupported_value_name="flag",
        optional_name="flag",
        supports_default=True,
        default_requires_explicit_type_message=True,
    ),
    **{
        kind: ResolvedModifierValuePolicy(
            python_type=str,
            display_name=display_name,
            unsupported_value_name=unsupported_value_name,
            optional_name=optional_name,
            supports_default=supports_default,
            default_requires_explicit_type_message=kind
            in {
                ResolvedValueKind.SCALAR_TYPE,
                ResolvedValueKind.ROUNDING_MODE,
                ResolvedValueKind.CACHE_OPERATOR,
                ResolvedValueKind.MEMORY_STATE_SPACE,
            },
            availability_requires_explicit_type_message=kind
            not in _LEGACY_COMBINED_AVAILABILITY_TYPE_KINDS,
        )
        for kind, (display_name, unsupported_value_name, optional_name, supports_default) in _STRING_POLICIES.items()
    },
}


RESOLVED_MODIFIER_VALUE_KINDS = frozenset(_RESOLVED_MODIFIER_VALUE_POLICIES)


def modifier_value_kind(modifier_kind: ModifierKind) -> ResolvedValueKind:
    """Return the resolved semantic kind for one normalized modifier kind."""

    try:
        return _MODIFIER_VALUE_KINDS[modifier_kind]
    except KeyError as error:
        raise ValueError(
            f"unsupported resolved modifier kind {modifier_kind!r}"
        ) from error


def resolved_modifier_value_policy(
    kind: ResolvedValueKind,
) -> ResolvedModifierValuePolicy:
    """Return semantic validation policy for one resolved modifier value kind."""

    try:
        return _RESOLVED_MODIFIER_VALUE_POLICIES[kind]
    except KeyError as error:
        raise ValueError(
            f"unsupported resolved modifier value kind {kind.value!r}"
        ) from error


def validate_resolved_modifier_value_type(
    kind: ResolvedValueKind,
    value: str | bool | int,
    *,
    modifier_name: str,
    default: bool,
) -> None:
    """Validate the semantic Python type and default support of a modifier value."""

    policy = resolved_modifier_value_policy(kind)
    if default and not policy.supports_default:
        raise ValueError(
            f"optional {policy.optional_name} modifier {modifier_name!r} is unsupported"
        )

    valid_type = type(value) is bool if policy.python_type is bool else isinstance(value, str)
    if valid_type:
        return

    if default and policy.default_requires_explicit_type_message:
        expected = "boolean" if policy.python_type is bool else "a string"
        raise ValueError(
            f"optional {policy.optional_name} modifier {modifier_name!r} "
            f"must have {expected} default"
        )
    if default:
        raise ValueError(
            f"optional {policy.optional_name} modifier {modifier_name!r} has "
            f"unsupported default {value!r}"
        )

    if not policy.availability_requires_explicit_type_message:
        raise unsupported_resolved_modifier_value_error(
            kind,
            value,
            modifier_name=modifier_name,
            default=False,
        )

    expected = "boolean" if policy.python_type is bool else "a string"
    raise ValueError(
        f"modifier {modifier_name!r}: {policy.display_name} value must be {expected}"
    )


def unsupported_resolved_modifier_value_error(
    kind: ResolvedValueKind,
    value: str | bool | int,
    *,
    modifier_name: str,
    default: bool,
) -> ValueError:
    """Build the stable diagnostic for a value absent from the supported domain."""

    policy = resolved_modifier_value_policy(kind)
    if default:
        return ValueError(
            f"optional {policy.optional_name} modifier {modifier_name!r} has "
            f"unsupported default {value!r}"
        )
    return ValueError(
        f"modifier {modifier_name!r}: unsupported {policy.unsupported_value_name} value {value!r}"
    )
