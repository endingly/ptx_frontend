"""Normalize and validate source-visible Tensor Memory copy and shift forms."""

from typing import Any

from ptx_frontend.spec.model import (
    AsyncCompletionKind, ModifierKind, ModifierPresence, OperandAccess,
    OperandKind, OperandRole, OperandTypeExpressionKind, SemanticRule,
    VariantSpec,
)


_PAIRS = (
    ("s128x256b", "none"),
    ("s4x256b", "none"),
    ("s128x128b", "none"),
    ("s64x128b", "warpx2_02_13"),
    ("s64x128b", "warpx2_01_23"),
    ("s32x128b", "warpx4"),
)
_FORMATS = ((False, False, False), (True, True, False), (True, False, True))


def normalize_tcgen_copy_contract(
    raw_variant: dict[str, Any],
) -> tuple[tuple[tuple[str, str], ...], tuple[tuple[bool, bool, bool], ...]]:
    """Retain the canonical closed pair tables for typed backend emission."""

    raw = raw_variant.get("tcgen_copy")
    if raw is None:
        return (), ()
    if not isinstance(raw, dict) or set(raw) != {"shape_multicast_pairs", "format_pairs"}:
        raise ValueError("Tensor Memory copy contract needs both closed pair tables")
    pairs = tuple(tuple(item) for item in raw["shape_multicast_pairs"])
    formats = tuple(tuple(item) for item in raw["format_pairs"])
    if (pairs != _PAIRS or formats != _FORMATS
            or any(type(value) is not bool for row in formats for value in row)):
        raise ValueError("Tensor Memory copy shape/multicast or format pairs changed")
    return pairs, formats


def validate_tcgen_copy_shift_variant(variant: VariantSpec) -> None:
    """Reject copy/shift topology drift before the generated checker is built."""

    modifier_names = {modifier.name for modifier in variant.modifiers}
    if ({"shift", "cta_group"} <= modifier_names
            and variant.rule is not SemanticRule.TENSOR_MEMORY_SHIFT):
        raise ValueError("Tensor Memory shift action requires the shift rule")
    if variant.rule not in {SemanticRule.TENSOR_MEMORY_COPY,
                            SemanticRule.TENSOR_MEMORY_SHIFT}:
        if variant.tcgen_copy_pairs or variant.tcgen_copy_formats:
            raise ValueError("Tensor Memory copy pairs belong only to copy")
        return
    copy = variant.rule is SemanticRule.TENSOR_MEMORY_COPY
    if (len(variant.operand_layouts) != 1
            or variant.completion_kind is not AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE):
        raise ValueError("Tensor Memory copy/shift requires one layout and TCGEN completion")
    mods = {modifier.name: modifier for modifier in variant.modifiers}
    group = mods.get("cta_group")
    if (group is None or group.kind is not ModifierKind.CTA_GROUP
            or group.presence is not ModifierPresence.REQUIRED
            or tuple(item.value for item in group.values)
            != ("cta_group::1", "cta_group::2")):
        raise ValueError("Tensor Memory copy/shift requires both written CTA groups")
    action = "cp" if copy else "shift"
    fixed = mods.get(action)
    if (fixed is None or fixed.kind is not ModifierKind.FLAG
            or fixed.presence is not ModifierPresence.FIXED
            or fixed.value is not True or fixed.token != f".{action}"):
        raise ValueError("Tensor Memory copy/shift action changed")
    address, *tail = variant.operand_layouts[0].operands
    if (address.name != "taddr"
            or address.kind is not OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET
            or address.role is not OperandRole.DESTINATION
            or address.access is not OperandAccess.WRITE
            or address.type_expression is None
            or address.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
            or address.type_expression.scalar_type != "u32"):
        raise ValueError("Tensor Memory copy/shift requires bracketed destination taddr")
    if not copy:
        down = mods.get("down")
        if (tail or set(mods) != {"shift", "cta_group", "down"}
                or down is None or down.kind is not ModifierKind.FLAG
                or down.presence is not ModifierPresence.REQUIRED
                or tuple((value.value, value.token) for value in down.values)
                != ((True, ".down"),)
                or variant.modifier_order_aliases != (("shift", "down", "cta_group"),)
                or variant.tcgen_copy_pairs or variant.tcgen_copy_formats):
            raise ValueError("Tensor Memory shift requires only .down and its documented alias")
        return
    if (len(tail) != 1 or tail[0].name != "s_desc"
            or tail[0].kind is not OperandKind.REGISTER
            or tail[0].role is not OperandRole.SOURCE
            or tail[0].access is not OperandAccess.READ
            or tail[0].type_expression is None
            or tail[0].type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
            or tail[0].type_expression.scalar_type != "b64"):
        raise ValueError("Tensor Memory copy requires one scalar descriptor register")
    expected = {"cp", "cta_group", "shape", "warpx2_02_13", "warpx2_01_23",
                "warpx4", "dst_format", "src_b6", "src_b4"}
    shape = mods.get("shape")
    if (set(mods) != expected or shape is None
            or shape.kind is not ModifierKind.TCGEN_SHAPE
            or shape.presence is not ModifierPresence.REQUIRED
            or tuple(value.value for value in shape.values)
            != ("s128x256b", "s4x256b", "s128x128b", "s64x128b", "s32x128b")
            or variant.tcgen_copy_pairs != _PAIRS
            or variant.tcgen_copy_formats != _FORMATS
            or variant.modifier_order_aliases):
        raise ValueError("Tensor Memory copy closed shape/format topology changed")
    tokens = {
        "warpx2_02_13": ".warpx2::02_13",
        "warpx2_01_23": ".warpx2::01_23",
        "warpx4": ".warpx4",
        "dst_format": ".b8x16",
        "src_b6": ".b6x16_p32",
        "src_b4": ".b4x16_p64",
    }
    for name, token in tokens.items():
        item = mods[name]
        if (item.kind is not ModifierKind.FLAG
                or item.presence is not ModifierPresence.OPTIONAL
                or item.default is not False or item.token != token):
            raise ValueError(f"Tensor Memory copy qualifier {name} changed")
