"""Caller-supplied tensor-map facts and finite PTX 9.3 compatibility rules.

This module never decodes a tensor-map object.  Encoded field identities and
selected-form availability are inputs supplied by the existing typed frontend
queries; they are not reconstructed from a raw code or variant name here.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType
from typing import Callable

from ptx_frontend.ir.resolved_ir import (
    TensorAccessMode,
    TensorDestination,
    tensor_im2col_info_contract,
)
from ptx_frontend.ir.tensor_reduction import (
    TENSOR_REDUCTION_ELEMENT_TYPES,
    TensorReductionOp,
)
from ptx_frontend.spec.semantic_domains import SemanticDomain, is_semantic_value


class TensorFactRule(Enum):
    """Stable identifiers for the twenty-two independently reported relations."""

    ELEMENT_IDENTITY = 1
    REDUCTION_TYPE = 2
    BASE_BOX = 3
    TILED_TRAVERSAL = 4
    FILL_PACKED = 5
    SUBBYTE_OPERATION = 6
    PACKED_GEOMETRY = 7
    PACKED_SWIZZLE = 8
    INTERLEAVE = 9
    SWIZZLE_ATOMICITY = 10
    SWIZZLE_ALIGNMENT = 11
    PATTERN_BASE_OFFSET = 12
    SWIZZLE_96 = 13
    FLIP_8 = 14
    SM103A_PACKED_STORE = 15
    SM120A_CLUSTER = 16
    STORE_BOUNDS = 17
    IM2COL_SHAPE = 18
    IM2COL_INFO = 19
    W_PROFILE = 20
    GATHER_SCATTER = 21
    SELECTED_AVAILABILITY = 22


class FactStatus(Enum):
    """A rule's result, without implying whole-descriptor validity."""

    CHECKED = "checked"
    VIOLATED = "violated"
    UNRESOLVED = "unresolved"
    NOT_APPLICABLE = "not_applicable"


class TensorDirection(Enum):
    """Selected tensor operation direction, independent of encoded format."""

    LOAD = "load"
    STORE = "store"
    PREFETCH = "prefetch"
    REDUCE = "reduce"


@dataclass(frozen=True)
class ConvertedInteger:
    """Known instruction-use value and optional original 64-bit source bits.

    ``value`` is the signed S32 coordinate or unsigned U16 information value
    after PTX integer-constant conversion.  ``source_bits`` is absent for a
    dynamically known register value; when present it retains authored bits.
    """

    value: int | None = None
    source_bits: int | None = None


@dataclass(frozen=True)
class ProjectedField:
    """Result of an existing Table 33 field-specific typed projection.

    ``identity`` uses the accepted domain spelling supplied by the caller.
    For element code 15 it may be absent when the directional meaning is not
    independently known; that absence is an unresolved semantic obligation.
    ``valid`` is the existing projection's result, not a second code table.
    A raw code without a projection remains unresolved; ``valid=False``
    diagnoses an invalid source/code/domain pair.
    """

    code: int | None = None
    identity: str | None = None
    valid: bool | None = None


@dataclass(frozen=True)
class TensorAccessContext:
    """Value-owned selected tensor access, supplied by canonical lowering.

    The future selected-form adapter must populate availability using accepted
    variant/value queries.  ``target_identity`` is an exact catalog identity,
    never a numeric-SM threshold or guessed family match.
    """

    direction: TensorDirection
    mode: TensorAccessMode
    rank: int
    destination: TensorDestination | None = None
    target_identity: str | None = None
    reduction_op: TensorReductionOp | None = None
    coordinates: tuple[ConvertedInteger, ...] | None = None
    info: tuple[ConvertedInteger, ...] | None = None
    selected_variant_available: bool | None = None
    selected_value_available: bool | None = None
    active_atomicity_use: bool | None = None
    group: int | None = None


@dataclass(frozen=True)
class TensorMapKnownFacts:
    """Independent claimed descriptor/access facts; no map-byte authentication.

    Array entries may themselves be absent.  ``None`` means unknown; a known
    zero remains zero.  Element counts/steps, byte sizes/strides, and distinct
    addresses retain their own units and are never implicitly converted.
    """

    rank: int | None = None
    element: ProjectedField | None = None
    interleave: ProjectedField | None = None
    swizzle: ProjectedField | None = None
    atomicity: ProjectedField | None = None
    fill: ProjectedField | None = None
    full_dimensions_elements: tuple[int | None, ...] | None = None
    tiled_box_elements: tuple[int | None, ...] | None = None
    traversal_steps_elements: tuple[int | None, ...] | None = None
    global_strides_bytes: tuple[int | None, ...] | None = None
    tensor_strides_bytes: tuple[int | None, ...] | None = None
    im2col_spatial_box_elements: tuple[int | None, ...] | None = None
    im2col_lower_edge_offsets: tuple[int | None, ...] | None = None
    im2col_upper_edge_offsets: tuple[int | None, ...] | None = None
    w_box_elements: int | None = None
    w_lower_edge_offset: int | None = None
    w_upper_edge_offset: int | None = None
    pixels_per_column_elements: int | None = None
    channels_per_pixel_elements: int | None = None
    available_ndhw_pixels: int | None = None
    inner_box_bytes: int | None = None
    inner_tensor_bytes: int | None = None
    accessed_box_bytes: int | None = None
    map_object_address_bytes: int | None = None
    data_base_address_bytes: int | None = None
    accessed_box_address_bytes: int | None = None
    shared_destination_address_bytes: int | None = None
    reduction_interpretation: str | None = None


@dataclass(frozen=True)
class FactDiagnostic:
    """Malformed caller claim detected before a dependent rule is evaluated."""

    field: str
    message: str


@dataclass(frozen=True)
class FactOutcome:
    """One bounded relation and the exact missing or violated prerequisites."""

    rule: TensorFactRule
    status: FactStatus
    detail: str
    fields: tuple[str, ...] = ()
    derived_value: int | None = None


@dataclass(frozen=True)
class TensorFactsReport:
    """All rule outcomes plus independent malformed-input diagnostics."""

    outcomes: tuple[FactOutcome, ...]
    diagnostics: tuple[FactDiagnostic, ...] = ()

    def outcome(self, rule: TensorFactRule) -> FactOutcome:
        """Return a rule's sole result without an aggregate validity claim."""

        return self.outcomes[rule.value - 1]


@dataclass(frozen=True)
class RuleDefinition:
    """Catalog row consumed by the Python query and future C++ renderer."""

    rule: TensorFactRule
    title: str
    inputs: tuple[str, ...]
    source_section: str


RULE_CATALOG = MappingProxyType({
    TensorFactRule.ELEMENT_IDENTITY: RuleDefinition(TensorFactRule.ELEMENT_IDENTITY, "encoded element and direction", ("element", "direction"), "5.5.1.1/Table 33"),
    TensorFactRule.REDUCTION_TYPE: RuleDefinition(TensorFactRule.REDUCTION_TYPE, "reduction interpretation", ("direction", "mode", "rank", "reduction_op", "reduction_interpretation", "element"), "9.7.9.26.5.3"),
    TensorFactRule.BASE_BOX: RuleDefinition(TensorFactRule.BASE_BOX, "tiled byte box and address", ("mode", "accessed_box_bytes", "accessed_box_address_bytes"), "5.5.3.1"),
    TensorFactRule.TILED_TRAVERSAL: RuleDefinition(TensorFactRule.TILED_TRAVERSAL, "element traversal step", ("mode", "interleave", "traversal_steps_elements"), "5.5.3.2"),
    TensorFactRule.FILL_PACKED: RuleDefinition(TensorFactRule.FILL_PACKED, "packed out-of-bounds fill", ("element", "fill"), "9.7.9.26.5.1"),
    TensorFactRule.SUBBYTE_OPERATION: RuleDefinition(TensorFactRule.SUBBYTE_OPERATION, "sub-byte operation restrictions", ("element", "direction", "destination", "target_identity"), "9.7.9.26.5.1"),
    TensorFactRule.PACKED_GEOMETRY: RuleDefinition(TensorFactRule.PACKED_GEOMETRY, "packed byte geometry", ("target_identity", "direction", "element", "inner_box_bytes", "inner_tensor_bytes", "tensor_strides_bytes", "data_base_address_bytes", "coordinates"), "9.7.9.26.5.1"),
    TensorFactRule.PACKED_SWIZZLE: RuleDefinition(TensorFactRule.PACKED_SWIZZLE, "packed swizzle", ("target_identity", "direction", "element", "swizzle", "atomicity"), "9.7.9.26.5.1"),
    TensorFactRule.INTERLEAVE: RuleDefinition(TensorFactRule.INTERLEAVE, "interleave applicability", ("interleave", "rank", "mode", "channels_per_pixel_elements", "element"), "5.5.6"),
    TensorFactRule.SWIZZLE_ATOMICITY: RuleDefinition(TensorFactRule.SWIZZLE_ATOMICITY, "selected swizzle atomicity", ("swizzle", "atomicity", "active_atomicity_use"), "5.5.7/Table 14"),
    TensorFactRule.SWIZZLE_ALIGNMENT: RuleDefinition(TensorFactRule.SWIZZLE_ALIGNMENT, "shared atomic destination alignment", ("swizzle", "atomicity", "shared_destination_address_bytes"), "5.5.7"),
    TensorFactRule.PATTERN_BASE_OFFSET: RuleDefinition(TensorFactRule.PATTERN_BASE_OFFSET, "shared repeating-pattern offset", ("swizzle", "shared_destination_address_bytes"), "5.5.7"),
    TensorFactRule.SWIZZLE_96: RuleDefinition(TensorFactRule.SWIZZLE_96, "96-byte swizzle compatibility", ("swizzle", "atomicity", "interleave", "inner_box_bytes", "element", "mode"), "9.7.9.26.5.1"),
    TensorFactRule.FLIP_8: RuleDefinition(TensorFactRule.FLIP_8, "128-byte 8-byte-flip restriction", ("swizzle", "atomicity", "direction", "mode"), "9.7.9.26.5.1"),
    TensorFactRule.SM103A_PACKED_STORE: RuleDefinition(TensorFactRule.SM103A_PACKED_STORE, "exact sm103a packed-store override", ("target_identity", "direction", "element", "inner_box_bytes", "inner_tensor_bytes", "data_base_address_bytes", "tensor_strides_bytes", "coordinates", "swizzle", "atomicity", "active_atomicity_use"), "9.7.9.26.5.1"),
    TensorFactRule.SM120A_CLUSTER: RuleDefinition(TensorFactRule.SM120A_CLUSTER, "exact sm120a cluster exclusions", ("target_identity", "direction", "destination", "element", "active_atomicity_use"), "9.7.9.26.5.1"),
    TensorFactRule.STORE_BOUNDS: RuleDefinition(TensorFactRule.STORE_BOUNDS, "store coordinate and corner signs", ("direction", "mode", "coordinates", "im2col_lower_edge_offsets", "im2col_upper_edge_offsets"), "9.7.9.26.5.1"),
    TensorFactRule.IM2COL_SHAPE: RuleDefinition(TensorFactRule.IM2COL_SHAPE, "im2col spatial box and edge offsets", ("mode", "rank", "im2col_spatial_box_elements", "im2col_lower_edge_offsets", "im2col_upper_edge_offsets"), "5.5.4.1"),
    TensorFactRule.IM2COL_INFO: RuleDefinition(TensorFactRule.IM2COL_INFO, "im2col U16 information and pixel count", ("mode", "rank", "info", "fill", "pixels_per_column_elements", "available_ndhw_pixels"), "5.5.4.2/9.7.9.26.5.2"),
    TensorFactRule.W_PROFILE: RuleDefinition(TensorFactRule.W_PROFILE, "W/W128 box and offset profile", ("mode", "rank", "w_box_elements", "w_lower_edge_offset", "w_upper_edge_offset", "coordinates", "im2col_spatial_box_elements", "info", "interleave", "swizzle", "atomicity", "active_atomicity_use"), "5.5.5/9.7.9.26.5.2"),
    TensorFactRule.GATHER_SCATTER: RuleDefinition(TensorFactRule.GATHER_SCATTER, "four-row gather/scatter topology", ("mode", "rank", "coordinates", "tiled_box_elements", "interleave"), "5.5.3.4"),
    TensorFactRule.SELECTED_AVAILABILITY: RuleDefinition(TensorFactRule.SELECTED_AVAILABILITY, "canonical selected-form availability", ("selected_variant_available", "selected_value_available"), "9.7.9.26.5/Table 63"),
})


_PACKED = frozenset({"b4x16", "b4x16_p64", "b6x16_p32", "b6p2x16"})
_PACKED_RESTRICTED = frozenset({"b4x16_p64", "b6x16_p32", "b6p2x16"})
_IM2COL = frozenset({TensorAccessMode.IM2COL, TensorAccessMode.IM2COL_W, TensorAccessMode.IM2COL_W128})
_W = frozenset({TensorAccessMode.IM2COL_W, TensorAccessMode.IM2COL_W128})
_GATHER = frozenset({TensorAccessMode.TILE_GATHER4, TensorAccessMode.TILE_SCATTER4})
_TILED = frozenset({TensorAccessMode.TILED, *_GATHER})


def _field(claim: ProjectedField | None) -> str | None:
    """Use only an existing projection's verified identity."""

    return claim.identity if isinstance(claim, ProjectedField) and claim.valid is True else None


def _result(rule: TensorFactRule, status: FactStatus, detail: str,
            *fields: str, derived_value: int | None = None) -> FactOutcome:
    """Construct one result without aggregating unrelated obligations."""

    return FactOutcome(rule, status, detail, tuple(fields), derived_value)


def _ok(rule: TensorFactRule, detail: str, *fields: str,
        derived_value: int | None = None) -> FactOutcome:
    """Record a sourced relation that supplied values actually satisfy."""

    return _result(rule, FactStatus.CHECKED, detail, *fields,
                   derived_value=derived_value)


def _bad(rule: TensorFactRule, detail: str, *fields: str) -> FactOutcome:
    """Record a counterexample to a sourced relation."""

    return _result(rule, FactStatus.VIOLATED, detail, *fields)


def _missing(rule: TensorFactRule, detail: str, *fields: str) -> FactOutcome:
    """Name facts or an undocumented relationship that prevents checking."""

    return _result(rule, FactStatus.UNRESOLVED, detail, *fields)


def _na(rule: TensorFactRule, detail: str) -> FactOutcome:
    """Record that a rule does not apply to this selected access."""

    return _result(rule, FactStatus.NOT_APPLICABLE, detail)


def _checked_int(value: ConvertedInteger, width: int, signed: bool) -> bool:
    """Check converted use bits without constraining original source magnitude."""

    if value.value is None:
        return value.source_bits is None
    if not isinstance(value.value, int) or isinstance(value.value, bool):
        return False
    minimum = -(1 << (width - 1)) if signed else 0
    maximum = (1 << (width - 1)) - 1 if signed else (1 << width) - 1
    if not minimum <= value.value <= maximum:
        return False
    if value.source_bits is None:
        return True
    if not isinstance(value.source_bits, int) or isinstance(value.source_bits, bool):
        return False
    if not 0 <= value.source_bits <= (1 << 64) - 1:
        return False
    converted = value.source_bits & ((1 << width) - 1)
    if signed and converted & (1 << (width - 1)):
        converted -= 1 << width
    return converted == value.value


def _validity(context: TensorAccessContext, facts: TensorMapKnownFacts) -> tuple[FactDiagnostic, ...]:
    """Diagnose malformed caller values before indexing arrays or enum tables."""

    problems: list[FactDiagnostic] = []
    if not isinstance(context.direction, TensorDirection):
        problems.append(FactDiagnostic("direction", "unknown selected direction"))
    if not isinstance(context.mode, TensorAccessMode):
        problems.append(FactDiagnostic("mode", "unknown selected tensor mode"))
    if not isinstance(context.rank, int) or isinstance(context.rank, bool) or context.rank not in range(1, 6):
        problems.append(FactDiagnostic("rank", "selected rank must be 1..5"))
    if context.destination is not None and not isinstance(context.destination, TensorDestination):
        problems.append(FactDiagnostic("destination", "unknown destination topology"))
    if context.target_identity is not None and not isinstance(context.target_identity, str):
        problems.append(FactDiagnostic("target_identity", "target identity must be a catalog spelling"))
    if context.reduction_op is not None and not isinstance(context.reduction_op, TensorReductionOp):
        problems.append(FactDiagnostic("reduction_op", "unknown reduction operation"))
    if context.group is not None and (isinstance(context.group, bool) or context.group not in (1, 2)):
        problems.append(FactDiagnostic("group", "selected group must be one or two"))
    for name in ("selected_variant_available", "selected_value_available", "active_atomicity_use"):
        value = getattr(context, name)
        if value is not None and not isinstance(value, bool):
            problems.append(FactDiagnostic(name, "selected-use claim must be true, false or unknown"))
    if facts.rank is not None and (not isinstance(facts.rank, int) or
                                   isinstance(facts.rank, bool) or
                                   facts.rank not in range(1, 6)):
        problems.append(FactDiagnostic("facts.rank", "known descriptor rank must be 1..5"))
    elif facts.rank is not None and facts.rank != context.rank:
        problems.append(FactDiagnostic("facts.rank", "known descriptor rank differs from selected rank"))
    for name in ("element", "interleave", "swizzle", "atomicity", "fill"):
        claim = getattr(facts, name)
        if claim is not None and (not isinstance(claim, ProjectedField) or claim.valid is False):
            problems.append(FactDiagnostic(name, "existing encoded-field projection rejected this claim"))
        elif claim is not None and claim.valid is True and (
                (not isinstance(claim.identity, str) or not claim.identity) and
                not (name == "element" and claim.identity is None and
                     claim.code == 15) or
                not isinstance(claim.code, int) or isinstance(claim.code, bool) or claim.code < 0):
            problems.append(FactDiagnostic(name, "verified projection lacks an encoded identity/code"))
        elif claim is not None and claim.valid is not None and not isinstance(claim.valid, bool):
            problems.append(FactDiagnostic(name, "projection validity must be true, false or unknown"))
        elif (name == "element" and claim is not None and claim.valid is True
              and claim.code == 15 and claim.identity is not None and
              claim.identity not in ("b6x16_p32", "b6p2x16")):
            problems.append(FactDiagnostic(name, "unknown code-15 directional interpretation"))
    if facts.reduction_interpretation is not None and (
            not isinstance(facts.reduction_interpretation, str) or
            not is_semantic_value(SemanticDomain.SCALAR_TYPE,
                                  facts.reduction_interpretation)):
        problems.append(FactDiagnostic("reduction_interpretation", "unknown scalar interpretation"))
    unsigned_arrays = ("full_dimensions_elements", "tiled_box_elements",
                       "traversal_steps_elements", "global_strides_bytes",
                       "tensor_strides_bytes", "im2col_spatial_box_elements")
    signed_arrays = ("im2col_lower_edge_offsets", "im2col_upper_edge_offsets")
    for name in (*unsigned_arrays, *signed_arrays):
        entries = getattr(facts, name)
        if entries is not None and (not isinstance(entries, tuple) or
                len(entries) > (3 if name in signed_arrays else 5) or
                any(value is not None and (
                    not isinstance(value, int) or isinstance(value, bool) or
                    (name in unsigned_arrays and not 0 <= value <= (1 << 64) - 1) or
                    (name in signed_arrays and not -(1 << 31) <= value < (1 << 31))
                ) for value in entries)):
            problems.append(FactDiagnostic(name, "malformed array entries or units"))
    for name in ("full_dimensions_elements", "tiled_box_elements", "traversal_steps_elements"):
        entries = getattr(facts, name)
        if isinstance(entries, tuple) and isinstance(facts.rank, int) and not isinstance(facts.rank, bool) and len(entries) != facts.rank:
            problems.append(FactDiagnostic(name, "array arity differs from known descriptor rank"))
    if isinstance(facts.global_strides_bytes, tuple) and isinstance(facts.rank, int) and not isinstance(facts.rank, bool) and len(facts.global_strides_bytes) != facts.rank - 1:
        problems.append(FactDiagnostic("global_strides_bytes", "global-stride arity must be rank minus one"))
    if context.coordinates is not None and not isinstance(context.coordinates, tuple):
        problems.append(FactDiagnostic("coordinates", "coordinates must be a tuple of converted values"))
    elif context.coordinates is not None and isinstance(context.rank, int) and context.rank in range(1, 6):
        expected = 5 if isinstance(context.mode, TensorAccessMode) and context.mode in _GATHER else context.rank
        if len(context.coordinates) != expected:
            problems.append(FactDiagnostic("coordinates", "coordinate count differs from selected mode/rank"))
        for value in context.coordinates:
            if not isinstance(value, ConvertedInteger) or not _checked_int(value, 32, True):
                problems.append(FactDiagnostic("coordinates", "S32 use value disagrees with original source bits"))
                break
    if context.info is not None and not isinstance(context.info, tuple):
        problems.append(FactDiagnostic("info", "information values must be a tuple"))
    elif context.info is not None:
        if not isinstance(context.mode, TensorAccessMode) or context.mode not in _IM2COL or not isinstance(context.rank, int) or context.rank not in (3, 4, 5):
            problems.append(FactDiagnostic("info", "selected mode/rank has no information operands"))
        elif len(context.info) != len(tensor_im2col_info_contract(context.mode, context.rank)):
            problems.append(FactDiagnostic("info", "information count differs from selected mode/rank"))
        for value in context.info:
            if not isinstance(value, ConvertedInteger) or not _checked_int(value, 16, False):
                problems.append(FactDiagnostic("info", "U16 use value disagrees with original source bits"))
                break
    if isinstance(context.rank, int) and context.rank in (3, 4, 5):
        for name in ("im2col_spatial_box_elements", "im2col_lower_edge_offsets", "im2col_upper_edge_offsets"):
            entries = getattr(facts, name)
            if isinstance(entries, tuple) and len(entries) != context.rank - 2:
                problems.append(FactDiagnostic(name, "spatial array arity must be selected rank minus two"))
    for name in ("map_object_address_bytes", "data_base_address_bytes", "accessed_box_address_bytes", "shared_destination_address_bytes", "inner_box_bytes", "inner_tensor_bytes", "accessed_box_bytes"):
        value = getattr(facts, name)
        if value is not None and (not isinstance(value, int) or isinstance(value, bool) or value < 0 or value > (1 << 64) - 1):
            problems.append(FactDiagnostic(name, "byte value must fit an unsigned 64-bit fact"))
    for name in ("w_box_elements", "pixels_per_column_elements",
                 "channels_per_pixel_elements", "available_ndhw_pixels"):
        value = getattr(facts, name)
        if value is not None and (not isinstance(value, int) or isinstance(value, bool)
                                  or not 0 <= value <= (1 << 64) - 1):
            problems.append(FactDiagnostic(name, "element count must fit an unsigned 64-bit fact"))
    for name in ("w_lower_edge_offset", "w_upper_edge_offset"):
        value = getattr(facts, name)
        if value is not None and (not isinstance(value, int) or isinstance(value, bool)
                                  or not -(1 << 31) <= value < (1 << 31)):
            problems.append(FactDiagnostic(name, "opposite-edge offset must fit a signed 32-bit fact"))
    if context.active_atomicity_use is True and facts.swizzle is not None and _field(facts.swizzle) == "none":
        problems.append(FactDiagnostic("active_atomicity_use", "active use contradicts a no-swizzle claim"))
    return tuple(problems)


def _r1(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.ELEMENT_IDENTITY
    if f.element is None or f.element.valid is None:
        return _missing(rule, "existing Table 33 field projection or element fact absent", "element")
    if f.element.valid is False:
        return _missing(rule, "malformed encoded field; see diagnostics", "element")
    if f.element.code == 15 and f.element.identity is None:
        return _missing(rule, "code 15 directional interpretation absent", "element")
    if f.element.code == 15 and c.direction in (TensorDirection.PREFETCH, TensorDirection.LOAD) and f.element.identity != "b6x16_p32":
        return _bad(rule, "code 15 load interpretation is b6x16_p32", "element", "direction")
    if f.element.code == 15 and c.direction in (TensorDirection.STORE, TensorDirection.REDUCE) and f.element.identity != "b6p2x16":
        return _bad(rule, "code 15 store interpretation is b6p2x16", "element", "direction")
    return _ok(rule, "accepted field projection and direction agree", "element", "direction")


def _element(c: TensorAccessContext, f: TensorMapKnownFacts) -> str | None:
    """Use a code-15 subtype only when its claim agrees with selected direction."""

    if f.element is None or f.element.valid is not True:
        return None
    if f.element.code == 15:
        expected = ("b6x16_p32" if c.direction in
                    (TensorDirection.LOAD, TensorDirection.PREFETCH)
                    else "b6p2x16")
        return f.element.identity if f.element.identity == expected else None
    return f.element.identity


def _packed_family(c: TensorAccessContext, f: TensorMapKnownFacts) -> bool:
    """Know a sub-byte family without selecting a code-15 directional subtype."""

    if f.element is None or f.element.valid is not True:
        return False
    return (_element(c, f) in _PACKED or
            (f.element.code == 15 and f.element.identity in
             (None, "b6x16_p32", "b6p2x16")))


def _r2(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.REDUCTION_TYPE
    if c.direction is not TensorDirection.REDUCE:
        return _na(rule, "not a tensor reduction")
    if c.reduction_op is None or f.reduction_interpretation is None:
        return _missing(rule, "selected operation or independent reduction interpretation absent", "reduction_op", "reduction_interpretation")
    if c.mode not in (TensorAccessMode.TILED, TensorAccessMode.IM2COL_NO_OFFS) or (c.mode is TensorAccessMode.IM2COL_NO_OFFS and c.rank < 3):
        return _bad(rule, "unsupported tensor-reduction mode/rank", "mode", "rank")
    if f.reduction_interpretation not in TENSOR_REDUCTION_ELEMENT_TYPES[c.reduction_op]:
        return _bad(rule, "accepted operation/type matrix rejects interpretation", "reduction_op", "reduction_interpretation")
    element = _element(c, f)
    if element is None:
        return _missing(rule, "operation/type membership checked; descriptor format unknown", "element")
    if element in ("f32.ftz", "tf32", "tf32.ftz") or f.reduction_interpretation in ("b32", "b64"):
        return _missing(rule, "no sourced descriptor/reduction coercion for FTZ, TF32, or bit types", "element", "reduction_interpretation")
    if element != f.reduction_interpretation:
        return _bad(rule, "exact named descriptor/reduction identities differ", "element", "reduction_interpretation")
    return _ok(rule, "accepted operation/type query and exact named identity agree", "element", "reduction_interpretation")


def _r3(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.BASE_BOX
    if c.mode not in _TILED:
        return _na(rule, "generic tiled box rule not applicable")
    if f.accessed_box_bytes is not None and f.accessed_box_bytes % 16:
        return _bad(rule, "accessed box byte size is not a multiple of 16", "accessed_box_bytes")
    if f.accessed_box_address_bytes is not None and f.accessed_box_address_bytes % 16:
        return _bad(rule, "accessed box address is not 16-byte aligned", "accessed_box_address_bytes")
    if f.accessed_box_bytes is None or f.accessed_box_address_bytes is None:
        return _missing(rule, "box bytes or accessed address unknown; no dimension-to-byte inference", "accessed_box_bytes", "accessed_box_address_bytes")
    if f.accessed_box_bytes == 0:
        return _missing(rule, "zero box bytes have no sourced nonempty-transfer legality", "accessed_box_bytes")
    return _ok(rule, "supplied box byte size and accessed address satisfy 16-byte rules")


def _r4(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.TILED_TRAVERSAL
    if c.mode not in _TILED:
        return _na(rule, "not tiled traversal")
    interleave = _field(f.interleave)
    if interleave is None or f.traversal_steps_elements is None or not f.traversal_steps_elements:
        return _missing(rule, "interleave or element traversal steps absent", "interleave", "traversal_steps_elements")
    if interleave == "none" and f.traversal_steps_elements[0] is not None:
        if f.traversal_steps_elements[0] != 1:
            return _bad(rule, "noninterleaved dimension-zero element step must be one", "traversal_steps_elements")
        return _ok(rule, "noninterleaved dimension-zero step is one")
    return _missing(rule, "no sourced general numeric traversal predicate for this layout", "traversal_steps_elements")


def _r5(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.FILL_PACKED
    element, fill = _element(c, f), _field(f.fill)
    if (element is None and not _packed_family(c, f)) or fill is None:
        return _missing(rule, "format or fill identity unknown", "element", "fill")
    if _packed_family(c, f) and fill == "oob_nan":
        return _bad(rule, "OOB-NaN fill excludes sub-byte formats", "element", "fill")
    if _packed_family(c, f):
        return _ok(rule, "packed format does not use OOB-NaN fill")
    return _missing(rule, "no complete non-packed fill/type compatibility matrix is sourced", "element", "fill")


def _r6(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SUBBYTE_OPERATION
    element = _element(c, f)
    if element is None and not _packed_family(c, f):
        return _missing(rule, "element identity unknown", "element")
    if not _packed_family(c, f):
        return _na(rule, "not a sub-byte format")
    if c.direction is TensorDirection.REDUCE:
        return _bad(rule, "tensor reduction excludes sub-byte formats", "direction", "element")
    if element is None:
        return _missing(rule, "code-15 direction-specific subtype absent", "element")
    if c.direction is TensorDirection.STORE and element == "b4x16_p64":
        return _bad(rule, "tensor store excludes b4x16_p64", "direction", "element")
    if c.direction is TensorDirection.LOAD and c.destination is TensorDestination.CLUSTER and c.target_identity == "sm_120a":
        return _bad(rule, "exact sm_120a cluster load excludes sub-byte formats", "target_identity", "element")
    return _ok(rule, "no selected sub-byte direction exclusion applies")


def _is_override(c: TensorAccessContext, f: TensorMapKnownFacts) -> bool:
    """Select only the exact sourced profile exception before generic packed rules."""

    return (c.target_identity == "sm_103a" and c.direction is TensorDirection.STORE
            and _element(c, f) == "b6p2x16")


def _packed_geometry(c: TensorAccessContext, f: TensorMapKnownFacts,
                     rule: TensorFactRule, *, special: bool) -> FactOutcome:
    """Evaluate supplied byte facts without a packed-width conversion guess."""

    element = _element(c, f)
    if element is None:
        return _missing(rule, "existing element projection absent", "element")
    if element not in _PACKED_RESTRICTED:
        return _na(rule, "no packed geometry clause for this format")
    required_box = 48 if special else (64 if element == "b4x16_p64" else 96)
    box_choices = (48, 96) if special else (required_box,)
    tensor_multiple = required_box
    address_multiple = 16 if special else 32
    coordinate_multiple = 64 if special else 128
    if f.inner_box_bytes is not None and f.inner_box_bytes not in box_choices:
        return _bad(rule, "packed Box-Size[0] byte value conflicts with selected profile", "inner_box_bytes")
    if f.inner_tensor_bytes is not None and f.inner_tensor_bytes % tensor_multiple:
        return _bad(rule, "packed Tensor-Size[0] is not a required byte multiple", "inner_tensor_bytes")
    if f.data_base_address_bytes is not None and f.data_base_address_bytes % address_multiple:
        return _bad(rule, "tensor data base lacks selected byte alignment", "data_base_address_bytes")
    if f.tensor_strides_bytes is not None and any(value is not None and value % address_multiple for value in f.tensor_strides_bytes):
        return _bad(rule, "supplied tensor byte stride lacks selected alignment", "tensor_strides_bytes")
    if c.coordinates and c.coordinates[0].value is not None and c.coordinates[0].value % coordinate_multiple:
        return _bad(rule, "converted first coordinate lacks selected packed multiple", "coordinates")
    if (f.inner_box_bytes is None or f.inner_tensor_bytes is None or
            f.data_base_address_bytes is None or f.tensor_strides_bytes is None or
            not c.coordinates or c.coordinates[0].value is None or
            any(value is None for value in f.tensor_strides_bytes)):
        return _missing(rule, "one or more independently supplied packed byte/coordinate facts absent", "inner_box_bytes", "inner_tensor_bytes", "data_base_address_bytes", "tensor_strides_bytes", "coordinates")
    if f.inner_tensor_bytes == 0 or any(value == 0 for value in f.tensor_strides_bytes):
        return _missing(rule, "zero tensor extent or stride has no sourced transfer-legality rule", "inner_tensor_bytes", "tensor_strides_bytes")
    return _ok(rule, "all explicitly supplied packed geometry facts satisfy selected profile")


def _r7(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.PACKED_GEOMETRY
    if _is_override(c, f):
        return _na(rule, "exact sm103a B6p2 store rule replaces generic packed geometry")
    return _packed_geometry(c, f, rule, special=False)


def _r8(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.PACKED_SWIZZLE
    if _is_override(c, f):
        return _na(rule, "exact sm103a B6p2 store rule replaces generic packed swizzle")
    if _element(c, f) is None:
        return _missing(rule, "existing element projection absent", "element")
    if _element(c, f) not in _PACKED_RESTRICTED:
        return _na(rule, "no generic packed swizzle clause")
    swizzle = _field(f.swizzle)
    if swizzle is None:
        return _missing(rule, "swizzle identity unknown", "swizzle")
    if swizzle not in ("none", "bytes128"):
        return _bad(rule, "packed format allows only none or 128-byte swizzle", "swizzle")
    if swizzle == "bytes128":
        atomicity = _field(f.atomicity)
        if atomicity is None:
            return _missing(rule, "128-byte packed atomicity unknown", "atomicity")
        if atomicity == "bytes32_flip8":
            return _bad(rule, "packed format excludes 32-byte plus 8-byte flip", "atomicity")
    return _ok(rule, "packed swizzle choice satisfies generic clause")


def _r9(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.INTERLEAVE
    interleave = _field(f.interleave)
    if interleave is None:
        return _missing(rule, "interleave identity unknown", "interleave")
    if interleave == "none":
        return _ok(rule, "no interleave selected")
    if c.rank < 3 or c.mode in (_GATHER | _W):
        return _bad(rule, "interleave excludes rank below three, gather/scatter and W modes", "interleave", "rank", "mode")
    return _missing(rule, "selected interleave is representable; channel-slice conversion is not sourced", "channels_per_pixel_elements", "element")


def _r10(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SWIZZLE_ATOMICITY
    swizzle = _field(f.swizzle)
    if swizzle is None:
        return _missing(rule, "swizzle identity unknown", "swizzle")
    if swizzle == "none":
        return _na(rule, "Table 14 atomicity is not applicable without swizzle")
    if c.active_atomicity_use is None:
        return _missing(rule, "raw atomicity code does not prove selected active use", "active_atomicity_use")
    if c.active_atomicity_use is False:
        return _na(rule, "caller says no selected atomicity use")
    atomicity = _field(f.atomicity)
    if atomicity is None:
        return _missing(rule, "active atomicity identity unknown", "atomicity")
    if swizzle in ("bytes32", "bytes64", "bytes96") and atomicity != "bytes16":
        return _bad(rule, "selected swizzle requires 16-byte atomicity", "swizzle", "atomicity")
    return _ok(rule, "selected swizzle/atomicity pair satisfies Table 14")


def _r11(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SWIZZLE_ALIGNMENT
    if _field(f.swizzle) is None:
        return _missing(rule, "swizzle identity unknown", "swizzle")
    if _field(f.swizzle) != "bytes128":
        return _na(rule, "extra atomicity alignment applies only to 128-byte swizzle")
    if c.active_atomicity_use is not True:
        return _missing(rule, "selected atomicity use not established", "active_atomicity_use")
    atomicity = _field(f.atomicity)
    if atomicity not in ("bytes32", "bytes64"):
        return _na(rule, "no extra alignment is sourced for this atomicity") if atomicity is not None else _missing(rule, "atomicity unknown", "atomicity")
    alignment = 32 if atomicity == "bytes32" else 64
    address = f.shared_destination_address_bytes
    if address is None:
        return _missing(rule, "shared destination numeric address unknown", "shared_destination_address_bytes")
    if address % alignment:
        return _bad(rule, "shared destination lacks selected atomicity alignment", "shared_destination_address_bytes")
    return _ok(rule, "shared destination meets selected atomicity alignment")


def _r12(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.PATTERN_BASE_OFFSET
    swizzle = _field(f.swizzle)
    modulus = {"bytes32": 2, "bytes64": 4, "bytes96": 2, "bytes128": 8}.get(swizzle)
    if swizzle == "none":
        return _na(rule, "no repeating swizzle pattern")
    if modulus is None:
        return _missing(rule, "swizzle identity unknown", "swizzle")
    if f.shared_destination_address_bytes is None:
        return _missing(rule, "shared destination numeric address unknown", "shared_destination_address_bytes")
    offset = (f.shared_destination_address_bytes // 128) % modulus
    return _ok(rule, "repeating-pattern base offset computed, not an alignment gate", "shared_destination_address_bytes", derived_value=offset)


def _r13(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SWIZZLE_96
    if _field(f.swizzle) is None:
        return _missing(rule, "swizzle identity unknown", "swizzle")
    if _field(f.swizzle) != "bytes96":
        return _na(rule, "not a 96-byte swizzle")
    if c.mode is TensorAccessMode.IM2COL_W128 or _element(c, f) in _PACKED_RESTRICTED:
        return _bad(rule, "96-byte swizzle excludes W128 and packed profiles", "mode", "element")
    if _field(f.interleave) not in (None, "none"):
        return _bad(rule, "96-byte swizzle excludes interleave", "interleave")
    if _field(f.atomicity) not in (None, "bytes16") and c.active_atomicity_use is True:
        return _bad(rule, "96-byte swizzle requires 16-byte atomicity", "atomicity")
    if f.inner_box_bytes is not None and f.inner_box_bytes > 96:
        return _bad(rule, "96-byte swizzle box exceeds 96 bytes", "inner_box_bytes")
    if _field(f.interleave) is None or f.inner_box_bytes is None or _element(c, f) is None or c.active_atomicity_use is None or (c.active_atomicity_use and _field(f.atomicity) is None):
        return _missing(rule, "interleave, box bytes, element or selected atomicity fact absent", "interleave", "inner_box_bytes", "element", "active_atomicity_use", "atomicity")
    return _ok(rule, "known 96-byte swizzle conditions satisfied")


def _r14(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.FLIP_8
    if _field(f.swizzle) is None or _field(f.atomicity) is None:
        return _missing(rule, "swizzle or atomicity identity unknown", "swizzle", "atomicity")
    if _field(f.swizzle) != "bytes128" or _field(f.atomicity) != "bytes32_flip8":
        return _na(rule, "not 128-byte swizzle with 8-byte flip")
    if c.active_atomicity_use is None:
        return _missing(rule, "raw atomicity code does not prove active selected use", "active_atomicity_use")
    if c.active_atomicity_use is False:
        return _na(rule, "selected atomicity use absent")
    if c.direction is not TensorDirection.LOAD or c.mode in _W:
        return _bad(rule, "8-byte flip requires a load and excludes W modes", "direction", "mode")
    return _ok(rule, "selected load allows 128-byte 8-byte flip")


def _r15(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SM103A_PACKED_STORE
    if c.target_identity is None or _element(c, f) is None:
        return _missing(rule, "exact target or encoded element identity unknown", "target_identity", "element")
    if not _is_override(c, f):
        return _na(rule, "not exact sm103a B6p2x16 tensor store")
    geometry = _packed_geometry(c, f, rule, special=True)
    if geometry.status is FactStatus.VIOLATED:
        return geometry
    swizzle, atomicity = _field(f.swizzle), _field(f.atomicity)
    if swizzle is not None and swizzle not in ("none", "bytes64", "bytes128"):
        return _bad(rule, "exact packed store excludes this swizzle", "swizzle")
    if swizzle == "bytes64" and atomicity not in (None, "bytes16") and c.active_atomicity_use is True:
        return _bad(rule, "64-byte override swizzle requires 16-byte atomicity", "atomicity")
    if swizzle == "bytes128" and atomicity == "bytes32_flip8" and c.active_atomicity_use is True:
        return _bad(rule, "override excludes 128-byte 8-byte flip", "atomicity")
    if geometry.status is FactStatus.UNRESOLVED or swizzle is None or c.active_atomicity_use is None or (swizzle != "none" and c.active_atomicity_use and atomicity is None):
        return _missing(rule, "override byte geometry or selected swizzle/atomicity fact absent", "inner_box_bytes", "inner_tensor_bytes", "data_base_address_bytes", "tensor_strides_bytes", "coordinates", "swizzle", "active_atomicity_use")
    return _ok(rule, "exact sm103a store override satisfied without generic packed conjunction")


def _r16(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SM120A_CLUSTER
    if c.target_identity is None:
        return _missing(rule, "exact selected target identity unknown", "target_identity")
    if c.target_identity != "sm_120a" or c.direction is not TensorDirection.LOAD or c.destination is not TensorDestination.CLUSTER:
        return _na(rule, "not exact sm120a cluster tensor load")
    if _packed_family(c, f):
        return _bad(rule, "exact target excludes sub-byte cluster loads", "element")
    if c.active_atomicity_use is True:
        return _bad(rule, "exact target excludes applied swizzle atomicity", "active_atomicity_use")
    if _element(c, f) is None or c.active_atomicity_use is None:
        return _missing(rule, "element or selected-use provenance unknown; raw code alone is insufficient", "element", "active_atomicity_use")
    return _ok(rule, "known exact-target exclusions absent")


def _r17(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.STORE_BOUNDS
    if c.direction not in (TensorDirection.STORE, TensorDirection.REDUCE):
        return _na(rule, "not a tensor write")
    if c.coordinates and any(value.value is not None and value.value < 0 for value in c.coordinates):
        return _bad(rule, "converted S32 tensor-write coordinate is negative", "coordinates")
    if f.im2col_lower_edge_offsets and any(value is not None and value < 0 for value in f.im2col_lower_edge_offsets):
        return _bad(rule, "store lower-edge offset must be nonnegative", "im2col_lower_edge_offsets")
    if f.im2col_upper_edge_offsets and any(value is not None and value > 0 for value in f.im2col_upper_edge_offsets):
        return _bad(rule, "store upper-edge offset must be nonpositive", "im2col_upper_edge_offsets")
    if c.coordinates is None or any(value.value is None for value in c.coordinates):
        return _missing(rule, "runtime coordinate value unknown", "coordinates")
    if c.mode is TensorAccessMode.IM2COL_NO_OFFS and (f.im2col_lower_edge_offsets is None or f.im2col_upper_edge_offsets is None):
        return _missing(rule, "im2col store corner offset facts absent", "im2col_lower_edge_offsets", "im2col_upper_edge_offsets")
    return _missing(rule, "box-within-tensor relation lacks sourced opposite-edge transform or full bounds", "full_dimensions_elements", "tiled_box_elements")


def _r18(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.IM2COL_SHAPE
    if c.mode not in _IM2COL and c.mode is not TensorAccessMode.IM2COL_NO_OFFS:
        return _na(rule, "not im2col geometry")
    if c.rank not in (3, 4, 5):
        return _bad(rule, "im2col requires rank three through five", "rank")
    expected = c.rank - 2
    if f.im2col_spatial_box_elements is not None and len(f.im2col_spatial_box_elements) != expected:
        return _bad(rule, "spatial box rank differs from tensor rank minus two", "im2col_spatial_box_elements")
    limit = {3: 1 << 15, 4: 1 << 7, 5: 1 << 4}[c.rank]
    for name in ("im2col_lower_edge_offsets", "im2col_upper_edge_offsets"):
        entries = getattr(f, name)
        if entries is not None and any(value is not None and not -limit <= value < limit for value in entries):
            return _bad(rule, "opposite-edge offset outside rank-specific signed range", name)
    if f.im2col_spatial_box_elements is None or f.im2col_lower_edge_offsets is None or f.im2col_upper_edge_offsets is None:
        return _missing(rule, "spatial box or opposite-edge offsets absent", "im2col_spatial_box_elements", "im2col_lower_edge_offsets", "im2col_upper_edge_offsets")
    return _missing(rule, "filter-base-to-box relation lacks selected spatial coordinate provenance", "coordinates", "im2col_spatial_box_elements")


def _r19(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.IM2COL_INFO
    if c.mode is not TensorAccessMode.IM2COL:
        return _na(rule, "not general im2col information")
    if c.rank not in (3, 4, 5):
        return _bad(rule, "general im2col requires rank three through five", "rank")
    if c.info is None:
        return _missing(rule, "optional information omitted; execution meaning remains unresolved", "info")
    contract = tensor_im2col_info_contract(c.mode, c.rank)
    if len(c.info) != len(contract):
        return _bad(rule, "information count differs from rank minus two", "info")
    for value, (_, maximum) in zip(c.info, contract, strict=True):
        if value.value is not None and not 0 <= value.value <= maximum:
            return _bad(rule, "converted U16 offset exceeds rank-specific bound", "info")
    if any(value.value is None for value in c.info):
        return _missing(rule, "one or more runtime U16 offsets unknown", "info")
    if f.pixels_per_column_elements is not None and f.available_ndhw_pixels is not None and f.pixels_per_column_elements > f.available_ndhw_pixels and _field(f.fill) is None:
        return _missing(rule, "requested pixels exceed available count but fill identity unknown", "pixels_per_column_elements", "available_ndhw_pixels", "fill")
    return _missing(rule, "pixel-count/traversal/OOB relationship is not fully sourced for caller facts", "pixels_per_column_elements", "available_ndhw_pixels", "traversal_steps_elements")


def _r20(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.W_PROFILE
    if c.mode not in _W:
        return _na(rule, "not a W profile")
    if c.rank not in (3, 4, 5):
        return _bad(rule, "W profile requires rank three through five", "rank")
    if _field(f.interleave) not in (None, "none"):
        return _bad(rule, "W profile excludes interleave", "interleave")
    if _field(f.swizzle) == "none":
        return _bad(rule, "W profile requires swizzle", "swizzle")
    if _field(f.swizzle) == "bytes128" and _field(f.atomicity) == "bytes32_flip8" and c.active_atomicity_use is True:
        return _bad(rule, "W profile excludes 128-byte 8-byte flip", "swizzle", "atomicity")
    if f.im2col_spatial_box_elements and any(value is not None and value != 1 for value in f.im2col_spatial_box_elements[:-1]):
        return _bad(rule, "W profile fixes D/H spatial box extents to one", "im2col_spatial_box_elements")
    if c.info is None:
        return _missing(rule, "optional W information omitted; execution meaning unresolved", "info")
    contract = tensor_im2col_info_contract(c.mode, c.rank)
    if len(c.info) != len(contract):
        return _bad(rule, "W information requires halo and offset", "info")
    for value, (_, maximum) in zip(c.info, contract, strict=True):
        if value.value is not None and not 0 <= value.value <= maximum:
            return _bad(rule, "converted W halo/offset exceeds mode bound", "info")
    if f.im2col_spatial_box_elements is None or _field(f.interleave) is None or _field(f.swizzle) is None or any(value.value is None for value in c.info):
        return _missing(rule, "W box/layout or runtime information unknown", "im2col_spatial_box_elements", "interleave", "swizzle", "info")
    return _missing(rule, "W left/right boundary relation lacks sourced width and opposite-edge provenance", "w_box_elements", "w_lower_edge_offset", "w_upper_edge_offset", "coordinates")


def _r21(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.GATHER_SCATTER
    if c.mode not in _GATHER:
        return _na(rule, "not gather4/scatter4")
    if c.rank != 2 or (c.coordinates is not None and len(c.coordinates) != 5):
        return _bad(rule, "four-row mode requires rank two and five coordinate roles", "rank", "coordinates")
    if _field(f.interleave) not in (None, "none"):
        return _bad(rule, "four-row mode excludes interleave", "interleave")
    if f.tiled_box_elements is not None and len(f.tiled_box_elements) > 1 and f.tiled_box_elements[1] is not None and f.tiled_box_elements[1] != 1:
        return _bad(rule, "four-row box dimension one must equal one", "tiled_box_elements")
    if c.coordinates is None or f.tiled_box_elements is None or len(f.tiled_box_elements) < 2 or f.tiled_box_elements[1] is None or _field(f.interleave) is None:
        return _missing(rule, "row coordinates, box height or interleave fact unknown", "coordinates", "tiled_box_elements", "interleave")
    return _missing(rule, "row boundary relation lacks independently known per-row extents", "full_dimensions_elements", "coordinates")


def _r22(c: TensorAccessContext, f: TensorMapKnownFacts) -> FactOutcome:
    rule = TensorFactRule.SELECTED_AVAILABILITY
    if c.selected_variant_available is False or c.selected_value_available is False:
        return _bad(rule, "accepted selected-variant/value query rejects this target", "selected_variant_available", "selected_value_available")
    if c.selected_variant_available is None or c.selected_value_available is None:
        return _missing(rule, "canonical selected-variant/value availability query absent", "selected_variant_available", "selected_value_available")
    return _ok(rule, "canonical selected availability queries accepted this target")


_EVALUATORS: dict[TensorFactRule, Callable[[TensorAccessContext, TensorMapKnownFacts], FactOutcome]] = {
    rule: globals()[f"_r{rule.value}"] for rule in TensorFactRule
}


def validate_tensor_access_facts(context: TensorAccessContext,
                                 facts: TensorMapKnownFacts) -> TensorFactsReport:
    """Evaluate each sourced relation against independent caller-known values.

    Malformed supplied context/facts yield diagnostics and unresolved dependent
    outcomes, never an unchecked lookup.  A Checked result attests only one
    relationship among caller claims, not an actual descriptor or GPU access.
    """

    diagnostics = _validity(context, facts)
    if diagnostics:
        damaged = {entry.field.removeprefix("facts.") for entry in diagnostics}
        if "facts.rank" in {entry.field for entry in diagnostics}:
            damaged.add("rank")
        outcomes = tuple(
            _missing(rule, "malformed supplied metadata; see diagnostics", *sorted(damaged))
            if (set(RULE_CATALOG[rule].inputs) & damaged or
                {"rank", "direction", "mode"} & damaged)
            else _EVALUATORS[rule](context, facts)
            for rule in TensorFactRule
        )
        return TensorFactsReport(outcomes, diagnostics)
    return TensorFactsReport(tuple(_EVALUATORS[rule](context, facts)
                                   for rule in TensorFactRule))
