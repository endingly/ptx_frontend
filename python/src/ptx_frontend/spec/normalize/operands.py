"""Normalize operand specifications without changing diagnostic order."""

import re
from dataclasses import dataclass
from typing import Any

from ptx_frontend.spec.model import (
    MbarrierStateTokenForm,
    OperandAccess,
    OperandImmediateConversionPolicy,
    OperandKind,
    OperandParameterConstraint,
    OperandRegisterWidthPolicy,
    OperandRole,
    OperandSpec,
    OperandStateSpaceExpression,
    OperandStateSpaceValue,
    OperandTypeExpression,
    OperandVectorArityExpression,
    OperandVectorTypePolicy,
)

from .availability import normalize_availability
from .expressions import (
    _normalize_operand_parameter_constraint,
    _normalize_operand_state_space,
    _normalize_operand_type_expression,
    _normalize_operand_vector_arity_expression,
)


@dataclass(frozen=True)
class _ShflSinkOptions:
    """Normalized scalar and predicate sink flags for a shfl destination."""

    allow_destination: bool
    allow_predicate: bool


@dataclass(frozen=True)
class _MbarrierOptions:
    """Normalized mbarrier token form and sink availability."""

    form: MbarrierStateTokenForm
    sink_availability: dict[str, Any]


@dataclass(frozen=True)
class _BracePackOptions:
    """Normalized cardinality and element kinds for a brace-pack operand."""

    minimum_elements: int | None
    maximum_elements: int | None
    element_kinds: tuple[OperandKind, ...]


@dataclass(frozen=True)
class _VectorOptions:
    """Normalized vector shape, type policy, and sink settings."""

    arities: tuple[int, ...]
    arity_expression: OperandVectorArityExpression | None
    type_policy: OperandVectorTypePolicy
    allow_sink: bool
    sink_payload_bits: int


@dataclass(frozen=True)
class _AddressOptions:
    """Normalized state-space and parameter constraints for an address."""

    state_space_values: tuple[OperandStateSpaceValue, ...]
    state_space_expression: OperandStateSpaceExpression | None
    parameter_constraint: OperandParameterConstraint | None


def normalize_operand(raw: dict[str, Any]) -> OperandSpec:
    """Normalize one operand specification in the established validation order."""

    kind = _parse_operand_kind(raw)
    role = _parse_operand_role(raw)
    access = _parse_operand_access(raw)
    raw = {**raw, "kind": kind, "role": role, "access": access}

    # Preserve this order: inputs with multiple errors must retain their
    # original first diagnostic.
    shfl_sink = _normalize_shfl_sink_options(raw)
    mbarrier = _normalize_mbarrier_options(raw)
    _validate_sink_destination(raw)

    type_tag = _normalize_type_tag(raw)
    pack = _normalize_brace_pack_options(raw)
    vector = _normalize_vector_options(raw)

    type_expression = _normalize_operand_type_expression(raw.get("type"))
    register_width = _normalize_register_width(
        raw,
        type_expression=type_expression,
    )
    immediate_conversion = _normalize_immediate_conversion(raw)
    address = _normalize_address_options(raw)

    return OperandSpec(
        name=raw["name"],
        kind=kind,
        role=role,
        access=access,
        type_expression=type_expression,
        register_width_policy=register_width,
        immediate_conversion_policy=immediate_conversion,
        state_space_values=address.state_space_values,
        state_space_expression=address.state_space_expression,
        parameter_constraint=address.parameter_constraint,
        vector_arities=vector.arities,
        vector_arity_expression=vector.arity_expression,
        vector_type_policy=vector.type_policy,
        vector_allow_sink=vector.allow_sink,
        vector_sink_payload_bits=vector.sink_payload_bits,
        allow_destination_sink=shfl_sink.allow_destination,
        allow_predicate_sink=shfl_sink.allow_predicate,
        mbarrier_state_token_form=mbarrier.form,
        sink_availability=mbarrier.sink_availability,
        type_tag=type_tag,
        minimum_elements=pack.minimum_elements,
        maximum_elements=pack.maximum_elements,
        element_kinds=pack.element_kinds,
    )


def _normalize_shfl_sink_options(raw: dict[str, Any]) -> _ShflSinkOptions:
    """Normalize shfl sink flags, preserving destination-before-predicate checks."""

    allow_destination_sink = raw.get("allow_destination_sink", False)
    if not isinstance(allow_destination_sink, bool):
        raise TypeError("allow_destination_sink must be a boolean when supplied.")
    if "allow_destination_sink" in raw and raw["kind"] is not OperandKind.SHFL_DESTINATION:
        raise ValueError("allow_destination_sink is only valid for kind 'shfl_dest'")
    allow_predicate_sink = raw.get("allow_predicate_sink", False)
    if not isinstance(allow_predicate_sink, bool):
        raise TypeError("allow_predicate_sink must be a boolean when supplied.")
    if "allow_predicate_sink" in raw and raw["kind"] is not OperandKind.SHFL_DESTINATION:
        raise ValueError("allow_predicate_sink is only valid for kind 'shfl_dest'")

    return _ShflSinkOptions(
        allow_destination=allow_destination_sink,
        allow_predicate=allow_predicate_sink,
    )


def _normalize_mbarrier_options(raw: dict[str, Any]) -> _MbarrierOptions:
    """Normalize the token form and availability before checking their use."""

    try:
        mbarrier_state_token_form = MbarrierStateTokenForm(
            raw.get("mbarrier_state_token_form", "register")
        )
    except ValueError as error:
        raise ValueError(
            "mbarrier_state_token_form must be register, " "register_or_sink, or sink"
        ) from error
    sink_availability = normalize_availability(raw.get("sink_availability", {}))
    if raw["kind"] is not OperandKind.MBARRIER_STATE_TOKEN:
        if "mbarrier_state_token_form" in raw or "sink_availability" in raw:
            raise ValueError(
                "mbarrier state-token sink settings are only valid for "
                "kind 'mbarrier_state_token'"
            )
    elif mbarrier_state_token_form is MbarrierStateTokenForm.REGISTER:
        if sink_availability:
            raise ValueError(
                "register-only mbarrier state token cannot have " "sink_availability"
            )
    else:
        if raw.get("role") is not OperandRole.DESTINATION or raw.get("access") is not OperandAccess.WRITE:
            raise ValueError(
                "sink-capable mbarrier state token must be a write destination"
            )
        if not sink_availability:
            raise ValueError(
                "sink-capable mbarrier state token requires sink_availability"
            )

    return _MbarrierOptions(
        form=mbarrier_state_token_form,
        sink_availability=sink_availability,
    )


def _validate_sink_destination(raw: dict[str, Any]) -> None:
    """Require scalar sink-capable operand kinds to be write destinations."""

    if raw["kind"] is OperandKind.REGISTER_OR_SINK and (
        raw.get("role") is not OperandRole.DESTINATION or raw.get("access") is not OperandAccess.WRITE
    ):
        raise ValueError("reg_or_sink must be a write destination")
    if raw["kind"] is OperandKind.PREDICATE_OR_SINK and (
        raw.get("role") is not OperandRole.DESTINATION or raw.get("access") is not OperandAccess.WRITE
    ):
        raise ValueError("pred_or_sink must be a write destination")
    if raw["kind"] is OperandKind.PREDICATE_PAIR_OR_SINK and (
        raw.get("role") is not OperandRole.DESTINATION or raw.get("access") is not OperandAccess.WRITE
    ):
        raise ValueError("pred_pair_or_sink must be a write destination")


def _normalize_type_tag(raw: dict[str, Any]) -> str | None:
    """Normalize the type tag for descriptor and typed-token operands."""

    type_tag = raw.get("type_tag")
    if raw["kind"] in {OperandKind.DESCRIPTOR, OperandKind.TYPED_TOKEN}:
        if (
            not isinstance(type_tag, str)
            or re.fullmatch(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*", type_tag) is None
        ):
            raise ValueError(f"{raw['kind']} operand requires a lower-snake type_tag")
    elif type_tag is not None:
        raise ValueError(
            "type_tag is only valid for descriptor or typed_token operands"
        )

    return type_tag


def _normalize_brace_pack_options(raw: dict[str, Any]) -> _BracePackOptions:
    """Normalize brace-pack cardinality before validating element kinds."""

    minimum_elements: int | None = None
    maximum_elements: int | None = None
    element_kinds: tuple[OperandKind, ...] = ()
    if raw["kind"] in {OperandKind.TENSOR_COORDINATE, OperandKind.MATRIX_FRAGMENT}:
        cardinality = raw.get("cardinality")
        if not isinstance(cardinality, dict):
            raise ValueError(f"{raw['kind']} operand requires cardinality")
        minimum_elements = cardinality.get("min")
        maximum_elements = cardinality.get("max")
        ceiling = 5 if raw["kind"] is OperandKind.TENSOR_COORDINATE else 64
        if (
            type(minimum_elements) is not int
            or type(maximum_elements) is not int
            or minimum_elements < 1
            or maximum_elements < minimum_elements
            or maximum_elements > ceiling
        ):
            raise ValueError(
                f"{raw['kind']} cardinality must be within 1..{ceiling} with min <= max"
            )
        raw_element_kinds = raw.get("element_kinds")
        if not isinstance(raw_element_kinds, list):
            raise ValueError(f"{raw['kind']} operand requires element_kinds")
        try:
            element_kinds = tuple(OperandKind(value) for value in raw_element_kinds)
        except ValueError as error:
            raise ValueError(f"{raw['kind']} element_kinds contain an unsupported kind") from error
        expected_element_kinds = (
            (OperandKind.REGISTER, OperandKind.IMMEDIATE)
            if raw["kind"] is OperandKind.TENSOR_COORDINATE
            else (OperandKind.REGISTER,)
        )
        if set(element_kinds) != set(expected_element_kinds) or len(
            element_kinds
        ) != len(expected_element_kinds):
            raise ValueError(
                f"{raw['kind']} element_kinds must be "
                f"{tuple(kind.value for kind in expected_element_kinds)!r}"
            )
    elif raw.get("cardinality") is not None or raw.get("element_kinds") is not None:
        raise ValueError(
            "cardinality and element_kinds are only valid for brace-pack primitives"
        )

    return _BracePackOptions(
        minimum_elements=minimum_elements,
        maximum_elements=maximum_elements,
        element_kinds=element_kinds,
    )


def _normalize_vector_options(raw: dict[str, Any]) -> _VectorOptions:
    """Normalize vector arity, type policy, and sink options in that order."""

    vector_arities: tuple[int, ...] = ()
    vector_arity_expression: OperandVectorArityExpression | None = None
    vector_type_policy = OperandVectorTypePolicy.AGGREGATE
    vector_allow_sink = False
    vector_sink_payload_bits = 0
    if raw["kind"] in {
        OperandKind.REGISTER_VECTOR,
        OperandKind.VECTOR_REGISTER,
        OperandKind.VECTOR_SPECIAL_REGISTER,
    }:
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
        if "sink_payload_bits" in vector:
            vector_sink_payload_bits = vector["sink_payload_bits"]
            if (
                type(vector_sink_payload_bits) is not int
                or not 8 <= vector_sink_payload_bits <= 256
                or vector_sink_payload_bits % 8 != 0
            ):
                raise ValueError(
                    f"{raw['kind']} vector.sink_payload_bits must be an 8..256 "
                    "multiple of eight."
                )
            if not vector_allow_sink:
                raise ValueError(
                    f"{raw['kind']} vector.sink_payload_bits requires vector.allow_sink."
                )

    return _VectorOptions(
        arities=vector_arities,
        arity_expression=vector_arity_expression,
        type_policy=vector_type_policy,
        allow_sink=vector_allow_sink,
        sink_payload_bits=vector_sink_payload_bits,
    )


def _normalize_register_width(
    raw: dict[str, Any],
    *,
    type_expression: OperandTypeExpression | None,
) -> OperandRegisterWidthPolicy:
    """Normalize register width using the already-normalized type expression."""

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
        if raw["kind"] not in {OperandKind.REGISTER, OperandKind.REGISTER_VECTOR}:
            raise ValueError(
                f"operand {raw['name']!r}: equal_or_wider register_width is "
                "only valid for kind 'reg' or 'reg_vector'"
            )
        if type_expression is None:
            raise ValueError(
                f"operand {raw['name']!r}: equal_or_wider register_width "
                "requires a type expression"
            )

    return register_width_policy


def _normalize_immediate_conversion(
    raw: dict[str, Any],
) -> OperandImmediateConversionPolicy:
    """Normalize immediate conversion and its operand-kind restriction."""

    try:
        immediate_conversion_policy = OperandImmediateConversionPolicy(
            raw.get("immediate_conversion", "narrow")
        )
    except ValueError as error:
        raise ValueError(
            f"operand {raw['name']!r}: unsupported immediate_conversion "
            f"{raw.get('immediate_conversion')!r}"
        ) from error
    if (
        immediate_conversion_policy
        is OperandImmediateConversionPolicy.REQUIRE_TARGET_RANGE
        and raw["kind"] not in {
            OperandKind.IMMEDIATE,
            OperandKind.REGISTER_OR_IMMEDIATE,
            OperandKind.TENSOR_COORDINATE,
        }
    ):
        raise ValueError(
            f"operand {raw['name']!r}: require_target_range immediate_conversion "
            "requires an immediate-capable operand"
        )

    return immediate_conversion_policy


def _normalize_address_options(raw: dict[str, Any]) -> _AddressOptions:
    """Normalize both address constraints before checking their relationship."""

    state_space_values, state_space_expression = _normalize_operand_state_space(
        raw.get("state_space")
    )
    parameter_constraint = _normalize_operand_parameter_constraint(raw.get("parameter"))
    has_address_constraint = (
        bool(state_space_values)
        or state_space_expression is not None
        or parameter_constraint is not None
    )
    if has_address_constraint and raw["kind"] not in {
        OperandKind.ADDRESS,
        OperandKind.CLUSTER_ADDRESS,
    }:
        raise ValueError(
            f"operand {raw['name']!r}: address constraints are only valid for "
            "kind 'addr' or 'cluster_address'"
        )
    if parameter_constraint is not None and state_space_expression is None:
        raise ValueError(
            f"operand {raw['name']!r}: parameter constraint requires a "
            "state_space modifier expression"
        )

    return _AddressOptions(
        state_space_values=state_space_values,
        state_space_expression=state_space_expression,
        parameter_constraint=parameter_constraint,
    )


def _parse_operand_kind(raw: dict[str, Any]) -> OperandKind:
    """Convert the YAML operand-kind spelling at the normalization boundary."""

    try:
        return OperandKind(raw["kind"])
    except ValueError as error:
        raise ValueError(
            f"operand {raw.get('name')!r}: unsupported operand kind "
            f"{raw.get('kind')!r}"
        ) from error


def _parse_operand_role(raw: dict[str, Any]) -> OperandRole | None:
    """Convert an optional YAML operand-role spelling at the boundary."""

    value = raw.get("role")
    if value is None:
        return None
    try:
        return OperandRole(value)
    except ValueError as error:
        raise ValueError(
            f"operand {raw.get('name')!r}: unsupported operand role {value!r}"
        ) from error


def _parse_operand_access(raw: dict[str, Any]) -> OperandAccess | None:
    """Convert an optional YAML operand-access spelling at the boundary."""

    value = raw.get("access")
    if value is None:
        return None
    try:
        return OperandAccess(value)
    except ValueError as error:
        raise ValueError(
            f"operand {raw.get('name')!r}: unsupported operand access {value!r}"
        ) from error
