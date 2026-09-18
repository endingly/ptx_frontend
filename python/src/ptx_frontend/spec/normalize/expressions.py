from ptx_frontend.spec.model import *
import re
from .availability import *

_MODIFIER_TYPE_EXPR = re.compile(r"modifier\(([A-Za-z_][A-Za-z0-9_]*)\)")
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
        raise ValueError("operand state_space object must be a modifier expression")
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
        values.append(OperandStateSpaceValue(value=value, availability=availability))
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
