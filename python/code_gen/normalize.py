"""Normalization of PTX ISA YAML into the typed code-generation model."""

from __future__ import annotations

import re
from typing import Any

from code_gen.load_yaml import expand_value_refs
from code_gen.model import (
    AddressAlignmentConstraint,
    ImmediateMultipleOfConstraint,
    ImmediateRangeConstraint,
    ImmediateValueConstraint,
    InstructionSpec,
    MemoryConsistencyConstraint,
    MemoryVectorConstraint,
    ModifierSpec,
    ModifierValueSpec,
    OperandLayoutSpec,
    OperandLayoutKind,
    OperandParameterConstraint,
    OperandRegisterWidthPolicy,
    OperandSpec,
    OperandStateSpaceExpression,
    OperandStateSpaceValue,
    OperandTypeCompatibilitySpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    OperandVectorArityExpression,
    OperandVectorTypePolicy,
    VariantSpec,
)
from ir.syntax_ast import OPERAND_SYNTAX_SHAPES


_MODIFIER_TYPE_EXPR = re.compile(
    r"modifier\(([A-Za-z_][A-Za-z0-9_]*)\)"
)
_UNSUPPORTED_TYPE_EXPR_FUNCTIONS = ("same_as", "one_of", "same_size_as")
_STATE_SPACES = frozenset(
    {
        "reg",
        "sreg",
        "const",
        "global",
        "local",
        "param",
        "shared",
        "tex",
        "surf",
        "generic",
    }
)
_AVAILABILITY_TARGET = re.compile(r"sm_([1-9][0-9]*)([af]?)$")
_AVAILABILITY_FAMILY = re.compile(r"sm_[1-9][0-9]*f$")
UINT32_MAX = (1 << 32) - 1


def validate_availability_sm_version(
    value: object, *, field: str = "availability SM version"
) -> int:
    """Return one SM version representable by the generated C++ descriptors."""

    if type(value) is not int or not 0 <= value <= UINT32_MAX:
        raise ValueError(f"{field} must be a uint32")
    return value


def parse_availability_target(target: object) -> tuple[int, str]:
    """Parse an exact availability target into its architecture and flavor."""

    match = _AVAILABILITY_TARGET.fullmatch(target) if isinstance(target, str) else None
    if match is None:
        raise ValueError("availability target must be an sm_<number>[a|f] spelling")
    return validate_availability_sm_version(
        int(match.group(1)), field="availability target architecture"
    ), {
        "": "Generic",
        "a": "ArchitectureSpecific",
        "f": "FamilySpecific",
    }[match.group(2)]


def validate_availability_family(family: object) -> str:
    """Return one YAML minimum family-specific feature target spelling."""

    if not isinstance(family, str) or _AVAILABILITY_FAMILY.fullmatch(family) is None:
        raise ValueError("availability family must be an sm_<number>f spelling")
    return family


def normalize_availability(raw: object) -> dict[str, Any]:
    """Validate the legacy availability form or its bounded DNF replacement."""

    if not isinstance(raw, dict):
        raise TypeError("availability must be an object")
    if not raw:
        return {}
    if "any_of" not in raw:
        allowed = {"ptx", "sm", "family", "deprecated", "removed", "notes"}
        if set(raw) - allowed or not set(raw) & {"ptx", "sm", "family"}:
            raise ValueError("availability must contain a legacy requirement or any_of")
        if "sm" in raw:
            validate_availability_sm_version(raw["sm"])
        if "family" in raw:
            validate_availability_family(raw["family"])
        return dict(raw)
    if set(raw) != {"any_of"}:
        raise ValueError("any_of availability cannot mix with legacy fields")
    clauses = raw["any_of"]
    if not isinstance(clauses, list) or not 1 <= len(clauses) <= 4:
        raise ValueError("availability any_of must contain one to four clauses")
    normalized: list[dict[str, Any]] = []
    for clause in clauses:
        if not isinstance(clause, dict):
            raise TypeError("availability any_of clauses must be objects")
        if set(clause) - {"ptx", "sm", "target", "capabilities"} or not clause:
            raise ValueError("availability any_of clause has invalid fields")
        if "sm" in clause:
            validate_availability_sm_version(clause["sm"])
        if "target" in clause:
            parse_availability_target(clause["target"])
        capabilities = clause.get("capabilities")
        if capabilities is not None:
            if (not isinstance(capabilities, list) or not 1 <= len(capabilities) <= 4
                    or len(capabilities) != len(set(capabilities))
                    or not all(isinstance(item, str) and item for item in capabilities)):
                raise ValueError("availability capabilities must be one to four unique names")
        normalized.append(dict(clause))
    return {"any_of": normalized}


def normalize_operand(raw: dict[str, Any]) -> OperandSpec:
    """Normalize one operand specification."""

    vector_arities: tuple[int, ...] = ()
    vector_arity_expression: OperandVectorArityExpression | None = None
    vector_type_policy = OperandVectorTypePolicy.AGGREGATE
    vector_allow_sink = False
    type_tag = raw.get("type_tag")
    if raw["kind"] in {"descriptor", "typed_token"}:
        if (not isinstance(type_tag, str) or
                re.fullmatch(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*", type_tag) is None):
            raise ValueError(f"{raw['kind']} operand requires a lower-snake type_tag")
    elif type_tag is not None:
        raise ValueError("type_tag is only valid for descriptor or typed_token operands")

    minimum_elements: int | None = None
    maximum_elements: int | None = None
    element_kinds: tuple[str, ...] = ()
    if raw["kind"] in {"tensor_coordinate", "matrix_fragment"}:
        cardinality = raw.get("cardinality")
        if not isinstance(cardinality, dict):
            raise ValueError(f"{raw['kind']} operand requires cardinality")
        minimum_elements = cardinality.get("min")
        maximum_elements = cardinality.get("max")
        ceiling = 5 if raw["kind"] == "tensor_coordinate" else 64
        if (type(minimum_elements) is not int or type(maximum_elements) is not int or
                minimum_elements < 1 or maximum_elements < minimum_elements or
                maximum_elements > ceiling):
            raise ValueError(
                f"{raw['kind']} cardinality must be within 1..{ceiling} with min <= max"
            )
        raw_element_kinds = raw.get("element_kinds")
        if not isinstance(raw_element_kinds, list):
            raise ValueError(f"{raw['kind']} operand requires element_kinds")
        element_kinds = tuple(raw_element_kinds)
        expected_element_kinds = (
            ("reg", "imm") if raw["kind"] == "tensor_coordinate" else ("reg",)
        )
        if set(element_kinds) != set(expected_element_kinds) or len(element_kinds) != len(expected_element_kinds):
            raise ValueError(
                f"{raw['kind']} element_kinds must be {expected_element_kinds!r}"
            )
    elif raw.get("cardinality") is not None or raw.get("element_kinds") is not None:
        raise ValueError("cardinality and element_kinds are only valid for brace-pack primitives")
    if raw["kind"] in {"reg_vector", "vector_reg", "vector_sreg"}:
        vector = raw.get("vector")
        if not isinstance(vector, dict) or "arity" not in vector:
            raise ValueError(f"{raw['kind']} operand must declare vector.arity")
        raw_arities = vector["arity"]
        if isinstance(raw_arities, dict):
            vector_arity_expression = _normalize_operand_vector_arity_expression(
                raw_arities
            )
        elif isinstance(raw_arities, int):
            raw_arities = [raw_arities]
            vector_arities = tuple(raw_arities)
        elif isinstance(raw_arities, list):
            vector_arities = tuple(raw_arities)
        else:
            raise TypeError("vector.arity must be an integer, list, or expression")
        if vector_arities and any(arity > 8 for arity in vector_arities):
            raise ValueError("resolved vector operands support at most eight elements")
        try:
            vector_type_policy = OperandVectorTypePolicy(
                vector.get("type_policy", "aggregate")
            )
        except ValueError as error:
            raise ValueError(
                f"operand {raw['name']!r}: unsupported vector.type_policy "
                f"{vector.get('type_policy')!r}"
            ) from error
        vector_allow_sink = vector.get("allow_sink", False)
        if not isinstance(vector_allow_sink, bool):
            raise TypeError(
                f"{raw['kind']} vector.allow_sink must be a boolean when supplied."
            )

    type_expression = _normalize_operand_type_expression(raw.get("type"))
    try:
        register_width_policy = OperandRegisterWidthPolicy(
            raw.get("register_width", "same_width")
        )
    except ValueError as error:
        raise ValueError(
            f"operand {raw['name']!r}: unsupported register_width "
            f"{raw.get('register_width')!r}"
        ) from error
    if register_width_policy is OperandRegisterWidthPolicy.EQUAL_OR_WIDER:
        if raw["kind"] not in {"reg", "reg_vector"}:
            raise ValueError(
                f"operand {raw['name']!r}: equal_or_wider register_width is "
                "only valid for kind 'reg' or 'reg_vector'"
            )
        if type_expression is None:
            raise ValueError(
                f"operand {raw['name']!r}: equal_or_wider register_width "
                "requires a type expression"
            )

    state_space_values, state_space_expression = _normalize_operand_state_space(
        raw.get("state_space")
    )
    parameter_constraint = _normalize_operand_parameter_constraint(
        raw.get("parameter")
    )
    has_address_constraint = (
        bool(state_space_values)
        or state_space_expression is not None
        or parameter_constraint is not None
    )
    if has_address_constraint and raw["kind"] != "addr":
        raise ValueError(
            f"operand {raw['name']!r}: address constraints are only valid for "
            "kind 'addr'"
        )
    if parameter_constraint is not None and state_space_expression is None:
        raise ValueError(
            f"operand {raw['name']!r}: parameter constraint requires a "
            "state_space modifier expression"
        )
    return OperandSpec(
        name=raw["name"],
        kind=raw["kind"],
        role=raw.get("role"),
        access=raw.get("access"),
        type_expression=type_expression,
        register_width_policy=register_width_policy,
        state_space_values=state_space_values,
        state_space_expression=state_space_expression,
        parameter_constraint=parameter_constraint,
        vector_arities=vector_arities,
        vector_arity_expression=vector_arity_expression,
        vector_type_policy=vector_type_policy,
        vector_allow_sink=vector_allow_sink,
        type_tag=type_tag,
        minimum_elements=minimum_elements,
        maximum_elements=maximum_elements,
        element_kinds=element_kinds,
    )


def _normalize_operand_type_expression(
    raw_type: object,
) -> OperandTypeExpression | None:
    """Parse YAML ``type`` into a typed source-model expression."""

    if raw_type is None:
        return None
    if isinstance(raw_type, str):
        return OperandTypeExpression(
            kind=OperandTypeExpressionKind.FIXED_SCALAR,
            scalar_type=raw_type,
        )
    if not isinstance(raw_type, dict):
        raise TypeError("operand type must be a scalar type or type expression")

    expression = raw_type.get("expr")
    if not isinstance(expression, str):
        raise TypeError("type expression must be a string")
    match = _MODIFIER_TYPE_EXPR.fullmatch(expression)
    if match is not None:
        return OperandTypeExpression(
            kind=OperandTypeExpressionKind.MODIFIER,
            modifier_name=match.group(1),
        )

    unsupported = next(
        (
            function
            for function in _UNSUPPORTED_TYPE_EXPR_FUNCTIONS
            if expression.startswith(f"{function}(")
        ),
        None,
    )
    if unsupported is not None:
        raise ValueError(
            f"type expression function {unsupported!r} is not supported yet"
        )
    raise ValueError("unsupported type expression; use modifier(<modifier_name>)")


def _normalize_operand_state_space(
    raw_state_space: object,
) -> tuple[tuple[OperandStateSpaceValue, ...], OperandStateSpaceExpression | None]:
    """Parse a static state-space allowlist or one dynamic modifier expression."""

    if raw_state_space is None:
        return (), None
    if isinstance(raw_state_space, str):
        return _normalize_operand_state_space_values([raw_state_space]), None
    if isinstance(raw_state_space, list):
        if not raw_state_space:
            raise ValueError("operand state_space list must not be empty")
        return _normalize_operand_state_space_values(raw_state_space), None
    if not isinstance(raw_state_space, dict):
        raise TypeError("operand state_space must be a value or expression")
    if set(raw_state_space) - {"expr", "doc"} or "expr" not in raw_state_space:
        raise ValueError(
            "operand state_space object must be a modifier expression"
        )
    expression = raw_state_space.get("expr")
    if not isinstance(expression, str):
        raise TypeError("state-space expression must be a string")
    match = _MODIFIER_TYPE_EXPR.fullmatch(expression)
    if match is None:
        raise ValueError(
            "unsupported state-space expression; use modifier(<modifier_name>)"
        )
    return (), OperandStateSpaceExpression(modifier_name=match.group(1))


def _normalize_operand_vector_arity_expression(
    raw_arity: object,
) -> OperandVectorArityExpression:
    """Parse ``vector.arity`` when it is supplied by a modifier expression."""

    if not isinstance(raw_arity, dict):
        raise TypeError("vector arity expression must be an object")
    if set(raw_arity) - {"expr", "doc"} or "expr" not in raw_arity:
        raise ValueError("vector arity object must be a modifier expression")
    expression = raw_arity.get("expr")
    if not isinstance(expression, str):
        raise TypeError("vector arity expression must be a string")
    match = _MODIFIER_TYPE_EXPR.fullmatch(expression)
    if match is None:
        raise ValueError(
            "unsupported vector arity expression; use modifier(<modifier_name>)"
        )
    return OperandVectorArityExpression(modifier_name=match.group(1))


def _normalize_operand_state_space_values(
    raw_values: list[object],
) -> tuple[OperandStateSpaceValue, ...]:
    """Normalize static state-space entries and reject semantic duplicates."""

    values: list[OperandStateSpaceValue] = []
    seen: set[str] = set()
    for raw_value in raw_values:
        if isinstance(raw_value, str):
            value = raw_value
            availability: dict[str, Any] = {}
        elif isinstance(raw_value, dict):
            if set(raw_value) != {"value", "availability"}:
                raise ValueError(
                    "operand state_space list entries must contain value and "
                    "availability"
                )
            value = raw_value["value"]
            raw_availability = raw_value["availability"]
            if not isinstance(raw_availability, dict):
                raise TypeError("operand state_space availability must be an object")
            availability = normalize_availability(raw_availability)
        else:
            raise TypeError(
                "operand state_space list entries must be strings or value objects"
            )
        if not isinstance(value, str) or value not in _STATE_SPACES:
            raise ValueError(f"unknown operand state space {value!r}")
        if value in seen:
            raise ValueError(f"duplicate operand state space {value!r}")
        seen.add(value)
        values.append(
            OperandStateSpaceValue(value=value, availability=availability)
        )
    return tuple(values)


def _normalize_operand_parameter_constraint(
    raw_parameter: object,
) -> OperandParameterConstraint | None:
    """Normalize the narrow direction rule for explicit .param addresses."""

    if raw_parameter is None:
        return None
    if not isinstance(raw_parameter, dict):
        raise TypeError("operand parameter constraint must be an object")
    if set(raw_parameter) != {"direction", "function_availability"}:
        raise ValueError(
            "operand parameter constraint must contain direction and "
            "function_availability"
        )
    direction = raw_parameter["direction"]
    if direction not in {"input", "return"}:
        raise ValueError(f"unsupported parameter direction {direction!r}")
    availability = raw_parameter["function_availability"]
    if not isinstance(availability, dict):
        raise TypeError("parameter function_availability must be an object")
    return OperandParameterConstraint(
        direction=direction,
        function_availability=normalize_availability(availability),
    )


def normalize_modifier(
    raw: dict[str, Any], reusable_value_sets: dict[str, list[str]]
) -> ModifierSpec:
    """Normalize one modifier and expand its reusable value-set references."""

    raw_values: object = raw.get("values", [])
    if not isinstance(raw_values, list):
        raise TypeError("modifier values must be a list")

    values = _normalize_modifier_values(raw_values, reusable_value_sets)
    if raw["kind"] == "cache" and any(
        value.value == "unspecified" for value in values
    ):
        raise ValueError(
            f"modifier {raw['name']!r}: cache sentinel 'unspecified' is not a "
            "syntax value"
        )
    _validate_modifier_default(raw, values)

    return ModifierSpec(
        name=raw["name"],
        kind=raw["kind"],
        presence=raw["presence"],
        domain=raw.get("domain"),
        values=values,
        value=raw.get("value"),
        token=raw.get("token"),
        default=raw.get("default"),
    )


def _validate_modifier_default(
    raw: dict[str, Any], values: tuple[ModifierValueSpec, ...]
) -> None:
    """Validate the semantic value used when an optional modifier is omitted."""

    presence = raw["presence"]
    has_default = "default" in raw
    if presence != "optional":
        if has_default:
            raise ValueError(
                f"modifier {raw['name']!r}: default is only valid for optional "
                "modifiers"
            )
        return
    if not has_default:
        raise ValueError(
            f"optional modifier {raw['name']!r} must define default"
        )

    default = raw["default"]
    kind = raw["kind"]
    if kind == "flag":
        if type(default) is not bool:
            raise ValueError(
                f"optional flag modifier {raw['name']!r} must have a boolean "
                "default"
            )
        return
    if kind == "type":
        if not isinstance(default, str):
            raise ValueError(
                f"optional type modifier {raw['name']!r} must have a string "
                "default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional type modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return
    if kind == "rounding":
        if not isinstance(default, str):
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} must have a "
                "string default"
            )
        allowed_values = {value.value for value in values}
        if default not in allowed_values:
            raise ValueError(
                f"optional rounding modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return
    if kind == "cache":
        if default != "unspecified":
            raise ValueError(
                f"optional cache modifier {raw['name']!r} must use semantic "
                "default 'unspecified'"
            )
        return
    if kind in {"semantics", "scope"}:
        if not isinstance(default, str):
            raise ValueError(
                f"optional {kind} modifier {raw['name']!r} must have a string "
                "default"
            )
        allowed_values = {value.value for value in values}
        # Omission sentinels intentionally are not spellable modifier values.
        sentinel = "omitted" if kind == "semantics" else "none"
        if default != sentinel and default not in allowed_values:
            raise ValueError(
                f"optional {kind} modifier {raw['name']!r} has default "
                f"{default!r} outside its allowed values"
            )
        return


def _normalize_modifier_values(
    raw_values: list[Any], reusable_value_sets: dict[str, list[str]]
) -> tuple[ModifierValueSpec, ...]:
    """Expand value-set references while preserving per-value availability."""

    values: list[ModifierValueSpec] = []
    seen: set[str | bool | int] = set()
    for raw_value in raw_values:
        if isinstance(raw_value, dict):
            raw_semantic_value = raw_value["value"]
            token = raw_value.get("token")
            availability = normalize_availability(raw_value.get("availability", {}))
        else:
            raw_semantic_value = raw_value
            token = None
            availability = {}

        if isinstance(raw_semantic_value, str) and raw_semantic_value.startswith("$"):
            if token is not None:
                raise ValueError(
                    "a modifier value-set reference cannot define one token "
                    "override for multiple expanded values"
                )
            expanded_values: tuple[str | bool | int, ...] = expand_value_refs(
                [raw_semantic_value], reusable_value_sets
            )
        else:
            expanded_values = (raw_semantic_value,)
        for value in expanded_values:
            if value in seen:
                raise ValueError(f"duplicate modifier value {value!r}")
            seen.add(value)
            values.append(
                ModifierValueSpec(
                    value=value,
                    token=token,
                    availability=availability,
                )
            )
    return tuple(values)


def normalize_operand_layouts(
    raw_variant: dict[str, Any],
    default_operands: Any,
    operand_patterns: dict[str, Any],
) -> tuple[OperandLayoutSpec, ...]:
    """Normalize explicit layouts, or lift the legacy operand list to ``default``."""

    raw_layouts = raw_variant.get("operand_layouts")
    if raw_layouts is None:
        operands = _resolve_operands(
            raw_variant.get("operands", default_operands), operand_patterns
        )
        return (OperandLayoutSpec(name="default", operands=operands),)

    if "operands" in raw_variant:
        raise ValueError(
            f"variant {raw_variant['name']!r} cannot define both operands and "
            "operand_layouts"
        )

    layouts: list[OperandLayoutSpec] = []
    names: set[str] = set()
    for raw_layout in raw_layouts:
        name = raw_layout["name"]
        if name in names:
            raise ValueError(
                f"variant {raw_variant['name']!r} has duplicate operand layout "
                f"name {name!r}"
            )
        names.add(name)
        try:
            kind = OperandLayoutKind(raw_layout.get("kind", "flat"))
        except ValueError as error:
            raise ValueError(
                f"operand layout {name!r}: unsupported kind "
                f"{raw_layout.get('kind')!r}"
            ) from error
        operands = _resolve_operands(raw_layout["operands"], operand_patterns)
        call_operand_kinds = {
            "direct_call_target",
            "indirect_call_target",
            "indirect_call_metadata",
            "call_return_param",
            "call_arguments",
        }
        if kind not in {OperandLayoutKind.CALL, OperandLayoutKind.INDIRECT_CALL} and any(
            operand.kind in call_operand_kinds for operand in operands
        ):
            raise ValueError(
                f"operand layout {name!r}: call operands require kind 'call' "
                "or 'indirect_call'"
            )
        if kind is OperandLayoutKind.CALL:
            call_shapes = {
                ("direct_call_target",),
                ("direct_call_target", "call_arguments"),
                ("call_return_param", "direct_call_target", "call_arguments"),
            }
            if tuple(operand.kind for operand in operands) not in call_shapes:
                raise ValueError(
                    f"call operand layout {name!r} must be direct target, "
                    "direct target plus input group, or return group plus "
                    "direct target plus input group"
                )
        if kind is OperandLayoutKind.INDIRECT_CALL:
            call_shapes = {
                ("indirect_call_target", "indirect_call_metadata"),
                (
                    "indirect_call_target",
                    "call_arguments",
                    "indirect_call_metadata",
                ),
                (
                    "call_return_param",
                    "indirect_call_target",
                    "call_arguments",
                    "indirect_call_metadata",
                ),
            }
            if tuple(operand.kind for operand in operands) not in call_shapes:
                raise ValueError(
                    f"indirect call operand layout {name!r} must be target plus "
                    "metadata, optionally with input and return groups"
                )
        layouts.append(
            OperandLayoutSpec(
                name=name,
                operands=operands,
                kind=kind,
                availability=normalize_availability(raw_layout.get("availability", {})),
            )
        )
    if not layouts:
        raise ValueError(f"variant {raw_variant['name']!r} has no operand layouts")
    normalized_layouts = tuple(layouts)
    _validate_flat_operand_layout_ordering(raw_variant["name"], normalized_layouts)
    return normalized_layouts


def _modern_pack_interval(operand: OperandSpec) -> tuple[int, int] | None:
    if operand.minimum_elements is None:
        return None
    return operand.minimum_elements, operand.maximum_elements


def _flat_slot_overlap(left: OperandSpec, right: OperandSpec) -> bool:
    if not (OPERAND_SYNTAX_SHAPES[left.kind] & OPERAND_SYNTAX_SHAPES[right.kind]):
        return False
    left_interval = _modern_pack_interval(left)
    right_interval = _modern_pack_interval(right)
    if left_interval is None and right_interval is None:
        return True
    if left_interval is not None and right_interval is not None:
        if max(left_interval[0], right_interval[0]) > min(left_interval[1], right_interval[1]):
            return False
        return bool(set(left.element_kinds) & set(right.element_kinds))
    return True


def _flat_slot_is_subset(candidate: OperandSpec, other: OperandSpec) -> bool:
    candidate_shapes = OPERAND_SYNTAX_SHAPES[candidate.kind]
    other_shapes = OPERAND_SYNTAX_SHAPES[other.kind]
    if (candidate_shapes & other_shapes) != candidate_shapes:
        return False
    candidate_interval = _modern_pack_interval(candidate)
    other_interval = _modern_pack_interval(other)
    if candidate_interval is None:
        return other_interval is None
    if other_interval is None:
        return True
    return (
        candidate_interval[0] >= other_interval[0]
        and candidate_interval[1] <= other_interval[1]
        and set(candidate.element_kinds) <= set(other.element_kinds)
    )


def _validate_flat_operand_layout_ordering(
    variant_name: str, layouts: tuple[OperandLayoutSpec, ...]
) -> None:
    flat_layouts = tuple(layout for layout in layouts if layout.kind is OperandLayoutKind.FLAT)
    for left_index, left in enumerate(flat_layouts):
        for right in flat_layouts[left_index + 1:]:
            if len(left.operands) != len(right.operands):
                continue
            if not all(
                _flat_slot_overlap(left_operand, right_operand)
                for left_operand, right_operand in zip(left.operands, right.operands, strict=True)
            ):
                continue
            left_subset = all(
                _flat_slot_is_subset(left_operand, right_operand)
                for left_operand, right_operand in zip(left.operands, right.operands, strict=True)
            )
            right_subset = all(
                _flat_slot_is_subset(right_operand, left_operand)
                for left_operand, right_operand in zip(left.operands, right.operands, strict=True)
            )
            if left_subset == right_subset:
                raise ValueError(
                    f"variant {variant_name!r}: flat operand layouts {left.name!r} and "
                    f"{right.name!r} accept overlapping syntax without a unique "
                    "most-specific layout"
                )


def _resolve_operands(
    raw_operands: Any, operand_patterns: dict[str, Any]
) -> tuple[OperandSpec, ...]:
    if raw_operands is None:
        raise ValueError("variant has neither operands nor inherited instruction operands")
    if isinstance(raw_operands, str):
        if not raw_operands.startswith("$"):
            raise ValueError(
                "operand-pattern references must use the '$name' form; "
                "use an explicit operand list for inline operands"
            )
        pattern_name = raw_operands[1:]
        try:
            raw_operands = operand_patterns[pattern_name]
        except KeyError as error:
            raise ValueError(f"unknown operand pattern: {pattern_name}") from error
    if not isinstance(raw_operands, list):
        raise TypeError("operands must be an explicit list or a '$name' reference")
    return tuple(normalize_operand(operand) for operand in raw_operands)


def _validate_modifier_type_expressions(
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> None:
    """Require ``modifier(name)`` expressions to name an active type modifier."""

    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for layout in layouts:
        for operand in layout.operands:
            expression = operand.type_expression
            if expression is None:
                continue
            if expression.kind is not OperandTypeExpressionKind.MODIFIER:
                continue
            assert expression.modifier_name is not None
            modifier_name = expression.modifier_name
            modifier = modifiers_by_name.get(modifier_name)
            if modifier is None:
                raise ValueError(
                    f"operand {operand.name!r}: type expression references unknown "
                    f"modifier {modifier_name!r}"
                )
            if modifier.kind != "type" or modifier.presence == "absent":
                raise ValueError(
                    f"operand {operand.name!r}: modifier {modifier_name!r} must be "
                    "an active type modifier"
                )


def _validate_modifier_state_space_expressions(
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> None:
    """Require state-space expressions to name an active matching modifier."""

    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for layout in layouts:
        for operand in layout.operands:
            expression = operand.state_space_expression
            if operand.parameter_constraint is not None and expression is None:
                raise ValueError(
                    f"operand {operand.name!r}: parameter constraint requires a "
                    "state-space modifier expression"
                )
            if expression is None:
                continue
            modifier = modifiers_by_name.get(expression.modifier_name)
            if modifier is None:
                raise ValueError(
                    f"operand {operand.name!r}: state-space expression references "
                    f"unknown modifier {expression.modifier_name!r}"
                )
            if modifier.kind != "state_space" or modifier.presence == "absent":
                raise ValueError(
                    f"operand {operand.name!r}: modifier "
                    f"{expression.modifier_name!r} must be an active "
                    "state-space modifier"
                )
            if operand.parameter_constraint is not None:
                allows_parameter = modifier.value == "param" or any(
                    value.value == "param" for value in modifier.values
                )
                if not allows_parameter:
                    raise ValueError(
                        f"operand {operand.name!r}: parameter constraint requires "
                        f"modifier {expression.modifier_name!r} to allow .param"
                    )


def _normalize_operand_type_compatibilities(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> tuple[OperandTypeCompatibilitySpec, ...]:
    """Normalize value-dependent type rules and validate their operand names."""

    operand_names = {
        operand.name for layout in layouts for operand in layout.operands
    }
    result: list[OperandTypeCompatibilitySpec] = []
    seen: set[tuple[str, str, str, int]] = set()
    for raw in raw_variant.get("operand_type_compatibilities", ()):
        operand = raw["operand"]
        if operand not in operand_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: operand type compatibility "
                f"references unknown operand {operand!r}"
            )
        for value in raw["values"]:
            key = (operand, raw["value_kind"], value, raw["instruction_width"])
            if key in seen:
                raise ValueError(
                    f"variant {raw_variant['name']!r}: duplicate operand type "
                    f"compatibility {key!r}"
                )
            seen.add(key)
        result.append(
            OperandTypeCompatibilitySpec(
                operand=operand,
                value_kind=raw["value_kind"],
                values=tuple(raw["values"]),
                instruction_width=raw["instruction_width"],
                effective_type=raw["effective_type"],
                availability=normalize_availability(raw["availability"]),
            )
        )
    return tuple(result)


def _normalize_memory_consistency_constraint(
    raw_variant: dict[str, Any], modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...]
) -> MemoryConsistencyConstraint | None:
    """Lower the one typed ld/st cross-modifier constraint, if present."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "memory_consistency"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one memory_consistency "
            "constraint is supported"
        )
    raw = matches[0]
    required = {
        "semantics_modifier", "scope_modifier", "cache_modifier",
        "address_operand",
    }
    missing = required - raw.keys()
    if missing:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency constraint "
            f"is missing {sorted(missing)}"
        )
    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    modifier_names = set(modifiers_by_name)
    for key in required - {"address_operand"}:
        value = raw[key]
        if value not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"references inactive modifier {value!r}"
            )
    operand_names = {operand.name for layout in layouts for operand in layout.operands}
    if raw["address_operand"] not in operand_names:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency address "
            f"references unknown operand {raw['address_operand']!r}"
        )
    if any(
        operand.name == raw["address_operand"] and operand.kind != "addr"
        for layout in layouts for operand in layout.operands
    ):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency address "
            "operand must have kind 'addr'"
        )
    expected_kinds = {
        "semantics_modifier": "semantics",
        "scope_modifier": "scope",
        "cache_modifier": "cache",
    }
    for key, expected_kind in expected_kinds.items():
        if modifiers_by_name[raw[key]].kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"must name a {expected_kind!r} modifier"
            )
    mmio_modifier = raw.get("mmio_modifier")
    if mmio_modifier is not None:
        if mmio_modifier not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency "
                f"mmio_modifier references inactive modifier {mmio_modifier!r}"
            )
        if modifiers_by_name[mmio_modifier].kind != "flag":
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency "
                "mmio_modifier must name a 'flag' modifier"
            )
    for key in ("state_space_modifier",):
        value = raw.get(key)
        if value is not None and value not in modifier_names:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_consistency {key} "
                f"references inactive modifier {value!r}"
            )
    if raw.get("state_space_modifier") is not None and (
        modifiers_by_name[raw["state_space_modifier"]].kind != "state_space"
    ):
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_consistency "
            "state_space_modifier must name a state_space modifier"
        )
    return MemoryConsistencyConstraint(
        semantics_modifier=raw["semantics_modifier"],
        scope_modifier=raw["scope_modifier"],
        cache_modifier=raw["cache_modifier"],
        address_operand=raw["address_operand"],
        mmio_modifier=mmio_modifier,
        state_space_modifier=raw.get("state_space_modifier"),
    )


def _normalize_address_alignment_constraint(
    raw_variant: dict[str, Any], modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...]
) -> AddressAlignmentConstraint | None:
    """Lower one data-driven natural-address-alignment rule."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "address_alignment"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one address_alignment "
            "constraint is supported"
        )
    raw = matches[0]
    has_singular = "address_operand" in raw
    has_plural = "address_operands" in raw
    if has_singular == has_plural:
        raise ValueError(
            f"variant {raw_variant['name']!r}: address_alignment constraint "
            "requires exactly one of address_operand or address_operands"
        )
    address_operands = ((raw["address_operand"],) if has_singular
                        else tuple(raw["address_operands"]))
    if (not address_operands or any(not isinstance(operand, str)
                                   for operand in address_operands) or
            len(set(address_operands)) != len(address_operands)):
        raise ValueError(
            f"variant {raw_variant['name']!r}: address_alignment address "
            "operands must be unique non-empty identifiers"
        )
    source_count = sum(key in raw for key in
                       ("type_modifier", "immediate_operand", "alignment"))
    if source_count != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: address_alignment requires "
            "exactly one of type_modifier, immediate_operand, or alignment"
        )
    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for key, expected_kind in (("type_modifier", "type"),
                               ("vector_modifier", "vector")):
        value = raw.get(key)
        if value is None:
            continue
        if value not in modifiers_by_name:
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment {key} "
                f"references inactive modifier {value!r}"
            )
        if modifiers_by_name[value].kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment {key} "
                f"must name a {expected_kind!r} modifier"
            )
    matching_operands = [
        operand for layout in layouts for operand in layout.operands
        if operand.name in address_operands
    ]
    if (len(matching_operands) != len(address_operands) or
            any(operand.kind != "addr" for operand in matching_operands)):
        raise ValueError(
            f"variant {raw_variant['name']!r}: address_alignment address "
            "operand must name an active kind 'addr' operand"
        )
    immediate_operand = raw.get("immediate_operand")
    if immediate_operand is not None:
        matching_immediates = [
            operand for layout in layouts for operand in layout.operands
            if operand.name == immediate_operand
        ]
        if (not matching_immediates or
                any(operand.kind != "imm" for operand in matching_immediates)):
            raise ValueError(
                f"variant {raw_variant['name']!r}: address_alignment "
                "immediate_operand must name an active kind 'imm' operand"
            )
    alignment = raw.get("alignment")
    if alignment is not None and (type(alignment) is not int or alignment <= 0):
        raise ValueError(
            f"variant {raw_variant['name']!r}: address_alignment alignment "
            "must be a positive integer"
        )
    return AddressAlignmentConstraint(
        address_operands=address_operands,
        type_modifier=raw.get("type_modifier"),
        vector_modifier=raw.get("vector_modifier"),
        immediate_operand=immediate_operand,
        alignment=alignment,
    )


def _normalize_memory_vector_constraint(
    raw_variant: dict[str, Any], modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...]
) -> MemoryVectorConstraint | None:
    """Lower the typed PTX 8.8 256-bit ld/st vector rule, if present."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "memory_vector"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one memory_vector "
            "constraint is supported"
        )
    raw = matches[0]
    required = {"type_modifier", "vector_operand", "address_operand", "availability"}
    missing = required - raw.keys()
    if missing:
        raise ValueError(
            f"variant {raw_variant['name']!r}: memory_vector constraint "
            f"is missing {sorted(missing)}"
        )
    modifiers_by_name = {modifier.name: modifier for modifier in modifiers}
    for key, expected_kind in (("type_modifier", "type"),):
        value = raw[key]
        if value not in modifiers_by_name:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"references inactive modifier {value!r}"
            )
        if modifiers_by_name[value].kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"must name a {expected_kind!r} modifier"
            )
    operand_by_name = {
        operand.name: operand for layout in layouts for operand in layout.operands
    }
    for key, expected_kind in (("vector_operand", "reg_vector"), ("address_operand", "addr")):
        value = raw[key]
        operand = operand_by_name.get(value)
        if operand is None:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"references unknown operand {value!r}"
            )
        if operand.kind != expected_kind:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector {key} "
                f"must name a {expected_kind!r} operand"
            )
    state_space_modifier = raw.get("state_space_modifier")
    if state_space_modifier is not None:
        modifier = modifiers_by_name.get(state_space_modifier)
        if modifier is None:
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector state_space_modifier "
                f"references inactive modifier {state_space_modifier!r}"
            )
        if modifier.kind != "state_space":
            raise ValueError(
                f"variant {raw_variant['name']!r}: memory_vector state_space_modifier "
                "must name a 'state_space' modifier"
            )
    availability = raw["availability"]
    if not isinstance(availability, dict):
        raise TypeError("memory_vector availability must be an object")
    return MemoryVectorConstraint(
        type_modifier=raw["type_modifier"],
        vector_operand=raw["vector_operand"],
        address_operand=raw["address_operand"],
        availability=normalize_availability(availability),
        state_space_modifier=state_space_modifier,
    )


def _normalize_immediate_value_constraint(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> ImmediateValueConstraint | None:
    """Lower one exact integer allowlist for an immediate operand."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_value"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one immediate_value "
            "constraint is supported"
        )
    raw = matches[0]
    if set(raw) != {"kind", "operand", "values"}:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value constraint "
            "requires only operand and values"
        )
    operand_name = raw["operand"]
    values = raw["values"]
    matching = [
        operand for layout in layouts for operand in layout.operands
        if operand.name == operand_name
    ]
    if not matching or any(operand.kind != "imm" for operand in matching):
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value operand must "
            "name an active kind 'imm' operand"
        )
    if (not isinstance(values, list) or not values or
            any(type(value) is not int or value < 0 for value in values) or
            len(set(values)) != len(values)):
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_value values must "
            "be unique non-negative integers"
        )
    return ImmediateValueConstraint(operand=operand_name, values=tuple(values))


def _normalize_immediate_range_constraints(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> tuple[ImmediateRangeConstraint, ...]:
    """Lower inclusive non-negative ranges, one for each immediate operand."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_range"
    ]
    if not matches:
        return ()
    ranges = []
    operands = set()
    for raw in matches:
        if set(raw) not in ({"kind", "operand", "minimum"},
                            {"kind", "operand", "minimum", "maximum"}):
            raise ValueError(
                f"variant {raw_variant['name']!r}: immediate_range constraint "
                "requires operand, minimum, and optional maximum"
            )
        operand_name, minimum, maximum = raw["operand"], raw["minimum"], raw.get("maximum")
        if operand_name in operands:
            raise ValueError(
                f"variant {raw_variant['name']!r}: duplicate immediate_range "
                f"operand {operand_name!r}"
            )
        operands.add(operand_name)
        matching = [
            operand for layout in layouts for operand in layout.operands
            if operand.name == operand_name
        ]
        if not matching or any(operand.kind != "imm" for operand in matching):
            raise ValueError(
                f"variant {raw_variant['name']!r}: immediate_range operand must "
                "name an active kind 'imm' operand"
            )
        if (type(minimum) is not int or minimum < 0 or
                (maximum is not None and
                 (type(maximum) is not int or maximum < minimum))):
            raise ValueError(
                f"variant {raw_variant['name']!r}: immediate_range bounds must "
                "be non-negative integers with optional maximum >= minimum"
            )
        ranges.append(
            ImmediateRangeConstraint(
                operand=operand_name, minimum=minimum, maximum=maximum
            )
        )
    return tuple(ranges)


def _normalize_immediate_multiple_of_constraint(
    raw_variant: dict[str, Any], layouts: tuple[OperandLayoutSpec, ...]
) -> ImmediateMultipleOfConstraint | None:
    """Lower one positive-divisor constraint for an immediate operand."""

    matches = [
        item for item in raw_variant.get("constraints", ())
        if item.get("kind") == "immediate_multiple_of"
    ]
    if not matches:
        return None
    if len(matches) != 1:
        raise ValueError(
            f"variant {raw_variant['name']!r}: at most one immediate_multiple_of "
            "constraint is supported"
        )
    raw = matches[0]
    if set(raw) != {"kind", "operand", "divisor"}:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_multiple_of constraint "
            "requires only operand and divisor"
        )
    operand_name, divisor = raw["operand"], raw["divisor"]
    matching = [
        operand for layout in layouts for operand in layout.operands
        if operand.name == operand_name
    ]
    if not matching or any(operand.kind != "imm" for operand in matching):
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_multiple_of operand must "
            "name an active kind 'imm' operand"
        )
    if type(divisor) is not int or divisor <= 0:
        raise ValueError(
            f"variant {raw_variant['name']!r}: immediate_multiple_of divisor must "
            "be a positive integer"
        )
    return ImmediateMultipleOfConstraint(operand=operand_name, divisor=divisor)


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    """Normalize all instruction definitions in one PTX ISA YAML file."""

    source_category = spec.get("category")
    codegen_category = spec.get("codegen_category")
    if not isinstance(source_category, str):
        raise ValueError("PTX spec file must define top-level category")
    if not isinstance(codegen_category, str):
        raise ValueError("PTX spec file must define top-level codegen_category")

    type_sets = spec.get("type_sets", {})
    value_sets = spec.get("value_sets", {})
    duplicate_set_names = set(type_sets) & set(value_sets)
    if duplicate_set_names:
        raise ValueError(
            "type_sets and value_sets define the same names: "
            f"{sorted(duplicate_set_names)}"
        )
    reusable_value_sets = {**type_sets, **value_sets}
    operand_patterns = spec.get("operand_patterns", {})
    instructions: list[InstructionSpec] = []

    for raw_instruction in spec["instructions"]:
        default_operands = raw_instruction.get("operands")
        variants: list[VariantSpec] = []

        for raw_variant in raw_instruction["variants"]:
            modifiers = tuple(
                normalize_modifier(modifier, reusable_value_sets)
                for modifier in raw_variant.get("modifiers", ())
            )
            operand_layouts = normalize_operand_layouts(
                raw_variant, default_operands, operand_patterns
            )
            _validate_modifier_type_expressions(modifiers, operand_layouts)
            _validate_modifier_state_space_expressions(
                modifiers, operand_layouts
            )
            variants.append(
                VariantSpec(
                    name=raw_variant["name"],
                    availability=normalize_availability(raw_variant["availability"]),
                    modifiers=modifiers,
                    operand_layouts=operand_layouts,
                    rule=raw_variant.get("rule"),
                    operand_type_compatibilities=(
                        _normalize_operand_type_compatibilities(
                            raw_variant, operand_layouts
                        )
                    ),
                    memory_consistency=_normalize_memory_consistency_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    address_alignment=_normalize_address_alignment_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    memory_vector=_normalize_memory_vector_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    immediate_value=_normalize_immediate_value_constraint(
                        raw_variant, operand_layouts
                    ),
                    immediate_ranges=_normalize_immediate_range_constraints(
                        raw_variant, operand_layouts
                    ),
                    immediate_multiple_of=_normalize_immediate_multiple_of_constraint(
                        raw_variant, operand_layouts
                    ),
                )
            )

        instructions.append(
            InstructionSpec(
                opcode=raw_instruction["opcode"],
                variants=tuple(variants),
                syntax_forms=(raw_instruction["syntax"],)
                if "syntax" in raw_instruction
                else (),
                source_categories=(source_category,),
                codegen_category=codegen_category,
            )
        )

    return tuple(instructions)
