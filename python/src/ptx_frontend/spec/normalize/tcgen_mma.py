"""Normalize caller-known dense MMA facts without interpreting source grammar.

Canonical operands and modifiers remain owned by the shared typed pipeline.
This adapter only prepares independent facts for the operational catalogue; it
must not be used to infer known words from live descriptor registers.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from ptx_frontend.spec.tcgen_mma_operations import (
    F16KnownFacts, Tf32KnownFacts, SharedOperandFacts,
)
from ptx_frontend.spec.model import (
    AsyncCompletionKind, ModifierKind, ModifierPresence, OperandKind,
    OperandTypeExpressionKind, SemanticRule, VariantSpec,
)

_FIELDS = frozenset(F16KnownFacts.__dataclass_fields__)
assert _FIELDS == frozenset(Tf32KnownFacts.__dataclass_fields__)
_SHARED_FIELDS = frozenset(SharedOperandFacts.__dataclass_fields__)
_BOOL_FIELDS = frozenset(("sparse", "transpose_a", "transpose_b", "a_shared"))
_INT_FIELDS = frozenset(("group", "m", "n", "k", "a_lane_half", "d_lane_half"))
_STR_FIELDS = frozenset(("d_type", "a_type", "b_type"))
_UNSCALED_TARGETS = {
    "any_of": [
        {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
        {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
        {"ptx": "9.0", "sm": 110, "target": "sm_110a"},
        {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
    ],
}
_SCALED_TARGETS = {"any_of": _UNSCALED_TARGETS["any_of"][:2]}


def validate_tcgen_mma_variant(variant: VariantSpec) -> None:
    """Keep each closed dense source kind tied to the typed MMA rule.

    The eight structural layouts encode A placement and optional operands;
    the written group remains a typed modifier rather than a variant index.
    """

    mods = {item.name: item for item in variant.modifiers}
    if variant.rule is not SemanticRule.TENSOR_MEMORY_MMA:
        if "mma" in mods or ("kind" in mods and "cta_group" in mods):
            raise ValueError("Tensor Memory MMA modifiers require its semantic rule")
        return
    kind = {"tcgen05_mma_f16": "f16",
            "tcgen05_mma_tf32": "tf32"}.get(variant.name)
    if kind is None:
        raise ValueError("unsupported dense MMA source kind")
    if (variant.completion_kind is not AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE
            or set(mods) != {"mma", "cta_group", "kind"}
            or variant.modifier_order_aliases != (("mma", "kind", "cta_group"),)
            or variant.availability != _UNSCALED_TARGETS):
        raise ValueError("dense f16 MMA action, group, kind, or order changed")
    if (mods["mma"].kind is not ModifierKind.FLAG or
            mods["mma"].presence is not ModifierPresence.FIXED or
            mods["mma"].token != ".mma" or mods["mma"].value is not True or
            mods["kind"].kind is not ModifierKind.FLAG or
            mods["kind"].presence is not ModifierPresence.FIXED or
            mods["kind"].token != f".kind::{kind}" or mods["kind"].value is not True or
            mods["cta_group"].kind is not ModifierKind.CTA_GROUP or
            mods["cta_group"].presence is not ModifierPresence.REQUIRED or
            tuple(value.value for value in mods["cta_group"].values) !=
            ("cta_group::1", "cta_group::2")):
        raise ValueError("dense f16 MMA typed qualifiers changed")
    expected = {
        f"{placement}_{'mask' if mask else 'no_mask'}_"
        f"{'scale' if scale else 'no_scale'}"
        for placement in ("shared", "tensor")
        for mask in (False, True) for scale in (False, True)
    }
    if {layout.name for layout in variant.operand_layouts} != expected:
        raise ValueError("dense f16 MMA requires exactly eight structural layouts")
    for layout in variant.operand_layouts:
        names = tuple(operand.name for operand in layout.operands)
        kinds = {operand.name: operand.kind for operand in layout.operands}
        a_kind = kinds.get("a")
        if a_kind is OperandKind.REGISTER:
            placement = "shared"
        elif a_kind is OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET:
            placement = "tensor"
        else:
            raise ValueError("dense f16 MMA A operand has unsupported kind")
        mask = "disable_output_lane" in names
        scale = "scale_input_d" in names
        expected_name = (f"{placement}_{'mask' if mask else 'no_mask'}_"
                         f"{'scale' if scale else 'no_scale'}")
        if layout.name != expected_name:
            raise ValueError("dense f16 MMA layout identity disagrees with typed roles")
        required = ("d", "a", "b", "idesc") + (
            ("disable_output_lane",) if mask else ()
        ) + ("enable_input_d",) + (("scale_input_d",) if scale else ())
        if names != required:
            raise ValueError("dense f16 MMA operand topology changed")
        if layout.availability != (_SCALED_TARGETS if scale else {}):
            raise ValueError("dense f16 MMA scaled target gate changed")
        if (kinds["d"] is not OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET or
                kinds["b"] is not OperandKind.REGISTER or
                kinds["idesc"] is not OperandKind.REGISTER or
                kinds["enable_input_d"] is not OperandKind.PREDICATE_SOURCE or
                (mask and kinds["disable_output_lane"] is not
                 OperandKind.REGISTER_VECTOR) or
                (scale and kinds["scale_input_d"] is not OperandKind.IMMEDIATE)):
            raise ValueError("dense f16 MMA source roles changed")
        expected_types = {"d": "u32", "a": (
            "b64" if placement == "shared" else "u32"),
            "b": "b64", "idesc": "b32", "enable_input_d": None}
        if mask:
            expected_types["disable_output_lane"] = "b32"
        if scale:
            expected_types["scale_input_d"] = "u32"
        for operand in layout.operands:
            expr = operand.type_expression
            expected_type = expected_types[operand.name]
            if (expected_type is None and expr is not None) or (
                expected_type is not None and (
                    expr is None or
                    expr.kind is not OperandTypeExpressionKind.FIXED_SCALAR or
                    expr.scalar_type != expected_type
                )
            ):
                raise ValueError("dense f16 MMA operand type changed")
        if mask:
            vector = next(item for item in layout.operands
                          if item.name == "disable_output_lane")
            if (vector.vector_arities != tuple(range(1, 9)) or
                    vector.vector_allowed_register_types !=
                    ("b32", "u32", "s32", "f32")):
                raise ValueError("dense f16 MMA mask carrier domain changed")


@dataclass(frozen=True)
class F16SourceTopology:
    """Closed dense f16 source placement/presence facts before owned lowering.

    ``mask_count`` records present source entries, never an inferred default.
    ``scale_source_value`` is the original evaluated integer, not narrowed bits.
    """

    group: int
    a_placement: str
    mask_count: int | None
    scale_source_value: int | None


def normalize_f16_source_topology(raw: dict[str, Any]) -> F16SourceTopology:
    """Validate the approved source topology without parsing operand spellings.

    The canonical typed pipeline still owns modifier order/provenance and
    register/predicate payloads. This check preserves absence versus an empty
    mask and rejects scale values outside the original integer domain.
    """

    expected = {"group", "a_placement", "mask_count", "scale_source_value"}
    if not isinstance(raw, dict) or set(raw) != expected:
        raise ValueError("dense f16 source topology needs four typed fields")
    group = raw["group"]
    a_placement = raw["a_placement"]
    count = raw["mask_count"]
    scale = raw["scale_source_value"]
    if type(group) is not int or group not in (1, 2):
        raise ValueError("dense f16 requires CTA group one or two")
    if a_placement not in ("shared", "tensor"):
        raise ValueError("dense f16 A placement must be shared or Tensor Memory")
    if count is not None and (type(count) is not int or
                              count != (4 if group == 1 else 8)):
        raise ValueError("present output-lane mask has wrong cardinality")
    if scale is not None and (type(scale) is not int or not 0 <= scale <= 15):
        raise ValueError("D scale requires original integer 0..15")
    return F16SourceTopology(group, a_placement, count, scale)


def normalize_f16_known_facts(raw: dict[str, Any]) -> F16KnownFacts:
    """Type-check an optional caller-known context without supplying defaults.

    Omitted fields remain unknown. A shared matrix role may contain optional
    major/swizzle names; no descriptor bits are decoded or authenticated here.
    """

    if not isinstance(raw, dict) or set(raw) - _FIELDS:
        raise ValueError("unknown dense f16 operational fact")
    values = dict(raw)
    for name in _BOOL_FIELDS:
        value = values.get(name)
        if value is not None and type(value) is not bool:
            raise ValueError(f"{name} requires a bool or unknown")
    for name in _INT_FIELDS:
        value = values.get(name)
        if value is not None and type(value) is not int:
            raise ValueError(f"{name} requires an integer or unknown")
    for name in _STR_FIELDS:
        value = values.get(name)
        if value is not None and type(value) is not str:
            raise ValueError(f"{name} requires a name or unknown")
    for name in ("a_shared_facts", "b_shared_facts"):
        value = values.get(name)
        if value is None or isinstance(value, SharedOperandFacts):
            continue
        if not isinstance(value, dict) or set(value) - _SHARED_FIELDS:
            raise ValueError(f"{name} requires known major/swizzle facts")
        if any(item is not None and type(item) is not str
               for item in value.values()):
            raise ValueError(f"{name} fact names must be strings")
        values[name] = SharedOperandFacts(**value)
    return F16KnownFacts(**values)


@dataclass(frozen=True)
class Tf32SourceTopology:
    """Known typed tf32 source placement and original optional values."""

    group: int
    a_placement: str
    mask_count: int | None
    scale_source_value: int | None


def normalize_tf32_source_topology(raw: dict[str, Any]) -> Tf32SourceTopology:
    """Apply the shared dense source bounds to a distinct tf32 fact type."""

    source = normalize_f16_source_topology(raw)
    return Tf32SourceTopology(**vars(source))


def normalize_tf32_known_facts(raw: dict[str, Any]) -> Tf32KnownFacts:
    """Check optional caller-known tf32 facts without reading live registers."""

    facts = normalize_f16_known_facts(raw)
    return Tf32KnownFacts(**vars(facts))
