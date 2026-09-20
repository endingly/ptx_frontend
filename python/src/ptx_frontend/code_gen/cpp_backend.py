"""Load C++ spelling/type mappings from the backend YAML specification."""

from __future__ import annotations

from enum import Enum
from importlib.resources.abc import Traversable
from typing import Any

from jsonschema import Draft202012Validator

from .load_yaml import load_yaml
from .model import (
    CodegenUnit,
    DomainBackend,
    RuntimeLookupKind,
)
from ptx_frontend.spec.resources import packaged_backend_spec_schema

DEFAULT_CPP_BACKEND_SCHEMA = packaged_backend_spec_schema()


class CppDomain(str, Enum):
    """Stable identifiers for keys under backend YAML ``domains``.

    Each value is the exact key at
    ``instructions/ptx_cpp_backend_spec/ptx_frontend.yaml:domains.<value>``.
    """

    SCALAR_TYPES = "scalar_types"  # YAML: domains.scalar_types
    ROUNDING_MODES = "rounding_modes"  # YAML: domains.rounding_modes
    COMPARISON_OPERATORS = "comparison_operators"
    BOOLEAN_OPERATORS = "boolean_operators"
    CACHE_OPERATORS = "cache_operators"  # YAML: domains.cache_operators
    EVICTION_PRIORITIES = "eviction_priorities"
    PREFETCH_SIZES = "prefetch_sizes"
    MEMORY_CONSISTENCIES = "memory_consistencies"
    MEMORY_SCOPES = "memory_scopes"
    VECTOR_ARITIES = "vector_arities"
    MEMORY_STATE_SPACES = "memory_state_spaces"  # YAML: domains.memory_state_spaces
    MBARRIER_PHASE_TYPES = "mbarrier_phase_types"
    MBARRIER_LAYOUTS = "mbarrier_layouts"
    ASYNC_PROXY_KINDS = "async_proxy_kinds"
    PROXY_KIND_PAIRS = "proxy_kind_pairs"
    PARAMETER_DIRECTIONS = "parameter_directions"
    REGISTER_WIDTH_POLICIES = "register_width_policies"
    IMMEDIATE_CONVERSION_POLICIES = "immediate_conversion_policies"
    RESOLVED_VALUE_CPP_TYPES = (  # YAML: domains.resolved_value_cpp_types
        "resolved_value_cpp_types"
    )
    MODIFIER_FIELD_NAMES = "modifier_field_names"  # YAML: domains.modifier_field_names
    SYNTAX_MODIFIER_PRESENCE = (  # YAML: domains.syntax_modifier_presence
        "syntax_modifier_presence"
    )
    SYNTAX_OPERAND_PRESENCE = (  # YAML: domains.syntax_operand_presence
        "syntax_operand_presence"
    )
    SYNTAX_OPERAND_LAYOUT_KINDS = (  # YAML: domains.syntax_operand_layout_kinds
        "syntax_operand_layout_kinds"
    )
    SYNTAX_OPERAND_SHAPES = (
        "syntax_operand_shapes"  # YAML: domains.syntax_operand_shapes
    )
    RESOLVED_VALUE_KINDS = "resolved_value_kinds"  # YAML: domains.resolved_value_kinds
    RESOLVED_OPERAND_ROLES = (  # YAML: domains.resolved_operand_roles
        "resolved_operand_roles"
    )
    RESOLVED_OPERAND_ACCESS = (  # YAML: domains.resolved_operand_access
        "resolved_operand_access"
    )
    RESOLVED_OPERAND_SHAPES = (  # YAML: domains.resolved_operand_shapes
        "resolved_operand_shapes"
    )
    RESOLVED_OPERAND_TYPE_EXPRESSION_KINDS = (
        # YAML: domains.resolved_operand_type_expression_kinds
        "resolved_operand_type_expression_kinds"
    )
    RESOLVED_MODIFIER_DEFAULT_KINDS = (  # YAML: domains.resolved_modifier_default_kinds
        "resolved_modifier_default_kinds"
    )
    CHECKER_MODIFIER_VALUE_KINDS = (  # YAML: domains.checker_modifier_value_kinds
        "checker_modifier_value_kinds"
    )
    SPECIAL_REGISTER_KINDS = (  # YAML: domains.special_register_kinds
        "special_register_kinds"
    )


_REQUIRED_DOMAINS = frozenset(domain.value for domain in CppDomain)

def load_cpp_backend(path: Traversable) -> CodegenUnit:
    """Read and normalize one backend resource without requiring hashability."""

    raw = load_yaml(path)
    schema = str(raw.get("schema", ""))
    if schema == "ptx-cpp-backend/v1":
        raise ValueError(
            f"{path}: backend schema {schema!r} is retired; migrate to "
            "'ptx-cpp-backend/v2'"
        )
    if schema != "ptx-cpp-backend/v2":
        raise ValueError(f"{path}: unsupported backend schema {schema!r}")
    _validate_schema(path, raw)
    if raw.get("backend") != "cpp":
        raise ValueError(f"{path}: backend must be 'cpp'")

    domains = _normalize_domains(path, raw.get("domains", {}))
    missing_domains = _REQUIRED_DOMAINS - domains.keys()
    if missing_domains:
        raise ValueError(
            f"{path}: C++ backend is missing required domains "
            f"{sorted(missing_domains)}"
        )
    return CodegenUnit(
        spec_schema=str(raw.get("spec_schema", "ptx-instr/v1")),
        backend_schema=schema,
        domains=domains,
    )


def cpp_domain(name: CppDomain, *, backend: CodegenUnit) -> DomainBackend:
    """Return a required backend domain with a contextual error."""

    if not isinstance(name, CppDomain):
        raise TypeError("C++ backend domain must be identified by a CppDomain member")
    try:
        return backend.domains[name.value]
    except KeyError as error:
        raise ValueError(f"C++ backend has no domain {name.value!r}") from error


def cpp_value(
    domain_name: CppDomain, semantic_value: str, *, backend: CodegenUnit
) -> str:
    """Map one semantic value to its configured C++ spelling."""

    domain = cpp_domain(domain_name, backend=backend)
    try:
        return domain.values[semantic_value]
    except KeyError as error:
        raise ValueError(
            f"C++ backend domain {domain_name.value!r} has no value "
            f"{semantic_value!r}"
        ) from error


def cpp_optional_value(
    domain_name: CppDomain, semantic_value: str, *, backend: CodegenUnit
) -> str | None:
    """Return an optional mapping, used for identity-preserving rewrites."""

    return cpp_domain(domain_name, backend=backend).values.get(semantic_value)


def cpp_default(domain_name: CppDomain, *, backend: CodegenUnit) -> str:
    """Return the required default/invalid expression of one domain."""

    domain = cpp_domain(domain_name, backend=backend)
    if domain.default is None:
        raise ValueError(f"C++ backend domain {domain_name.value!r} has no default")
    return domain.default


def _normalize_domains(
    path: Traversable, raw_domains: object
) -> dict[str, DomainBackend]:
    if not isinstance(raw_domains, dict) or not raw_domains:
        raise ValueError(f"{path}: backend domains must be a non-empty mapping")

    domains: dict[str, DomainBackend] = {}
    for name, raw_domain_object in raw_domains.items():
        if name not in _REQUIRED_DOMAINS:
            raise ValueError(
                f"{path}: C++ backend has unsupported domain {name!r}; "
                "the current backend domain contract is closed"
            )
        if not isinstance(raw_domain_object, dict):
            raise TypeError(f"{path}: domain {name!r} must be a mapping")
        raw_values = raw_domain_object.get("values")
        if not isinstance(raw_values, dict) or not raw_values:
            raise ValueError(f"{path}: domain {name!r} has no values")

        values: dict[str, str] = {}
        for semantic_value, raw_value in raw_values.items():
            if isinstance(raw_value, str):
                cpp = raw_value
            elif isinstance(raw_value, dict) and isinstance(raw_value.get("cpp"), str):
                cpp = raw_value["cpp"]
            else:
                raise TypeError(
                    f"{path}: domain {name!r} value {semantic_value!r} "
                    "must be a C++ expression or an object containing 'cpp'"
                )
            values[str(semantic_value)] = cpp

        cpp_type = raw_domain_object.get("cpp_type")
        if not isinstance(cpp_type, str):
            raise TypeError(f"{path}: domain {name!r} needs cpp_type")
        default = raw_domain_object.get("default")
        if default is not None and not isinstance(default, str):
            raise TypeError(f"{path}: domain {name!r} default must be a string")
        raw_runtime_lookup = raw_domain_object.get("runtime_lookup")
        if raw_runtime_lookup is None:
            runtime_lookup = None
        elif not isinstance(raw_runtime_lookup, str):
            raise TypeError(f"{path}: domain {name!r} runtime_lookup must be a string")
        else:
            try:
                runtime_lookup = RuntimeLookupKind(raw_runtime_lookup)
            except ValueError as error:
                raise ValueError(
                    f"{path}: domain {name!r} has unsupported runtime lookup "
                    f"{raw_runtime_lookup!r}"
                ) from error
        domains[str(name)] = DomainBackend(
            cpp_type=cpp_type,
            values=values,
            default=default,
            runtime_lookup=runtime_lookup,
        )
    return domains


def _validate_schema(path: Traversable, raw: dict[str, Any]) -> None:
    schema = load_yaml(DEFAULT_CPP_BACKEND_SCHEMA)
    errors = sorted(
        Draft202012Validator(schema).iter_errors(raw),
        key=lambda error: list(error.path),
    )
    if not errors:
        return
    error = errors[0]
    location = ".".join(str(piece) for piece in error.path) or "<root>"
    raise ValueError(f"{path}:{location}: {error.message}")
