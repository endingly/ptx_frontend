"""Load C++ spelling/type mappings from the backend YAML specification."""

from __future__ import annotations

from enum import Enum
from functools import cache
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml
from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    EmitAlternativeBackend,
    EmitBackend,
    InstructionBackend,
    ModifierBackend,
    OperandBackend,
    RuntimeLookupKind,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CPP_BACKEND_SPEC = (
    REPO_ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
)
DEFAULT_CPP_BACKEND_SCHEMA = (
    REPO_ROOT / "instructions/schemas/ptx-cpp-backend-v1.schema.yaml"
)


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
    MEMORY_CONSISTENCIES = "memory_consistencies"
    MEMORY_SCOPES = "memory_scopes"
    VECTOR_ARITIES = "vector_arities"
    MEMORY_STATE_SPACES = (  # YAML: domains.memory_state_spaces
        "memory_state_spaces"
    )
    PARAMETER_DIRECTIONS = "parameter_directions"
    REGISTER_WIDTH_POLICIES = "register_width_policies"
    MODIFIER_VALUE_CPP_TYPES = (  # YAML: domains.modifier_value_cpp_types
        "modifier_value_cpp_types"
    )
    OPERAND_VALUE_CPP_TYPES = (  # YAML: domains.operand_value_cpp_types
        "operand_value_cpp_types"
    )
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
    SYNTAX_OPERAND_SHAPES = "syntax_operand_shapes"  # YAML: domains.syntax_operand_shapes
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

_active_backend_spec = DEFAULT_CPP_BACKEND_SPEC


def configure_cpp_backend(path: Path) -> None:
    """Select the backend specification used by subsequent model/emitter calls."""

    global _active_backend_spec
    _active_backend_spec = path.resolve()


def get_cpp_backend() -> CodegenUnit:
    """Return the configured, immutable backend model."""

    return load_cpp_backend(_active_backend_spec)


@cache
def load_cpp_backend(path: Path) -> CodegenUnit:
    """Normalize one backend YAML file into the existing backend model API."""

    raw = load_yaml(path)
    _validate_schema(path, raw)
    schema = str(raw.get("schema", ""))
    if schema != "ptx-cpp-backend/v1":
        raise ValueError(f"{path}: unsupported backend schema {schema!r}")
    if raw.get("backend") != "cpp":
        raise ValueError(f"{path}: backend must be 'cpp'")

    domains = _normalize_domains(path, raw.get("domains", {}))
    missing_domains = _REQUIRED_DOMAINS - domains.keys()
    if missing_domains:
        raise ValueError(
            f"{path}: C++ backend is missing required domains "
            f"{sorted(missing_domains)}"
        )
    instructions = _normalize_instruction_backends(
        path, raw.get("instructions", {})
    )
    includes = _normalize_includes(path, raw.get("includes"))

    return CodegenUnit(
        spec_schema=str(raw.get("spec_schema", "ptx-instr/v1")),
        backend_schema=schema,
        category=str(raw.get("category", "all")),
        namespace=str(raw.get("namespace", "ptx_frontend::resolved_ir")),
        includes=includes,
        instructions=(),
        backends=instructions,
        domains=domains,
    )


def cpp_domain(name: CppDomain) -> DomainBackend:
    """Return a required backend domain with a contextual error."""

    if not isinstance(name, CppDomain):
        raise TypeError(
            "C++ backend domain must be identified by a CppDomain member"
        )
    try:
        return get_cpp_backend().domains[name.value]
    except KeyError as error:
        raise ValueError(f"C++ backend has no domain {name.value!r}") from error


def cpp_value(domain_name: CppDomain, semantic_value: str) -> str:
    """Map one semantic value to its configured C++ spelling."""

    domain = cpp_domain(domain_name)
    try:
        return domain.values[semantic_value]
    except KeyError as error:
        raise ValueError(
            f"C++ backend domain {domain_name.value!r} has no value "
            f"{semantic_value!r}"
        ) from error


def cpp_optional_value(
    domain_name: CppDomain, semantic_value: str
) -> str | None:
    """Return an optional mapping, used for identity-preserving rewrites."""

    return cpp_domain(domain_name).values.get(semantic_value)


def cpp_default(domain_name: CppDomain) -> str:
    """Return the required default/invalid expression of one domain."""

    domain = cpp_domain(domain_name)
    if domain.default is None:
        raise ValueError(
            f"C++ backend domain {domain_name.value!r} has no default"
        )
    return domain.default


def _normalize_domains(
    path: Path, raw_domains: object
) -> dict[str, DomainBackend]:
    if not isinstance(raw_domains, dict) or not raw_domains:
        raise ValueError(f"{path}: backend domains must be a non-empty mapping")

    domains: dict[str, DomainBackend] = {}
    for name, raw_domain_object in raw_domains.items():
        if not isinstance(raw_domain_object, dict):
            raise TypeError(f"{path}: domain {name!r} must be a mapping")
        raw_values = raw_domain_object.get("values")
        if not isinstance(raw_values, dict) or not raw_values:
            raise ValueError(f"{path}: domain {name!r} has no values")

        values: dict[str, str] = {}
        for semantic_value, raw_value in raw_values.items():
            if isinstance(raw_value, str):
                cpp = raw_value
            elif isinstance(raw_value, dict) and isinstance(
                raw_value.get("cpp"), str
            ):
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
            raise TypeError(
                f"{path}: domain {name!r} runtime_lookup must be a string"
            )
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


def _normalize_instruction_backends(
    path: Path, raw_instructions: object
) -> dict[str, InstructionBackend]:
    """Retain the restored instruction-backend model for future consumers."""

    if raw_instructions is None:
        return {}
    if not isinstance(raw_instructions, dict):
        raise TypeError(f"{path}: backend instructions must be a mapping")

    result: dict[str, InstructionBackend] = {}
    for opcode, raw_instruction_object in raw_instructions.items():
        if not isinstance(raw_instruction_object, dict):
            raise TypeError(f"{path}: backend instruction {opcode!r} is invalid")
        raw_emit = raw_instruction_object.get("emit", {"kind": "direct"})
        if not isinstance(raw_emit, dict):
            raise TypeError(f"{path}: instruction {opcode!r} emit is invalid")
        alternatives = tuple(
            EmitAlternativeBackend(
                name=str(raw_alternative["name"]),
                variants=tuple(
                    str(value) for value in raw_alternative.get("variants", ())
                ),
            )
            for raw_alternative in raw_emit.get("alternatives", ())
            if isinstance(raw_alternative, dict)
        )
        emit = EmitBackend(
            kind=str(raw_emit.get("kind", "direct")),
            instance=_optional_string(raw_emit.get("instance")),
            type=_optional_string(raw_emit.get("type")),
            alternatives=alternatives,
        )
        modifiers = {
            str(name): ModifierBackend(
                field=str(raw_modifier.get("field", name)),
                cpp_type=_optional_string(raw_modifier.get("cpp_type")),
                domain=_optional_string(raw_modifier.get("domain")),
                default=_optional_string(raw_modifier.get("default")),
            )
            for name, raw_modifier in raw_instruction_object.get(
                "modifiers", {}
            ).items()
            if isinstance(raw_modifier, dict)
        }
        operands = {
            str(name): OperandBackend(
                field=str(raw_operand.get("field", name)),
                cpp_type=str(raw_operand.get("cpp_type", "Operand")),
            )
            for name, raw_operand in raw_instruction_object.get(
                "operands", {}
            ).items()
            if isinstance(raw_operand, dict)
        }
        printer = raw_instruction_object.get("printer", {})
        type_checker = raw_instruction_object.get("type_checker", {})
        visitor = raw_instruction_object.get("visitor", {})
        result[str(opcode)] = InstructionBackend(
            opcode=str(opcode),
            cpp=str(raw_instruction_object.get("cpp", opcode)),
            emit=emit,
            modifiers=modifiers,
            operands=operands,
            type_checker_rule=_mapping_optional_string(type_checker, "rule"),
            visitor_name=_mapping_optional_string(visitor, "visit_name"),
            modifier_order=_mapping_string_tuple(printer, "modifier_order"),
            operand_order=_mapping_string_tuple(printer, "operand_order"),
        )
    return result


def _normalize_includes(path: Path, raw_includes: object) -> tuple[str, ...] | None:
    if raw_includes is None:
        return None
    if not isinstance(raw_includes, list):
        raise TypeError(f"{path}: backend includes must be a list")
    return tuple(str(include) for include in raw_includes)


def _optional_string(value: Any) -> str | None:
    return None if value is None else str(value)


def _mapping_optional_string(mapping: object, key: str) -> str | None:
    if not isinstance(mapping, dict):
        return None
    return _optional_string(mapping.get(key))


def _mapping_string_tuple(mapping: object, key: str) -> tuple[str, ...]:
    if not isinstance(mapping, dict):
        return ()
    values = mapping.get(key, ())
    if not isinstance(values, (list, tuple)):
        return ()
    return tuple(str(value) for value in values)


def _validate_schema(path: Path, raw: dict[str, Any]) -> None:
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
