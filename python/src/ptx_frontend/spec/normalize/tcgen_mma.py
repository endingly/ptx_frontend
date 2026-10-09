"""Normalize caller-known dense MMA facts without interpreting source grammar.

Canonical operands and modifiers remain owned by the shared typed pipeline.
This adapter only prepares independent facts for the operational catalogue; it
must not be used to infer known words from live descriptor registers.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from ptx_frontend.spec.tcgen_mma_operations import (
    F16KnownFacts, Tf32KnownFacts, I8KnownFacts, F8F6F4KnownFacts,
    SharedOperandFacts,
)
from ptx_frontend.spec.model import (
    AsyncCompletionKind, ModifierKind, ModifierPresence, OperandAccess,
    OperandKind, OperandRole, OperandTypeExpressionKind, SemanticRule,
    VariantSpec,
)

_FIELDS = frozenset(F16KnownFacts.__dataclass_fields__)
assert _FIELDS == frozenset(Tf32KnownFacts.__dataclass_fields__)
assert _FIELDS == frozenset(I8KnownFacts.__dataclass_fields__)
assert _FIELDS == frozenset(F8F6F4KnownFacts.__dataclass_fields__)
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
_I8_TARGETS = {"any_of": [_UNSCALED_TARGETS["any_of"][0],
                          _UNSCALED_TARGETS["any_of"][2]]}


_MX8_VEC_TARGETS = {"any_of": [{"ptx": "8.6", "sm": 100, "target": "sm_100a"}]}
_MX8_BLOCK_TARGETS = {
    "any_of": [
        {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
        {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
    ],
}


def _validate_mx8_variant(variant: VariantSpec) -> None:
    """Keep source-legal block scaling and both A carriers in one typed form."""

    mods = {item.name: item for item in variant.modifiers}
    if (variant.completion_kind is not AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE
            or tuple(item.name for item in variant.modifiers) !=
            ("mma", "cta_group", "kind", "block_scale", "scale_vector_size")
            or variant.modifier_order_aliases != ()
            or variant.availability != _UNSCALED_TARGETS):
        raise ValueError("dense MX8 MMA action, qualifier order or target changed")
    if (mods["mma"].kind is not ModifierKind.FLAG or
            mods["mma"].presence is not ModifierPresence.FIXED or
            mods["mma"].token != ".mma" or mods["mma"].value is not True or
            mods["kind"].kind is not ModifierKind.FLAG or
            mods["kind"].presence is not ModifierPresence.FIXED or
            mods["kind"].token != ".kind::mxf8f6f4" or
            mods["kind"].value is not True or
            mods["block_scale"].kind is not ModifierKind.FLAG or
            mods["block_scale"].presence is not ModifierPresence.FIXED or
            mods["block_scale"].token != ".block_scale" or
            mods["block_scale"].value is not True or
            mods["cta_group"].kind is not ModifierKind.CTA_GROUP or
            mods["cta_group"].presence is not ModifierPresence.REQUIRED or
            tuple(value.value for value in mods["cta_group"].values) !=
            ("cta_group::1", "cta_group::2")):
        raise ValueError("dense MX8 MMA typed qualifier changed")
    scale = mods["scale_vector_size"]
    if (scale.kind is not ModifierKind.TCGEN_SCALE_VECTOR_SIZE or
            scale.presence is not ModifierPresence.OPTIONAL or
            scale.default != "absent" or
            tuple((value.value, value.token, value.availability)
                  for value in scale.values) != (
                      ("scale_vec::1X", ".scale_vec::1X", _MX8_VEC_TARGETS),
                      ("block32", ".block32", _MX8_BLOCK_TARGETS))):
        raise ValueError("dense MX8 scale selector or target changed")
    if {layout.name for layout in variant.operand_layouts} != {"shared", "tensor"}:
        raise ValueError("dense MX8 requires exact shared/Tensor Memory A forms")
    for layout in variant.operand_layouts:
        names = tuple(item.name for item in layout.operands)
        kinds = {item.name: item.kind for item in layout.operands}
        if (names != ("d", "a", "b", "idesc", "scale_a", "scale_b",
                      "enable_input_d") or layout.availability != {} or
                kinds != {"d": OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET,
                          "a": (OperandKind.REGISTER if layout.name == "shared"
                                else OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET),
                          "b": OperandKind.REGISTER, "idesc": OperandKind.REGISTER,
                          "scale_a": OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET,
                          "scale_b": OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET,
                          "enable_input_d": OperandKind.PREDICATE_SOURCE}):
            raise ValueError("dense MX8 operand topology changed")
        expected = {"d": "u32", "a": ("b64" if layout.name == "shared" else "u32"),
                    "b": "b64", "idesc": "b32", "scale_a": "u32",
                    "scale_b": "u32", "enable_input_d": None}
        expected_roles = {"d": (OperandRole.DESTINATION, OperandAccess.READ_WRITE),
                          "a": (OperandRole.SOURCE, OperandAccess.READ),
                          "b": (OperandRole.SOURCE, OperandAccess.READ),
                          "idesc": (OperandRole.SOURCE, OperandAccess.READ),
                          "scale_a": (OperandRole.SOURCE, OperandAccess.READ),
                          "scale_b": (OperandRole.SOURCE, OperandAccess.READ),
                          "enable_input_d": (OperandRole.PREDICATE,
                                             OperandAccess.READ)}
        for operand in layout.operands:
            expr = operand.type_expression
            want = expected[operand.name]
            if (operand.role, operand.access) != expected_roles[operand.name]:
                raise ValueError("dense MX8 operand role or access changed")
            if (want is None and expr is not None) or (
                    want is not None and (expr is None or
                    expr.kind is not OperandTypeExpressionKind.FIXED_SCALAR or
                    expr.scalar_type != want)):
                raise ValueError("dense MX8 operand scalar type changed")


def validate_tcgen_mma_variant(variant: VariantSpec) -> None:
    """Keep each closed dense source kind tied to the typed MMA rule.

    Four unscaled-no-D-scale or eight scaled-capable structural layouts encode A placement and
    optional operands; the written group remains one typed modifier value.
    """

    mods = {item.name: item for item in variant.modifiers}
    if variant.rule is not SemanticRule.TENSOR_MEMORY_MMA:
        if "mma" in mods or ("kind" in mods and "cta_group" in mods):
            raise ValueError("Tensor Memory MMA modifiers require its semantic rule")
        return
    kind = {"tcgen05_mma_f16": "f16",
            "tcgen05_mma_tf32": "tf32",
            "tcgen05_mma_i8": "i8",
            "tcgen05_mma_f8f6f4": "f8f6f4"}.get(variant.name)
    if variant.name == "tcgen05_mma_mxf8f6f4":
        _validate_mx8_variant(variant)
        return
    if kind is None:
        raise ValueError("unsupported dense MMA source kind")
    if (variant.completion_kind is not AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE
            or set(mods) != {"mma", "cta_group", "kind"}
            or variant.modifier_order_aliases !=
            (() if kind == "f8f6f4" else (("mma", "kind", "cta_group"),))
            or variant.availability !=
            (_I8_TARGETS if kind == "i8" else _UNSCALED_TARGETS)):
        raise ValueError("dense MMA action, group, kind, order or target changed")
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
        raise ValueError("dense MMA typed qualifiers changed")
    expected = set()
    for placement in ("shared", "tensor"):
        for mask in (False, True):
            prefix = f"{placement}_{'mask' if mask else 'no_mask'}"
            if kind in ("i8", "f8f6f4"):
                expected.add(prefix)
            else:
                expected.update((f"{prefix}_no_scale", f"{prefix}_scale"))
    if {layout.name for layout in variant.operand_layouts} != expected:
        raise ValueError("dense MMA structural layout set changed")
    for layout in variant.operand_layouts:
        names = tuple(operand.name for operand in layout.operands)
        kinds = {operand.name: operand.kind for operand in layout.operands}
        a_kind = kinds.get("a")
        if a_kind is OperandKind.REGISTER:
            placement = "shared"
        elif a_kind is OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET:
            placement = "tensor"
        else:
            raise ValueError("dense MMA A operand has unsupported kind")
        mask = "disable_output_lane" in names
        scale = "scale_input_d" in names
        if kind in ("i8", "f8f6f4") and scale:
            raise ValueError("dense kind has no D-scale source operand")
        expected_name = (f"{placement}_{'mask' if mask else 'no_mask'}"
                         + ("" if kind in ("i8", "f8f6f4") else
                            f"_{'scale' if scale else 'no_scale'}"))
        if layout.name != expected_name:
            raise ValueError("dense MMA layout identity disagrees with typed roles")
        required = ("d", "a", "b", "idesc") + (
            ("disable_output_lane",) if mask else ()
        ) + ("enable_input_d",) + (("scale_input_d",) if scale else ())
        if names != required:
            raise ValueError("dense MMA operand topology changed")
        if layout.availability != (_SCALED_TARGETS if scale else {}):
            raise ValueError("dense MMA layout target gate changed")
        if (kinds["d"] is not OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET or
                kinds["b"] is not OperandKind.REGISTER or
                kinds["idesc"] is not OperandKind.REGISTER or
                kinds["enable_input_d"] is not OperandKind.PREDICATE_SOURCE or
                (mask and kinds["disable_output_lane"] is not
                 OperandKind.REGISTER_VECTOR) or
                (scale and kinds["scale_input_d"] is not OperandKind.IMMEDIATE)):
            raise ValueError("dense MMA source roles changed")
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
                raise ValueError("dense MMA operand type changed")
        if mask:
            vector = next(item for item in layout.operands
                          if item.name == "disable_output_lane")
            if (vector.vector_arities != tuple(range(1, 9)) or
                    vector.vector_allowed_register_types !=
                    ("b32", "u32", "s32", "f32")):
                raise ValueError("dense MMA mask carrier domain changed")


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


@dataclass(frozen=True)
class I8SourceTopology:
    """Known typed i8 placement and optional mask entry count.

    The count is absent or exactly four/eight for group one/two; no scale
    source value exists in this kind's source grammar.
    """

    group: int
    a_placement: str
    mask_count: int | None


def normalize_i8_source_topology(raw: dict[str, Any]) -> I8SourceTopology:
    """Reuse shared mask/group checks while excluding any source D scale."""

    if not isinstance(raw, dict) or set(raw) != {
            "group", "a_placement", "mask_count"}:
        raise ValueError("dense i8 topology needs only group, A and mask")
    source = normalize_f16_source_topology({**raw, "scale_source_value": None})
    return I8SourceTopology(source.group, source.a_placement,
                            source.mask_count)


def normalize_i8_known_facts(raw: dict[str, Any]) -> I8KnownFacts:
    """Type-check independent known i8 facts without decoding live registers."""

    facts = normalize_f16_known_facts(raw)
    return I8KnownFacts(**vars(facts))


@dataclass(frozen=True)
class F8F6F4SourceTopology:
    """Canonical unscaled low-bit source placement and optional mask count."""

    group: int
    a_placement: str
    mask_count: int | None


def normalize_f8f6f4_source_topology(
        raw: dict[str, Any]) -> F8F6F4SourceTopology:
    """Use shared group/mask checks while forbidding scale and extra roles."""

    if not isinstance(raw, dict) or set(raw) != {
            "group", "a_placement", "mask_count"}:
        raise ValueError("dense f8f6f4 topology needs only group, A and mask")
    source = normalize_f16_source_topology({**raw, "scale_source_value": None})
    return F8F6F4SourceTopology(source.group, source.a_placement,
                                source.mask_count)


def normalize_f8f6f4_known_facts(raw: dict[str, Any]) -> F8F6F4KnownFacts:
    """Type-check independent known low-bit facts without decoding source."""

    facts = normalize_f16_known_facts(raw)
    return F8F6F4KnownFacts(**vars(facts))
