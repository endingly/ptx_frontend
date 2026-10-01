"""Validate the closed canonical Tensor Memory register-transfer forms."""

from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandKind,
    OperandRole,
    OperandTypeExpressionKind,
    SemanticRule,
    VariantSpec,
)


_TRANSFER_RULES = frozenset({
    SemanticRule.TENSOR_MEMORY_LOAD,
    SemanticRule.TENSOR_MEMORY_STORE,
    SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION,
    SemanticRule.TENSOR_MEMORY_WAIT,
})
_NUMS = ("x1", "x2", "x4", "x8", "x16", "x32", "x64", "x128")


def validate_tcgen_transfer_variant(variant: VariantSpec) -> None:
    """Reject source contracts that weaken transfer topology or completion."""

    if variant.rule not in _TRANSFER_RULES:
        return
    if len(variant.operand_layouts) != 1:
        raise ValueError("Tensor Memory transfer requires one operand layout")
    modifiers = {item.name: item for item in variant.modifiers}
    operands = variant.operand_layouts[0].operands
    names = tuple(item.name for item in operands)
    is_wait = variant.rule is SemanticRule.TENSOR_MEMORY_WAIT
    is_store = variant.rule is SemanticRule.TENSOR_MEMORY_STORE
    is_reduction = variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION
    split = "splitoff" in names

    for name in ("sync", "aligned"):
        item = modifiers.get(name)
        if (item is None or item.kind is not ModifierKind.FLAG
                or item.presence is not ModifierPresence.FIXED
                or item.value is not True):
            raise ValueError(f"Tensor Memory transfer requires fixed .{name}")
    if is_wait:
        wait = modifiers.get("wait")
        if (names or wait is None or wait.kind is not ModifierKind.TCGEN_WAIT
                or wait.presence is not ModifierPresence.FIXED
                or wait.value not in {"wait::ld", "wait::st"}):
            raise ValueError("Tensor Memory wait requires one fixed wait class and no operands")
        required = (AsyncCompletionKind.TCGEN_LOAD_WAIT if wait.value == "wait::ld"
                    else AsyncCompletionKind.TCGEN_STORE_WAIT)
        if variant.completion_kind is not required:
            raise ValueError("Tensor Memory wait completion class disagrees")
        if set(modifiers) != {"wait", "sync", "aligned"}:
            raise ValueError("Tensor Memory wait has an extra modifier")
        return

    action = "st" if is_store else "ld"
    if any(name in modifiers for name in {"ld", "st"} - {action}):
        raise ValueError("Tensor Memory transfer actions conflict")
    selected = modifiers.get(action)
    if (selected is None or selected.kind is not ModifierKind.FLAG
            or selected.presence is not ModifierPresence.FIXED
            or selected.value is not True):
        raise ValueError("Tensor Memory transfer requires its fixed action")
    required_completion = (AsyncCompletionKind.TCGEN_STORE_WAIT if is_store
                           else AsyncCompletionKind.TCGEN_LOAD_WAIT)
    if variant.completion_kind is not required_completion:
        raise ValueError("Tensor Memory transfer completion class disagrees")
    if is_reduction != ("red" in modifiers):
        raise ValueError("Tensor Memory reduction action disagrees")
    element_type = modifiers.get("type")
    if element_type is None or element_type.kind is not ModifierKind.TYPE:
        raise ValueError("Tensor Memory transfer needs a typed element width")
    expected_modifiers = {action, "sync", "aligned", "shape", "num", "type"}
    if is_reduction:
        red = modifiers["red"]
        op = modifiers.get("red_op")
        if (red.kind is not ModifierKind.FLAG
                or red.presence is not ModifierPresence.FIXED
                or red.value is not True or op is None
                or op.kind is not ModifierKind.TCGEN_RED_OP
                or op.presence is not ModifierPresence.REQUIRED
                or tuple(value.value for value in op.values) != ("min", "max")):
            raise ValueError("Tensor Memory reduction operator changed")
        expected_modifiers |= {"red", "red_op"}
        if element_type.value == "f32":
            expected_modifiers |= {"abs", "nan"}
            for control in ("abs", "nan"):
                item = modifiers.get(control)
                if (item is None or item.kind is not ModifierKind.FLAG
                        or item.presence is not ModifierPresence.OPTIONAL
                        or item.default is not False):
                    raise ValueError("Floating reduction controls changed")
        elif (element_type.presence is not ModifierPresence.REQUIRED
              or tuple(value.value for value in element_type.values)
              != ("u32", "s32")):
            raise ValueError("Integer reduction types changed")
        canonical = tuple(item.name for item in variant.modifiers)
        alias = tuple(name for name in canonical if name != "type")
        position = alias.index("red_op")
        alias = alias[:position] + ("type",) + alias[position:]
        if variant.modifier_order_aliases != (alias,):
            raise ValueError("Reduction source-order alias changed")
    else:
        packed = modifiers.get("unpack" if is_store else "pack")
        if (element_type.presence is not ModifierPresence.FIXED
                or element_type.value != "b32" or packed is None
                or packed.kind is not ModifierKind.FLAG
                or packed.presence is not ModifierPresence.OPTIONAL
                or packed.default is not False):
            raise ValueError("Tensor Memory packing or data type changed")
        expected_modifiers.add("unpack" if is_store else "pack")
    if set(modifiers) != expected_modifiers:
        raise ValueError("Tensor Memory transfer modifier topology changed")
    shape = modifiers.get("shape")
    num = modifiers.get("num")
    if (shape is None or shape.kind is not ModifierKind.TCGEN_SHAPE
            or num is None or num.kind is not ModifierKind.TCGEN_NUM
            or num.presence is not ModifierPresence.REQUIRED
            or tuple(value.value for value in num.values) != _NUMS[1 if is_reduction else 0:]):
        raise ValueError("Tensor Memory transfer shape/repeat domain changed")
    if split:
        if (shape.presence is not ModifierPresence.FIXED
                or shape.value != "s16x32bx2"):
            raise ValueError("Half-split offset requires the split shape")
    elif is_reduction:
        if (shape.presence is not ModifierPresence.FIXED
                or shape.value != "s32x32b"):
            raise ValueError("Base reduction requires 32x32b")
    elif (shape.presence is not ModifierPresence.REQUIRED
          or tuple(value.value for value in shape.values)
          != ("s32x32b", "s16x64b", "s16x128b", "s16x256b")):
        raise ValueError("Base transfer requires four non-split shapes")

    expected_names = (("taddr", "splitoff", "r") if is_store and split else
                      ("taddr", "r") if is_store else
                      ("r", "redval", "taddr", "splitoff") if is_reduction and split else
                      ("r", "redval", "taddr") if is_reduction else
                      ("r", "taddr", "splitoff") if split else
                      ("r", "taddr"))
    if names != expected_names:
        raise ValueError("Tensor Memory transfer operand topology changed")
    by_name = {item.name: item for item in operands}
    r = by_name["r"]
    if (r.kind is not OperandKind.MATRIX_FRAGMENT
            or r.access is not (OperandAccess.READ if is_store else OperandAccess.WRITE)
            or r.minimum_elements != (2 if is_reduction else 1)
            or r.maximum_elements != 128
            or r.element_kinds != (OperandKind.REGISTER,)
            or r.type_expression is None
            or r.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
            or r.type_expression.scalar_type != "b32"):
        raise ValueError("Tensor Memory register fragment contract changed")
    address = by_name["taddr"]
    if (address.kind is not OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET
            or address.role is not (OperandRole.DESTINATION if is_store
                                    else OperandRole.SOURCE)
            or address.access is not (OperandAccess.WRITE if is_store else OperandAccess.READ)
            or address.type_expression is None
            or address.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
            or address.type_expression.scalar_type != "u32"):
        raise ValueError("Tensor Memory transfer requires a bracketed taddr")
    if split and (by_name["splitoff"].kind is not OperandKind.TCGEN_HALF_SPLIT_OFFSET
                  or by_name["splitoff"].type_expression is not None):
        raise ValueError("Tensor Memory split offset lost its source-only domain")
    if is_reduction and (by_name["redval"].kind is not OperandKind.REGISTER
                         or by_name["redval"].access is not OperandAccess.WRITE):
        raise ValueError("Tensor Memory reduction requires a scalar result")
