"""Render final-form checker methods and ordered layout-specific checks.

Callers provide exact class and payload expressions; optional payload members
must be guarded before their expressions are evaluated in generated C++.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.code_gen.cpp_names import form_name
from ptx_frontend.code_gen.render_models.operand_views import (
    build_check_modifier_value_view,
    build_check_modifier_view,
    build_check_operand_view, ModifierViewModel, ModifierValueViewModel, OperandViewModel,
)
from ptx_frontend.code_gen.resolved_layout import (
    OperandSlot, operand_slot_for_field, operand_slots,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedField, ResolvedFieldOrigin, ResolvedInstruction,
    ResolvedOperandLayout, ResolvedValueKind, ResolvedVariant, TensorAccessMode,
)
from ptx_frontend.spec.model import (
    AsyncCompletionKind, CodegenUnit, FabricOperation, SemanticRule,
)



@dataclass(frozen=True)
class AppendResultModel:
    """Final result expression whose diagnostics append in evaluation order."""

    expression: str
    structure: Literal["append_result"] = "append_result"


@dataclass(frozen=True)
class PresenceGuardModel:
    """Payload-presence predicates in slot order, before optional dereferences."""

    condition: tuple[str, ...]
    structure: Literal["presence_guard"] = "presence_guard"


@dataclass(frozen=True)
class CheckLayoutModel:
    """One selected-layout branch, including guards and ordered nested checks."""

    layout_index: int
    checker_variant: str
    availability_check_result: AppendResultModel
    layout_modifier_check_result: AppendResultModel
    guard: PresenceGuardModel | None
    named_check: tuple[NamedArrayCheckModel, ...]
    operand_count: int
    views: tuple[OperandViewModel, ...]
    descriptor: str
    variant_index: int
    matrix: str
    operand_check_result: AppendResultModel
    mma_check: MmaSourceCheckModel | None
    tensor_checks: tuple[TensorCheckRow, ...] | None
    texture_check: TexturePayloadCheckModel | TextureQueryCheckModel | None
    surface_check: SurfaceCheckModel | None
    stack_check: StackCheckModel | None
    cross_checks: tuple[CrossRuleRow, ...]
    structure: Literal["check_layout"] = "check_layout"


@dataclass(frozen=True)
class NamedArrayCheckModel:
    """Borrowed address expression and preselected named-array acceptance policy."""

    pointer: str
    policy: str
    structure: Literal["named_array_check"] = "named_array_check"


@dataclass(frozen=True)
class SurfaceCheckModel:
    """Final nested surface-payload checker expression and diagnostic append."""

    expression: str
    surface_payload_check_result: AppendResultModel
    structure: Literal["surface_check"] = "surface_check"


@dataclass(frozen=True)
class StackCheckModel:
    """Borrowed owned-stack carriers and original alignment provenance."""

    form_cpp_name: str
    stack_token: str
    allocation_result: str
    size_operand: str
    align: str
    stack_payload_check_result: AppendResultModel
    structure: Literal["stack_check"] = "stack_check"


@dataclass(frozen=True)
class TexturePayloadCheckModel:
    """Final texture contract, operand expressions, and residency requirement."""

    name: str
    access_member: str
    result_member: str
    residency_required: str
    texture_payload_check_result: AppendResultModel
    structure: Literal["texture_payload_check"] = "texture_payload_check"


@dataclass(frozen=True)
class TextureQueryCheckModel:
    """Final texture-query contract and borrowed resource expression."""

    name: str
    resource_member: str
    texture_query_check_result: AppendResultModel
    structure: Literal["texture_query_check"] = "texture_query_check"


@dataclass(frozen=True)
class NamedRuleCheckModel:
    """One named checker call and its ordered diagnostic append."""

    label: str
    expression: str
    named_check_result: AppendResultModel
    structure: Literal["named_rule_check"] = "named_rule_check"


@dataclass(frozen=True)
class MmaSourceCheckModel:
    """Preselected direct MMA carriers and optional modifier references."""

    d_member: str
    address: str
    shared: str
    b_member: str
    idesc_member: str
    mask: str
    enable_input_d_member: str
    scale: str
    selector: str
    scale_a: str
    scale_b: str
    ashift: str
    collector: str
    metadata: str
    zero_column: str
    ws: str
    sparse: str
    mma_source_check_result: AppendResultModel
    structure: Literal["mma_source_check"] = "mma_source_check"


@dataclass(frozen=True)
class CheckModel:
    """Final form identity and ordered modifier views and layout branches."""

    name: str
    predicate_check_result: AppendResultModel
    modifier_count: int
    field_views: tuple[ModifierViewModel, ...]
    modifier_views: tuple[ModifierValueViewModel, ...]
    descriptor: str
    variant_name: str
    common_check_result: AppendResultModel
    variant_index: int
    domain_check_result: AppendResultModel
    modifier_availability_check_result: AppendResultModel
    group_check: NamedRuleCheckModel | None
    hint_check: NamedRuleCheckModel | None
    layout_count: int
    layout_check_result: AppendResultModel
    layout_cases: tuple[CheckLayoutModel, ...]
    structure: Literal["check"] = "check"


@dataclass(frozen=True)
class UnifiedAddressCheckModel:
    """Selected unified address check structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["unified_address_check"] = "unified_address_check"


@dataclass(frozen=True)
class AtomicQualifierCheckModel:
    """Selected atomic qualifier check structure with final descriptor references."""

    checker_variant_expr: str
    address_qualifier_expr: str
    structure: Literal["atomic_qualifier_check"] = "atomic_qualifier_check"


@dataclass(frozen=True)
class MemoryConsistencyCheckModel:
    """Selected memory consistency check structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["memory_consistency_check"] = "memory_consistency_check"


@dataclass(frozen=True)
class MemoryVectorCheckModel:
    """Selected memory vector check structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["memory_vector_check"] = "memory_vector_check"


@dataclass(frozen=True)
class AddressAlignmentLoopModel:
    """Selected address alignment loop structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["address_alignment_loop"] = "address_alignment_loop"


@dataclass(frozen=True)
class ImmediateValueCheckModel:
    """Selected immediate value check structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["immediate_value_check"] = "immediate_value_check"


@dataclass(frozen=True)
class ImmediateRangeLoopModel:
    """Selected immediate range loop structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["immediate_range_loop"] = "immediate_range_loop"


@dataclass(frozen=True)
class ImmediateMultipleCheckModel:
    """Selected immediate multiple check structure with final descriptor references."""

    checker_variant_expr: str
    structure: Literal["immediate_multiple_check"] = "immediate_multiple_check"


@dataclass(frozen=True)
class TensorStoreCoordinateLoopModel:
    """Selected tensor store coordinate loop structure with final descriptor references."""


    structure: Literal["tensor_store_coordinate_loop"] = "tensor_store_coordinate_loop"


@dataclass(frozen=True)
class CvtRuleCheckModel:
    """Selected cvt rule check structure with final descriptor references."""


    structure: Literal["cvt_rule_check"] = "cvt_rule_check"


@dataclass(frozen=True)
class CreatePolicyRuleCheckModel:
    """Selected create policy rule check structure with final descriptor references."""


    structure: Literal["create_policy_rule_check"] = "create_policy_rule_check"


@dataclass(frozen=True)
class BulkSizeWidthCheckModel:
    """Selected bulk size width check structure with final descriptor references."""


    structure: Literal["bulk_size_width_check"] = "bulk_size_width_check"


@dataclass(frozen=True)
class CpAsyncRuleCheckModel:
    """Selected cp async rule check structure with final descriptor references."""


    structure: Literal["cp_async_rule_check"] = "cp_async_rule_check"


@dataclass(frozen=True)
class AsyncReleaseCheckModel:
    """Selected async release check structure with final descriptor references."""


    structure: Literal["async_release_check"] = "async_release_check"


@dataclass(frozen=True)
class TensorInfoBoundsModel:
    """Ordered maximum values for the im2col info array."""

    bounds: tuple[int, ...]
    structure: Literal["tensor_info_bounds"] = "tensor_info_bounds"

TensorCheckRow = (
    NamedRuleCheckModel
    | TensorInfoBoundsModel
)
CrossRuleRow = (
    UnifiedAddressCheckModel
    | AtomicQualifierCheckModel
    | MemoryConsistencyCheckModel
    | MemoryVectorCheckModel
    | AddressAlignmentLoopModel
    | ImmediateValueCheckModel
    | ImmediateRangeLoopModel
    | ImmediateMultipleCheckModel
    | TensorStoreCoordinateLoopModel
    | CvtRuleCheckModel
    | CreatePolicyRuleCheckModel
    | BulkSizeWidthCheckModel
    | CpAsyncRuleCheckModel
    | AsyncReleaseCheckModel
    | NamedRuleCheckModel
)

def named_array_policy(entry: GenerationInstruction, variant: ResolvedVariant) -> str:
    """Select the bounded address surface from canonical final-variant metadata."""
    opcode = entry.specification.opcode
    if opcode == "ld" and variant.rule in {
        SemanticRule.DATA_MOVEMENT_LD_GENERIC, SemanticRule.DATA_MOVEMENT_LD_EXPLICIT
    } and not any(field.name == "nc" and field.constant_value is True
                  for field in variant.modifier_fields):
        return "NamedArrayAddressPolicy::Memory"
    if opcode == "st" and variant.rule in {
        SemanticRule.DATA_MOVEMENT_ST_GENERIC, SemanticRule.DATA_MOVEMENT_ST_EXPLICIT
    }:
        return "NamedArrayAddressPolicy::Memory"
    return "NamedArrayAddressPolicy::Reject"


def _append_result(expression: str) -> AppendResultModel:
    """Append a checker result's diagnostics in source evaluation order."""

    return AppendResultModel(expression=expression)


def _member_expr(field: ResolvedField, slot: OperandSlot, *, object_name: str = "selected") -> str:
    """Return an already-guarded direct member expression for one layout."""

    if slot.optional:
        return f"(*{object_name}.{slot.member_name})"
    return f"{object_name}.{slot.member_name}"


def _build_operand_view(field: ResolvedField, slot: OperandSlot, backend: CodegenUnit) -> OperandViewModel:
    """Adapt the shared operand-view emitter to a direct typed slot."""

    return build_check_operand_view(field, _member_expr(field, slot), backend)


def _build_presence_guard(variant: ResolvedVariant, layout_index: int, slots: tuple[OperandSlot, ...]) -> PresenceGuardModel | None:
    """Guard all layout-specific members before any dereference or callback."""

    conditions = []
    for slot in slots:
        if not slot.optional:
            continue
        required = layout_index in slot.layout_indices
        conditions.append(
            f"selected.{slot.member_name}.has_value() != "
            f"{'true' if required else 'false'}"
        )
    if not conditions:
        return None
    condition = tuple(conditions)
    return PresenceGuardModel(condition=condition)


def _build_check_layout(entry: GenerationInstruction, variant: ResolvedVariant, variant_index: int, layout_index: int, backend: CodegenUnit) -> CheckLayoutModel:
    """Build one selected-layout checker branch after tag and presence checks."""

    name = form_name(entry, variant)
    layout = variant.operand_layouts[layout_index]
    slots = operand_slots(variant, backend)
    views = tuple(
        _build_operand_view(field, operand_slot_for_field(slots, field, backend), backend)
        for field in layout.fields
    )
    guard = _build_presence_guard(variant, layout_index, slots)
    descriptor = f"{entry.specification.opcode.replace('.', '_')}_resolved_descriptor()"
    checker_descriptor = f"{entry.specification.opcode.replace('.', '_')}_checker_descriptor()"
    checker_variant = f"{checker_descriptor}.variants[{variant_index}]"
    cross_checks = _build_cross_rule_checks(
        entry.resolved, variant, checker_variant,
        address_qualifier_expr="selected.address_qualifier",
    )
    mma_check = _build_tcgen_mma_source_check(variant, layout, slots, backend)
    tensor_checks = _build_tensor_layout_checks(variant, layout, slots, backend)
    surface_check = _build_surface_layout_check(entry, variant, layout, slots, backend)
    texture_check = _build_texture_layout_check(entry, variant, layout, slots, backend)
    stack_check = _build_stack_layout_check(entry, variant, layout, slots, backend)
    named_checks = []
    for field in layout.fields:
        member = _member_expr(field, operand_slot_for_field(slots, field, backend))
        policy = named_array_policy(entry, variant)
        if field.value_kind is ResolvedValueKind.ADDRESS:
            pointer = f"&{member}.value"
        elif field.value_kind is ResolvedValueKind.MOV_SOURCE:
            pointer = f"std::get_if<ResolvedAddress>(&{member}.value)"
            policy = "NamedArrayAddressPolicy::Mov"
        elif field.value_kind is ResolvedValueKind.TENSOR_OPERAND:
            pointer = f"&{member}.value.tensor_map.address"
            policy = "NamedArrayAddressPolicy::Reject"
        else:
            continue
        named_checks.append(NamedArrayCheckModel(pointer=pointer, policy=policy))
    named_check = tuple(named_checks)
    matrix = f"&{name}::matrix_topology" if variant.matrix is not None else "nullptr"
    return CheckLayoutModel(
        layout_index=layout_index,
        checker_variant=checker_variant,
        availability_check_result=_append_result('availability_check'),
        layout_modifier_check_result=_append_result('layout_modifier_check'),
        guard=guard,
        named_check=named_check,
        operand_count=len(layout.fields),
        views=views,
        descriptor=descriptor,
        variant_index=variant_index,
        matrix=matrix,
        operand_check_result=_append_result('operand_check'),
        mma_check=mma_check,
        tensor_checks=tensor_checks,
        texture_check=texture_check,
        surface_check=surface_check,
        stack_check=stack_check,
        cross_checks=cross_checks,
    )


def _build_surface_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> SurfaceCheckModel | None:
    """Recheck nested surface payloads after selected-layout presence guards."""
    if variant.surface is None:
        return None
    access = next(field for field in layout.fields if field.value_kind in {
        ResolvedValueKind.SURFACE_ACCESS, ResolvedValueKind.SURFACE_QUERY_RESOURCE})
    member = _member_expr(access, operand_slot_for_field(slots, access, backend))
    name = form_name(entry, variant)
    if variant.surface.query is not None:
        expression = f"checker::check_surface_query_static_payload({name}::surface_contract, {member}.value, context)"
    else:
        expression = f"checker::check_surface_static_payload({name}::surface_contract, selected.surface_selected_types(), {member}.value, context)"
    return SurfaceCheckModel(
        expression=expression,
        surface_payload_check_result=_append_result('surface_payload_check'),
    )


def _build_stack_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> StackCheckModel | None:
    """Recheck owned stack carrier and unconverted alignment provenance."""
    if variant.stack is None:
        return None
    def pointer(kind: ResolvedValueKind) -> str:
        """Borrow the field selected by this exact layout."""
        field = next((f for f in layout.fields if f.value_kind is kind), None)
        if field is None:
            return "nullptr"
        member = _member_expr(field, operand_slot_for_field(slots, field, backend))
        return "&" + member + ".value"
    alignment = next((f for f in layout.fields if f.source_name == "alignment"), None)
    align = ("&" + _member_expr(alignment, operand_slot_for_field(slots, alignment, backend)) + ".value"
             if alignment else "nullptr")
    return StackCheckModel(
        form_cpp_name=form_name(entry, variant),
        stack_token=pointer(ResolvedValueKind.STACK_TOKEN),
        allocation_result=pointer(ResolvedValueKind.LOCAL_ALLOCATION_RESULT),
        size_operand=pointer(ResolvedValueKind.REG_OR_IMM),
        align=align,
        stack_payload_check_result=_append_result('stack_payload_check'),
    )


def _build_texture_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> TexturePayloadCheckModel | TextureQueryCheckModel | None:
    """Recheck nested texture payloads after selected-layout presence guards."""

    if variant.texture is None:
        return None
    if variant.texture.geometry is None:
        if variant.texture.query is None:
            return None
        resource = next(field for field in layout.fields
                        if field.value_kind is ResolvedValueKind.TEXTURE_QUERY_RESOURCE)
        resource_member = _member_expr(
            resource, operand_slot_for_field(slots, resource, backend))
        name = form_name(entry, variant)
        return TextureQueryCheckModel(
            name=name,
            resource_member=resource_member,
            texture_query_check_result=_append_result('texture_query_check'),
        )
    access = next(field for field in layout.fields
                  if field.value_kind is ResolvedValueKind.TEXTURE_ACCESS)
    result = next(field for field in layout.fields
                  if field.value_kind is ResolvedValueKind.TEXTURE_RESULT)
    access_member = _member_expr(
        access, operand_slot_for_field(slots, access, backend))
    result_member = _member_expr(
        result, operand_slot_for_field(slots, result, backend))
    required = any(binding.texture_residency_required
                   for binding in layout.bindings)
    name = form_name(entry, variant)
    return TexturePayloadCheckModel(
        name=name,
        access_member=access_member,
        result_member=result_member,
        residency_required=str(required).lower(),
        texture_payload_check_result=_append_result('texture_payload_check'),
    )


def _build_named_rule_check(label: str, expression: str) -> NamedRuleCheckModel:
    """Emit one ordered checker call and append its diagnostics."""

    return NamedRuleCheckModel(
        label=label,
        expression=expression,
        named_check_result=_append_result(label),
    )


def _build_tcgen_mma_source_check(variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> MmaSourceCheckModel | None:
    """Select MMA A's typed carrier from the field, not its layout label."""

    if variant.rule is not SemanticRule.TENSOR_MEMORY_MMA:
        return None
    fields = {field.name: field for field in layout.fields}
    def reference(name: str) -> str:
        """Borrow the selected direct member after the emitted presence guard."""

        field = fields[name]
        slot = operand_slot_for_field(slots, field, backend)
        return "&" + _member_expr(field, slot)
    a = fields["a"]
    if a.value_kind is ResolvedValueKind.TCGEN_BRACKETED_ADDRESS:
        address, shared = reference("a"), "nullptr"
    elif a.value_kind is ResolvedValueKind.REGISTER:
        address, shared = "nullptr", reference("a")
    else:
        raise ValueError("TCGEN MMA A has unsupported typed carrier")
    mask = reference("disable_output_lane") if "disable_output_lane" in fields else "nullptr"
    scale = reference("scale_input_d") if "scale_input_d" in fields else "nullptr"
    selector = "&selected.scale_vector_size" if any(
        modifier.name == "scale_vector_size" for modifier in variant.modifier_fields
    ) else "nullptr"
    scale_a = reference("scale_a") if "scale_a" in fields else "nullptr"
    scale_b = reference("scale_b") if "scale_b" in fields else "nullptr"
    ashift = "&selected.ashift" if any(
        modifier.name == "ashift" for modifier in variant.modifier_fields
    ) else "nullptr"
    collector = "&selected.collector" if any(
        modifier.name == "collector" for modifier in variant.modifier_fields
    ) else "nullptr"
    zero_column = (reference("zero_column_desc")
                   if "zero_column_desc" in fields else "nullptr")
    metadata = reference("sp_meta") if "sp_meta" in fields else "nullptr"
    ws = "true" if any(modifier.name == "ws"
                       for modifier in variant.modifier_fields) else "false"
    sparse = "true" if any(modifier.name == "sp"
                           for modifier in variant.modifier_fields) else "false"
    return MmaSourceCheckModel(
        d_member=reference('d')[1:],
        address=address,
        shared=shared,
        b_member=reference('b')[1:],
        idesc_member=reference('idesc')[1:],
        mask=mask,
        enable_input_d_member=reference('enable_input_d')[1:],
        scale=scale,
        selector=selector,
        scale_a=scale_a,
        scale_b=scale_b,
        ashift=ashift,
        collector=collector,
        metadata=metadata,
        zero_column=zero_column,
        ws=ws,
        sparse=sparse,
        mma_source_check_result=_append_result('mma_source_check'),
    )


def _build_tensor_layout_checks(variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> tuple[TensorCheckRow, ...] | None:
    """Recheck tensor-owned roles whose operands depend on selected layout."""

    if variant.tensor_access_mode is None:
        return None
    fields = {field.name: field for field in layout.fields}
    def member(name: str) -> str:
        """Name one selected owned member after its presence guard."""

        field = fields[name]
        return _member_expr(field, operand_slot_for_field(slots, field, backend))
    checks: list[TensorCheckRow] = []
    if variant.tensor_im2col_info_elements:
        args = (
            f"{member('tensor')}, {member('dst')}, {member('mbar')}, context"
            if "dst" in fields else f"{member('tensor')}, context"
        )
        checks.append(_build_named_rule_check(
            "tensor_read_address_check", f"check_tensor_read_addresses({args})"
        ))
        if "im2col_info" in fields:
            checks.append(TensorInfoBoundsModel(tuple(bound for _, bound in variant.tensor_im2col_info_elements)))
            checks.append(_build_named_rule_check(
                "tensor_info_check",
                f"check_tensor_im2col_info({member('tensor')}, "
                f"{member('im2col_info')}, info_maximum_values, context)",
            ))
    elif variant.tensor_access_mode in {
        TensorAccessMode.TILE_GATHER4, TensorAccessMode.TILE_SCATTER4,
    }:
        checks.append(_build_named_rule_check(
            "gather_scatter_coordinate_check",
            f"check_tensor_gather_scatter_coordinates({member('tensor')}, context)",
        ))
        if variant.tensor_access_mode is TensorAccessMode.TILE_GATHER4:
            args = (
                f"{member('tensor')}, {member('dst')}, {member('mbar')}, context"
                if "dst" in fields else f"{member('tensor')}, context"
            )
            checks.append(_build_named_rule_check(
                "gather_address_check", f"check_tensor_read_addresses({args})"
            ))
    elif (variant.tensor_access_mode is TensorAccessMode.TILED and
          (variant.tensor_cta_group_applicable or variant.tensor_multicast) and
          {"dst", "mbar"} <= fields.keys()):
        checks.append(_build_named_rule_check(
            "tensor_read_address_check",
            f"check_tensor_read_addresses({member('tensor')}, "
            f"{member('dst')}, {member('mbar')}, context)",
        ))
    if variant.tensor_multicast:
        if "cta_mask" not in fields:
            raise ValueError("multicast tensor form lacks mask")
        checks.append(_build_named_rule_check(
            "tensor_multicast_mask_check",
            f"check_tensor_multicast_mask({member('cta_mask')}, context)",
        ))
    if any(field.name == "cache_policy" for field in layout.fields):
        checks.append(_build_named_rule_check(
            "tensor_cache_policy_check",
            f"check_tensor_cache_policy({member('cache_policy')}, context)",
        ))
    return tuple(checks)


def build_check(entry: GenerationInstruction, variant: ResolvedVariant, variant_index: int, backend: CodegenUnit) -> CheckModel:
    """Build one final form's checker preserving prior diagnostic order."""

    name = form_name(entry, variant)
    modifier_fields = tuple(
        field for field in variant.modifier_fields
        if field.origin is ResolvedFieldOrigin.MODIFIER
    )
    field_views = tuple(
        build_check_modifier_view(
            field, backend, form_cpp_name=name, member_expr=f"selected.{field.name}"
        )
        for field in modifier_fields
    )
    modifier_views = tuple(
        build_check_modifier_value_view(
            variant, field, backend, index,
            form_cpp_name=name, member_expr=f"selected.{field.name}",
        )
        for index, field in enumerate(modifier_fields)
    )
    prefix = entry.specification.opcode.replace(".", "_")
    descriptor = f"{prefix}_checker_descriptor()"
    layout_cases = tuple(
        _build_check_layout(entry, variant, variant_index, index, backend)
        for index, _ in enumerate(variant.operand_layouts)
    )
    group_check = (
        _build_named_rule_check(
            "tensor_group_check", "check_tensor_cta_group(selected.cta_group, context)"
        )
        if variant.tensor_access_mode is not None
        and any(field.source_name == "cta_group" for field in modifier_fields)
        else None
    )
    hint_check = (
        _build_named_rule_check(
            "tensor_cache_hint_check",
            "check_tensor_cache_hint(selected.cache_hint, context)",
        )
        if variant.tensor_access_mode is not None
        and any(field.name == "cache_hint" for field in modifier_fields)
        else None
    )
    return CheckModel(
        name=name,
        predicate_check_result=_append_result('predicate_check'),
        modifier_count=len(modifier_fields),
        field_views=field_views,
        modifier_views=modifier_views,
        descriptor=descriptor,
        variant_name=variant.cpp_name,
        common_check_result=_append_result('common_check'),
        variant_index=variant_index,
        domain_check_result=_append_result('domain_check'),
        modifier_availability_check_result=_append_result('modifier_availability_check'),
        group_check=group_check,
        hint_check=hint_check,
        layout_count=len(variant.operand_layouts),
        layout_check_result=_append_result('layout_check'),
        layout_cases=layout_cases,
    )


def _build_cross_rule_checks(
    instruction: ResolvedInstruction,
    variant: ResolvedVariant,
    checker_variant_expr: str,
    *, address_qualifier_expr: str,
) -> tuple[CrossRuleRow, ...]:
    """Build a variant's cross-rule checks in a fixed order."""

    checks: list[CrossRuleRow] = []
    if instruction.atomic_address_qualifier is not None:
        checks.append(AtomicQualifierCheckModel(checker_variant_expr=checker_variant_expr, address_qualifier_expr=address_qualifier_expr))
    checks.append(UnifiedAddressCheckModel(checker_variant_expr=checker_variant_expr))
    if variant.memory_consistency is not None:
        checks.append(MemoryConsistencyCheckModel(checker_variant_expr=checker_variant_expr))
    if variant.memory_vector is not None:
        checks.append(MemoryVectorCheckModel(checker_variant_expr=checker_variant_expr))
    if variant.address_alignments:
        checks.append(AddressAlignmentLoopModel(checker_variant_expr=checker_variant_expr))
    if variant.immediate_value is not None:
        checks.append(ImmediateValueCheckModel(checker_variant_expr=checker_variant_expr))
    if variant.immediate_ranges:
        checks.append(ImmediateRangeLoopModel(checker_variant_expr=checker_variant_expr))
    if variant.immediate_multiple_of is not None:
        checks.append(ImmediateMultipleCheckModel(checker_variant_expr=checker_variant_expr))
    if variant.fabric is not None and variant.fabric.operation in {
        FabricOperation.TRY_GET, FabricOperation.TRY_PUT,
        FabricOperation.TRY_RED, FabricOperation.TRY_PULLRED,
    }:
        handle_field = ("src" if variant.fabric.operation in {
            FabricOperation.TRY_GET, FabricOperation.TRY_PULLRED} else "dst")
        checks.append(_build_named_rule_check(
            "fabric_handle_check",
            f"check_fabric_handle(selected.{handle_field}, "
            f"{'true' if variant.fabric.counted else 'false'}, context)",
        ))
    if (
        variant.completion_kind is AsyncCompletionKind.BULK_GROUP
        and any(
            field.value_kind is ResolvedValueKind.TENSOR_OPERAND
            for layout in variant.operand_layouts
            for field in layout.fields
        )
    ):
        checks.append(TensorStoreCoordinateLoopModel())
    if variant.rule is SemanticRule.DATA_MOVEMENT_CVT:
        checks.append(CvtRuleCheckModel())
    if variant.rule is SemanticRule.DATA_MOVEMENT_CREATEPOLICY:
        checks.append(CreatePolicyRuleCheckModel())
    if variant.rule is SemanticRule.DATA_MOVEMENT_ST_BULK:
        checks.append(BulkSizeWidthCheckModel())
    if variant.rule is SemanticRule.DATA_MOVEMENT_CP_ASYNC:
        checks.append(CpAsyncRuleCheckModel())
    if variant.rule is SemanticRule.PARALLEL_SYNC_AND_COMMUNICATION_RED_ASYNC_RELEASE:
        checks.append(AsyncReleaseCheckModel())
    if variant.rule is SemanticRule.DATA_MOVEMENT_TENSORMAP_REPLACE:
        checks.append(_build_named_rule_check(
            "tensor_map_rule_check", "check_tensor_map_replace_rule(fields, operands, context)"
        ))
        checks.append(_build_named_rule_check(
            "tensor_map_address_check",
            "check_tensor_map_address_register_width(selected.tensor_map, context)",
        ))
    if variant.rule is SemanticRule.DATA_MOVEMENT_TENSORMAP_CP_FENCEPROXY:
        checks.append(_build_named_rule_check(
            "tensor_map_fence_check",
            "check_tensor_map_cp_fenceproxy_rule(operands, context)",
        ))
        for name in ("dst", "src"):
            checks.append(_build_named_rule_check(
                f"tensor_map_{name}_check",
                f"check_tensor_map_address_register_width(selected.{name}, context)",
            ))
    if (variant.completion_kind is AsyncCompletionKind.BULK_GROUP and
        any(field.value_kind is ResolvedValueKind.TENSOR_OPERAND
            for layout in variant.operand_layouts for field in layout.fields)):
        checks.append(_build_named_rule_check(
            "tensor_write_address_check",
            "check_tensor_reduction_addresses(selected.tensor, selected.src, context)",
        ))
    allocation_rules = {
        SemanticRule.TENSOR_MEMORY_ALLOC,
        SemanticRule.TENSOR_MEMORY_DEALLOC,
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT,
    }
    if variant.rule in allocation_rules:
        checks.append(_build_named_rule_check(
            "tcgen_allocation_check",
            "check_tcgen_allocation_rule(selected.allocation_action, operands, context)",
        ))
    if variant.rule is SemanticRule.TENSOR_MEMORY_ALLOC:
        checks.append(_build_named_rule_check(
            "tcgen_result_slot_check",
            "check_tcgen_allocation_result_slot(selected.dst, context)",
        ))
    if variant.rule is SemanticRule.TENSOR_MEMORY_COMMIT:
        checks.append(_build_named_rule_check(
            "tcgen_barrier_check", "check_tcgen_commit_address(selected.mbar, context)"
        ))
        if variant.tcgen_commit_multicast:
            checks.append(_build_named_rule_check(
                "tcgen_mask_check", "check_tcgen_commit_mask(selected.cta_mask, context)"
            ))
    if variant.rule in {
        SemanticRule.TENSOR_MEMORY_LOAD,
        SemanticRule.TENSOR_MEMORY_STORE,
        SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION,
    }:
        reduction = "true" if variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION else "false"
        checks.append(_build_named_rule_check(
            "tcgen_transfer_check",
            f"check_tcgen_transfer_rule(fields, operands, {reduction}, context)",
        ))
        checks.append(_build_named_rule_check(
            "tcgen_address_check", "check_tcgen_transfer_address(selected.taddr, context)"
        ))
        checks.append(_build_named_rule_check(
            "tcgen_fragment_check", "check_tcgen_transfer_fragment(selected.r, context)"
        ))
        if variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION:
            checks.append(_build_named_rule_check(
                "tcgen_result_check", "check_tcgen_reduction_result(selected.redval, context)"
            ))
        if any(field.name == "splitoff" for layout in variant.operand_layouts
               for field in layout.fields):
            checks.append(_build_named_rule_check(
                "tcgen_split_check", "check_tcgen_half_split_offset(selected.splitoff, context)"
            ))
    if variant.rule is SemanticRule.TENSOR_MEMORY_COPY:
        checks.append(_build_named_rule_check(
            "tcgen_copy_check",
            "check_tcgen_copy_rule(fields, selected.copy_pairs, selected.copy_format_masks, context)",
        ))
        checks.append(_build_named_rule_check(
            "tcgen_address_check", "check_tcgen_transfer_address(selected.taddr, context)"
        ))
        checks.append(_build_named_rule_check(
            "tcgen_descriptor_check", "check_tcgen_copy_descriptor(selected.s_desc, context)"
        ))
    if variant.rule is SemanticRule.TENSOR_MEMORY_SHIFT:
        checks.append(_build_named_rule_check(
            "tcgen_shift_check", "check_tcgen_shift_address(selected.taddr, context)"
        ))
    return tuple(checks)



@dataclass(frozen=True)
class CrossRuleChecksModel:
    """Ordered rows consumed by the supported cross-rule fragment adapter."""

    rows: tuple[CrossRuleRow, ...]
