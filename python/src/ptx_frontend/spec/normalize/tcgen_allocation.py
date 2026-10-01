"""Validate the closed Tensor Memory allocation source contract."""

from ptx_frontend.spec.model import (
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandKind,
    OperandTypeExpressionKind,
    SemanticRule,
    VariantSpec,
)


_ALLOCATION_RULES = frozenset({
    SemanticRule.TENSOR_MEMORY_ALLOC,
    SemanticRule.TENSOR_MEMORY_DEALLOC,
    SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT,
})


def validate_tcgen_allocation_variant(variant: VariantSpec) -> None:
    """Reject a canonical allocation form that would weaken its typed checker."""

    if variant.rule not in _ALLOCATION_RULES:
        return
    if len(variant.operand_layouts) != 1:
        raise ValueError("Tensor Memory allocation requires one flat operand layout")
    modifiers = {modifier.name: modifier for modifier in variant.modifiers}
    group = modifiers.get("cta_group")
    if (group is None or group.kind is not ModifierKind.CTA_GROUP
            or group.presence is not ModifierPresence.REQUIRED
            or tuple(value.value for value in group.values)
            != ("cta_group::1", "cta_group::2")):
        raise ValueError("Tensor Memory allocation requires CTA groups 1 and 2")
    for name in ("sync", "aligned"):
        fixed = modifiers.get(name)
        if (fixed is None or fixed.kind is not ModifierKind.FLAG
                or fixed.presence is not ModifierPresence.FIXED
                or fixed.value is not True):
            raise ValueError(f"Tensor Memory allocation requires fixed .{name}")
    action_name = {
        SemanticRule.TENSOR_MEMORY_ALLOC: "alloc",
        SemanticRule.TENSOR_MEMORY_DEALLOC: "dealloc",
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT:
            "relinquish_alloc_permit",
    }[variant.rule]
    action = modifiers.get(action_name)
    if (action is None or action.kind is not ModifierKind.FLAG
            or action.presence is not ModifierPresence.FIXED
            or action.value is not True):
        raise ValueError(f"Tensor Memory allocation requires .{action_name}")
    if any(name in modifiers for name in
           {"alloc", "dealloc", "relinquish_alloc_permit"} - {action_name}):
        raise ValueError("Tensor Memory allocation has conflicting actions")
    element_type = modifiers.get("type")
    if variant.rule is SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT:
        if element_type is not None:
            raise ValueError("Tensor Memory permit release has no element type")
    elif (element_type is None or element_type.kind is not ModifierKind.TYPE
          or element_type.presence is not ModifierPresence.FIXED
          or element_type.value != "b32"):
        raise ValueError("Tensor Memory allocation requires fixed .b32")

    operands = variant.operand_layouts[0].operands
    expected_names = {
        SemanticRule.TENSOR_MEMORY_ALLOC: ("dst", "ncols"),
        SemanticRule.TENSOR_MEMORY_DEALLOC: ("taddr", "ncols"),
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT: (),
    }[variant.rule]
    if tuple(operand.name for operand in operands) != expected_names:
        raise ValueError("Tensor Memory allocation operand roles changed")
    if variant.rule is SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT:
        if "shared_cta" in modifiers:
            raise ValueError("Tensor Memory permit release has no result slot")
        return
    count = operands[-1]
    if (count.kind is not OperandKind.REGISTER_OR_IMMEDIATE
            or count.type_expression is None
            or count.type_expression.kind
            is not OperandTypeExpressionKind.FIXED_SCALAR
            or count.type_expression.scalar_type != "u32"):
        raise ValueError("Tensor Memory column count requires a u32 use")
    if variant.rule is SemanticRule.TENSOR_MEMORY_DEALLOC:
        if "shared_cta" in modifiers:
            raise ValueError("Tensor Memory deallocation has no result slot")
        address = operands[0]
        if (address.kind is not OperandKind.TENSOR_MEMORY_ADDRESS
                or address.type_expression is None
                or address.type_expression.scalar_type != "u32"):
            raise ValueError("Tensor Memory deallocation requires a32 taddr")
        return
    destination = operands[0]
    if (destination.kind is not OperandKind.ADDRESS
            or destination.access is not OperandAccess.WRITE
            or len(variant.address_alignments) != 1
            or variant.address_alignments[0].address_operands != ("dst",)
            or variant.address_alignments[0].alignment != 4):
        raise ValueError("Tensor Memory allocation requires a 4-byte result slot")
    written_shared = "shared_cta" in modifiers
    spaces = tuple(space.value for space in destination.state_space_values)
    if spaces != (("shared",) if written_shared else ()):
        raise ValueError("Tensor Memory result-slot qualifier and space disagree")
