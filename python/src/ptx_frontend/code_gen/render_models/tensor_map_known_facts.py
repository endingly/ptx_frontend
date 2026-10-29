"""Typed public metadata and selected tensor projections for fixed templates."""

from dataclasses import dataclass

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.cpp_literals import cpp_string
from ptx_frontend.code_gen.cpp_names import form_name
from ptx_frontend.code_gen.resolved_layout import operand_slot_for_field, operand_slots
from ptx_frontend.ir.resolved_ir import (
    ResolvedOperandAccess, ResolvedValueKind, TensorAccessMode,
    tensor_im2col_info_contract,
)
from ptx_frontend.spec.tensor_map_known_facts import RULE_CATALOG, TensorFactRule


_CPP_TENSOR_MODE = {
    TensorAccessMode.TILED: "Tiled",
    TensorAccessMode.IM2COL_NO_OFFS: "Im2colNoOffs",
    TensorAccessMode.IM2COL: "Im2col",
    TensorAccessMode.IM2COL_W: "Im2colW",
    TensorAccessMode.IM2COL_W128: "Im2colW128",
    TensorAccessMode.TILE_GATHER4: "TileGather4",
    TensorAccessMode.TILE_SCATTER4: "TileScatter4",
}


@dataclass(frozen=True)
class FactRuleModel:
    """Stable public rule identity and precomputed dependency mask."""

    name: str
    value: int
    title_literal: str
    source_literal: str
    input_mask: int


@dataclass(frozen=True)
class FactInputModel:
    """One dependency's C++ literal and stable metadata bit."""

    name_literal: str
    bit: int


@dataclass(frozen=True)
class InfoBoundsModel:
    """Selected mode/rank profile with three padded U16 maxima."""

    mode: str
    rank: int
    arity: int
    maxima: tuple[int, ...]


@dataclass(frozen=True)
class TensorKnownFactRulesModel:
    """Public metadata arrays in accepted catalogue and profile order."""

    inputs: tuple[FactInputModel, ...]
    rules: tuple[FactRuleModel, ...]
    bounds: tuple[InfoBoundsModel, ...]


@dataclass(frozen=True)
class TensorKnownFactQueryModel:
    """The known-fact evaluator has fixed algorithms and no dynamic source facts."""


@dataclass(frozen=True)
class SelectedMetaModel:
    """Normalized tensor classification for one exact Cp descriptor row."""

    is_tensor: bool
    direction: str
    mode: str
    rank_expression: str
    destination_expression: str
    group_applicable: bool


@dataclass(frozen=True)
class ProjectionLayoutModel:
    """Selected member expressions and optional-storage consistency predicates."""

    index: int
    tensor_expression: str
    info_expression: str
    guards: tuple[str, ...]


@dataclass(frozen=True)
class SelectedProjectionModel:
    """One exact class's owned layout projections and supported optional facts."""

    name: str
    index: int
    layouts: tuple[ProjectionLayoutModel, ...]
    reduction: bool
    group_applicable: bool


@dataclass(frozen=True)
class TensorKnownSelectedAdapterModel:
    """Metadata row order and tensor-only selector/projection cases."""

    rows: tuple[SelectedMetaModel, ...]
    projections: tuple[SelectedProjectionModel, ...]


@dataclass(frozen=True)
class TensorKnownFactSourceModel:
    """Compose the fixed evaluator with the owned selected-form adapter."""

    query: TensorKnownFactQueryModel
    adapter: TensorKnownSelectedAdapterModel


def build_tensor_known_fact_source(context: GenerationContext) -> TensorKnownFactSourceModel:
    """Build both typed source sections for static template composition."""

    return TensorKnownFactSourceModel(
        TensorKnownFactQueryModel(), build_tensor_known_selected_adapter(context),
    )


def build_tensor_map_known_fact_rules() -> TensorKnownFactRulesModel:
    """Resolve stable dependency bits and existing accepted U16 profiles."""

    inputs = sorted({name for entry in RULE_CATALOG.values() for name in entry.inputs})
    if len(inputs) > 64:
        raise ValueError("known-fact dependency metadata exceeds uint64_t")
    input_bits = {name: 1 << index for index, name in enumerate(inputs)}
    rules = []
    for rule in TensorFactRule:
        entry = RULE_CATALOG[rule]
        mask = 0
        for name in entry.inputs:
            mask |= input_bits[name]
        rules.append(FactRuleModel(rule.name, rule.value, cpp_string(entry.title),
                                   cpp_string(entry.source_section), mask))
    bounds = []
    for mode in (TensorAccessMode.IM2COL, TensorAccessMode.IM2COL_W,
                 TensorAccessMode.IM2COL_W128):
        for rank in (3, 4, 5):
            maxima = tuple(maximum for _, maximum in tensor_im2col_info_contract(mode, rank))
            bounds.append(InfoBoundsModel(_CPP_TENSOR_MODE[mode], rank, len(maxima),
                                          maxima + (0,) * (3 - len(maxima))))
    return TensorKnownFactRulesModel(
        tuple(FactInputModel(cpp_string(name), input_bits[name]) for name in inputs),
        tuple(rules), tuple(bounds),
    )


def build_tensor_known_selected_adapter(context: GenerationContext) -> TensorKnownSelectedAdapterModel:
    """Validate normalized Cp topology and resolve direct member projections."""

    copies = [entry for entry in context.entries if entry.specification.opcode == "cp"]
    if len(copies) != 1:
        raise ValueError("known-fact adapter requires one normalized Cp model")
    entry = copies[0]
    rows = []
    projections = []
    for index, variant in enumerate(entry.resolved.variants):
        if variant.tensor_access_mode is None:
            rows.append(SelectedMetaModel(False, "Load", "Tiled", "TensorRank::One",
                                          "std::nullopt", False))
            continue
        ranks = {binding.expected_tensor_rank
                 for layout in variant.operand_layouts for binding in layout.bindings
                 if binding.tensor_access_mode is not None}
        if len(ranks) != 1 or None in ranks:
            raise ValueError("tensor variant lacks one normalized rank")
        rank = next(iter(ranks))
        if variant.tensor_reduction_op is not None:
            direction = "Reduce"
        elif any(field.value_kind is ResolvedValueKind.ADDRESS and
                 field.operand_access is ResolvedOperandAccess.WRITE
                 for layout in variant.operand_layouts for field in layout.fields):
            direction = "Load"
        elif any(field.value_kind is ResolvedValueKind.ADDRESS and
                 field.operand_access is ResolvedOperandAccess.READ
                 for layout in variant.operand_layouts for field in layout.fields):
            direction = "Store"
        else:
            direction = "Prefetch"
        destination = variant.tensor_destination
        if (direction == "Load") != (destination is not None):
            raise ValueError("normalized load/destination topology disagrees")
        if variant.modifier_value_availabilities:
            raise ValueError("tensor modifier-value views need explicit adapter support")
        dest_cpp = ("TensorKnownFactDestination::" + destination.name.title()
                    if destination is not None else "std::nullopt")
        rows.append(SelectedMetaModel(
            True, direction, _CPP_TENSOR_MODE[variant.tensor_access_mode],
            f"static_cast<TensorRank>({rank})", dest_cpp,
            variant.tensor_cta_group_applicable,
        ))
        slots = operand_slots(variant, context.backend)
        layouts = []
        for layout_index, layout in enumerate(variant.operand_layouts):
            tensor_fields = [field for field in layout.fields
                             if field.value_kind is ResolvedValueKind.TENSOR_OPERAND]
            if len(tensor_fields) != 1:
                raise ValueError(f"{variant.variant_id}: missing tensor payload")
            tensor_slot = operand_slot_for_field(slots, tensor_fields[0], context.backend)
            tensor_expr = f"selected->{tensor_slot.member_name}"
            if tensor_slot.optional:
                tensor_expr = f"(*{tensor_expr})"
            info_fields = [field for field in layout.fields
                           if field.value_kind is ResolvedValueKind.TENSOR_IM2COL_INFO]
            if len(info_fields) > 1:
                raise ValueError(f"{variant.variant_id}: duplicate im2col information")
            info_expr = "nullptr"
            if info_fields:
                info_slot = operand_slot_for_field(slots, info_fields[0], context.backend)
                info_expr = f"&selected->{info_slot.member_name}.value"
                if info_slot.optional:
                    info_expr = f"&selected->{info_slot.member_name}->value"
            guards = tuple(
                f"selected->{slot.member_name}.has_value() != "
                f"{'true' if layout_index in slot.layout_indices else 'false'}"
                for slot in slots if slot.optional
            )
            layouts.append(ProjectionLayoutModel(layout_index, tensor_expr, info_expr, guards))
        projections.append(SelectedProjectionModel(
            form_name(entry, variant), index, tuple(layouts),
            variant.tensor_reduction_op is not None, variant.tensor_cta_group_applicable,
        ))
    return TensorKnownSelectedAdapterModel(tuple(rows), tuple(projections))
