from ptx_frontend.spec.model import *


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
