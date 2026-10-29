"""Render final-form checker methods and ordered layout-specific checks.

Callers provide exact class and payload expressions; optional payload members
must be guarded before their expressions are evaluated in generated C++.
"""

from __future__ import annotations

from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.code_gen.cpp_names import form_name
from ptx_frontend.code_gen.emit.operand_views import (
    emit_check_modifier_value_view,
    emit_check_modifier_view,
    emit_check_operand_view,
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


def _append_result(expression: str) -> str:
    """Append a checker result's diagnostics in source evaluation order."""

    return f"""  if (!{expression}) {{
    diagnostics.insert(diagnostics.end(), {expression}.error().begin(),
                       {expression}.error().end());
  }}"""


def _member_expr(field: ResolvedField, slot: OperandSlot, *, object_name: str = "selected") -> str:
    """Return an already-guarded direct member expression for one layout."""

    if slot.optional:
        return f"(*{object_name}.{slot.member_name})"
    return f"{object_name}.{slot.member_name}"


def _emit_operand_view(field: ResolvedField, slot: OperandSlot, backend: CodegenUnit) -> str:
    """Adapt the shared operand-view emitter to a direct typed slot."""

    return emit_check_operand_view(field, _member_expr(field, slot), backend)


def _emit_presence_guard(variant: ResolvedVariant, layout_index: int, slots: tuple[OperandSlot, ...]) -> str:
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
        return ""
    condition = " ||\n      ".join(conditions)
    return f"""  if ({condition}) {{
    diagnostics.push_back(checker::CheckDiagnostic{{
        .kind = checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch,
        .range = context.instruction_range,
        .message = "Resolved operand-layout tag and direct fields disagree.",
    }});
    return std::unexpected(std::move(diagnostics));
  }}"""


def _emit_check_layout(entry: GenerationInstruction, variant: ResolvedVariant, variant_index: int, layout_index: int, backend: CodegenUnit) -> str:
    """Emit one selected-layout checker branch after tag and presence checks."""

    name = form_name(entry, variant)
    layout = variant.operand_layouts[layout_index]
    slots = operand_slots(variant, backend)
    views = ",\n".join(
        _emit_operand_view(field, operand_slot_for_field(slots, field, backend), backend)
        for field in layout.fields
    )
    guard = _emit_presence_guard(variant, layout_index, slots)
    descriptor = f"{entry.specification.opcode.replace('.', '_')}_resolved_descriptor()"
    checker_descriptor = f"{entry.specification.opcode.replace('.', '_')}_checker_descriptor()"
    checker_variant = f"{checker_descriptor}.variants[{variant_index}]"
    cross_checks = _emit_cross_rule_checks(
        entry.resolved, variant, checker_variant,
        address_qualifier_expr="selected.address_qualifier",
    )
    mma_check = _emit_tcgen_mma_source_check(variant, layout, slots, backend)
    tensor_checks = _emit_tensor_layout_checks(variant, layout, slots, backend)
    surface_check = _emit_surface_layout_check(entry, variant, layout, slots, backend)
    texture_check = _emit_texture_layout_check(entry, variant, layout, slots, backend)
    stack_check = _emit_stack_layout_check(entry, variant, layout, slots, backend)
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
        named_checks.append(f'''      if (const auto* address = {pointer};
          address && !valid_named_array_address(*address, {policy}, context.instruction_range)) {{
        diagnostics.push_back(checker::CheckDiagnostic{{
            .kind = checker::CheckDiagnosticKind::RuleViolation,
            .range = context.instruction_range,
            .message = "Invalid or unsupported owned named-array address payload.",
        }});
      }}''')
    named_check = "\n".join(named_checks)
    matrix = f"&{name}::matrix_topology" if variant.matrix is not None else "nullptr"
    return f"""    case {layout_index}: {{
      const auto availability_check = checker::check_operand_layout_availability(
          {checker_variant}, {layout_index}, context);
{_append_result('availability_check')}
      const auto layout_modifier_check = checker::check_operand_layout_modifiers(
          {checker_variant}, {layout_index}, modifier_values, context);
{_append_result('layout_modifier_check')}
{guard}
{named_check}
      const std::array<checker::OperandView, {len(layout.fields)}> operands = {{{{
{views}
      }}}};
      const auto operand_check = checker::check_operands(
          {descriptor}.variants[{variant_index}].operand_layouts[{layout_index}].bindings,
          fields, operands, {checker_variant}.operand_type_compatibilities,
          context, {matrix});
{_append_result('operand_check')}
{mma_check}
{tensor_checks}
{texture_check}
{surface_check}
{stack_check}
{cross_checks}
      break;
    }}"""


def _emit_surface_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> str:
    """Recheck nested surface payloads after selected-layout presence guards."""
    if variant.surface is None:
        return ""
    access = next(field for field in layout.fields if field.value_kind in {
        ResolvedValueKind.SURFACE_ACCESS, ResolvedValueKind.SURFACE_QUERY_RESOURCE})
    member = _member_expr(access, operand_slot_for_field(slots, access, backend))
    name = form_name(entry, variant)
    if variant.surface.query is not None:
        expression = f"checker::check_surface_query_static_payload({name}::surface_contract, {member}.value, context)"
    else:
        expression = f"checker::check_surface_static_payload({name}::surface_contract, selected.surface_selected_types(), {member}.value, context)"
    return f"      const auto surface_payload_check = {expression};\n" + _append_result("surface_payload_check")


def _emit_stack_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> str:
    """Recheck owned stack carrier and unconverted alignment provenance."""
    if variant.stack is None:
        return ""
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
    return f"""      const auto stack_payload_check = checker::check_stack_static_payload(
          {form_name(entry, variant)}::stack_contract,
          {pointer(ResolvedValueKind.STACK_TOKEN)},
          {pointer(ResolvedValueKind.LOCAL_ALLOCATION_RESULT)},
          {pointer(ResolvedValueKind.REG_OR_IMM)}, {align}, context);
{_append_result('stack_payload_check')}"""


def _emit_texture_layout_check(entry: GenerationInstruction, variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> str:
    """Recheck nested texture payloads after selected-layout presence guards."""

    if variant.texture is None:
        return ""
    if variant.texture.geometry is None:
        if variant.texture.query is None:
            return ""
        resource = next(field for field in layout.fields
                        if field.value_kind is ResolvedValueKind.TEXTURE_QUERY_RESOURCE)
        resource_member = _member_expr(
            resource, operand_slot_for_field(slots, resource, backend))
        name = form_name(entry, variant)
        return f"""      const auto texture_query_check = checker::check_texture_query_static_payload(
          {name}::texture_contract, {resource_member}.value, context);
{_append_result('texture_query_check')}"""
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
    return f"""      const auto texture_payload_check = checker::check_texture_static_payload(
          {name}::texture_contract, selected.texture_selected_types(),
          {access_member}.value, {result_member}.value,
          {str(required).lower()}, context);
{_append_result('texture_payload_check')}"""


def _emit_named_rule_check(label: str, expression: str) -> str:
    """Emit one ordered checker call and append its diagnostics."""

    return f"      const auto {label} = {expression};\n{_append_result(label)}\n"


def _emit_tcgen_mma_source_check(variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> str:
    """Select MMA A's typed carrier from the field, not its layout label."""

    if variant.rule is not SemanticRule.TENSOR_MEMORY_MMA:
        return ""
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
    return f"""      const auto mma_source_check = check_tcgen_mma_sources(
          selected.cta_group, {reference('d')[1:]}, {address}, {shared},
          {reference('b')[1:]}, {reference('idesc')[1:]}, {mask},
          {reference('enable_input_d')[1:]}, {scale}, {selector},
          {scale_a}, {scale_b}, {ashift}, {collector}, {metadata},
          {zero_column}, {ws}, {sparse},
          context);
{_append_result('mma_source_check')}"""


def _emit_tensor_layout_checks(variant: ResolvedVariant, layout: ResolvedOperandLayout, slots: tuple[OperandSlot, ...], backend: CodegenUnit) -> str:
    """Recheck tensor-owned roles whose operands depend on selected layout."""

    if variant.tensor_access_mode is None:
        return ""
    fields = {field.name: field for field in layout.fields}
    def member(name: str) -> str:
        """Name one selected owned member after its presence guard."""

        field = fields[name]
        return _member_expr(field, operand_slot_for_field(slots, field, backend))
    checks = ""
    if variant.tensor_im2col_info_elements:
        args = (
            f"{member('tensor')}, {member('dst')}, {member('mbar')}, context"
            if "dst" in fields else f"{member('tensor')}, context"
        )
        checks += _emit_named_rule_check(
            "tensor_read_address_check", f"check_tensor_read_addresses({args})"
        )
        if "im2col_info" in fields:
            bounds = ", ".join(str(bound) for _, bound in variant.tensor_im2col_info_elements)
            checks += (
                f"      const std::array<uint16_t, {len(variant.tensor_im2col_info_elements)}> "
                f"info_maximum_values = {{{bounds}}};\n"
            )
            checks += _emit_named_rule_check(
                "tensor_info_check",
                f"check_tensor_im2col_info({member('tensor')}, "
                f"{member('im2col_info')}, info_maximum_values, context)",
            )
    elif variant.tensor_access_mode in {
        TensorAccessMode.TILE_GATHER4, TensorAccessMode.TILE_SCATTER4,
    }:
        checks += _emit_named_rule_check(
            "gather_scatter_coordinate_check",
            f"check_tensor_gather_scatter_coordinates({member('tensor')}, context)",
        )
        if variant.tensor_access_mode is TensorAccessMode.TILE_GATHER4:
            args = (
                f"{member('tensor')}, {member('dst')}, {member('mbar')}, context"
                if "dst" in fields else f"{member('tensor')}, context"
            )
            checks += _emit_named_rule_check(
                "gather_address_check", f"check_tensor_read_addresses({args})"
            )
    elif (variant.tensor_access_mode is TensorAccessMode.TILED and
          (variant.tensor_cta_group_applicable or variant.tensor_multicast) and
          {"dst", "mbar"} <= fields.keys()):
        checks += _emit_named_rule_check(
            "tensor_read_address_check",
            f"check_tensor_read_addresses({member('tensor')}, "
            f"{member('dst')}, {member('mbar')}, context)",
        )
    if variant.tensor_multicast:
        if "cta_mask" not in fields:
            raise ValueError("multicast tensor form lacks mask")
        checks += _emit_named_rule_check(
            "tensor_multicast_mask_check",
            f"check_tensor_multicast_mask({member('cta_mask')}, context)",
        )
    if any(field.name == "cache_policy" for field in layout.fields):
        checks += _emit_named_rule_check(
            "tensor_cache_policy_check",
            f"check_tensor_cache_policy({member('cache_policy')}, context)",
        )
    return checks


def emit_check(entry: GenerationInstruction, variant: ResolvedVariant, variant_index: int, backend: CodegenUnit) -> str:
    """Emit one final form's checker preserving prior diagnostic order."""

    name = form_name(entry, variant)
    modifier_fields = tuple(
        field for field in variant.modifier_fields
        if field.origin is ResolvedFieldOrigin.MODIFIER
    )
    field_views = ",\n".join(
        emit_check_modifier_view(
            field, backend, form_cpp_name=name, member_expr=f"selected.{field.name}"
        )
        for field in modifier_fields
    )
    modifier_views = ",\n".join(
        emit_check_modifier_value_view(
            variant, field, backend, index,
            form_cpp_name=name, member_expr=f"selected.{field.name}",
        )
        for index, field in enumerate(modifier_fields)
    )
    prefix = entry.specification.opcode.replace(".", "_")
    descriptor = f"{prefix}_checker_descriptor()"
    layout_cases = "\n".join(
        _emit_check_layout(entry, variant, variant_index, index, backend)
        for index, _ in enumerate(variant.operand_layouts)
    )
    group_check = (
        _emit_named_rule_check(
            "tensor_group_check", "check_tensor_cta_group(selected.cta_group, context)"
        )
        if variant.tensor_access_mode is not None
        and any(field.source_name == "cta_group" for field in modifier_fields)
        else ""
    )
    hint_check = (
        _emit_named_rule_check(
            "tensor_cache_hint_check",
            "check_tensor_cache_hint(selected.cache_hint, context)",
        )
        if variant.tensor_access_mode is not None
        and any(field.name == "cache_hint" for field in modifier_fields)
        else ""
    )
    return f"""/** Check the {name} final form in existing diagnostic order. */
checker::CheckResult {name}::check(const checker::Context& context) const {{
  const auto& selected = *this;
  checker::CheckDiagnostics diagnostics;
  const auto predicate_check = checker::check_execution_predicate(
      execution_predicate, context);
{_append_result('predicate_check')}
  const std::array<checker::FieldView, {len(modifier_fields)}> fields = {{{{
{field_views}
  }}}};
  const std::array<checker::ModifierValueView, {len(modifier_fields)}> modifier_values = {{{{
{modifier_views}
  }}}};
  const auto common_check = checker::check_common({descriptor},
      "{variant.cpp_name}", context);
{_append_result('common_check')}
  const auto domain_check = checker::check_modifier_value_domain(
      {descriptor}.variants[{variant_index}].modifier_value_domains,
      modifier_values, context);
{_append_result('domain_check')}
  const auto modifier_availability_check = checker::check_modifier_value_availability(
      {descriptor}.variants[{variant_index}].modifier_value_availabilities,
      modifier_values, context);
{_append_result('modifier_availability_check')}
{group_check}
{hint_check}
  const auto layout_check = checker::check_operand_layout_tag(
      "{variant.cpp_name}", operand_layout.value,
      {len(variant.operand_layouts)}, context);
{_append_result('layout_check')}
  if (layout_check) {{
    switch (operand_layout.value) {{
{layout_cases}
    default:
      break;
    }}
  }}
  if (diagnostics.empty()) return {{}};
  return std::unexpected(std::move(diagnostics));
}}"""


def _emit_cross_rule_checks(
    instruction: ResolvedInstruction,
    variant: ResolvedVariant,
    checker_variant_expr: str,
    *, address_qualifier_expr: str,
) -> str:
    """Emit a variant's cross-rule checks in a fixed order."""

    checks = ""
    if instruction.atomic_address_qualifier is not None:
        checks += f"""            const auto atomic_check = check_atomic_qualifiers(
                {checker_variant_expr}.atomic_address_qualifier,
                {address_qualifier_expr}, fields, operands, context);
""" + """\
            if (!atomic_check) {
              diagnostics.insert(diagnostics.end(), atomic_check.error().begin(),
                                 atomic_check.error().end());
            }
"""
    checks += f"""            const auto unified_address_check = check_unified_address_suffix(
                {checker_variant_expr}, fields, operands, context);
            if (!unified_address_check) {{
              diagnostics.insert(diagnostics.end(), unified_address_check.error().begin(),
                                 unified_address_check.error().end());
            }}
"""
    if variant.memory_consistency is not None:
        checks += f"""            const auto consistency_check = check_memory_consistency(
                {checker_variant_expr}.memory_consistency, fields, operands, context);
            if (!consistency_check) {{
              diagnostics.insert(diagnostics.end(), consistency_check.error().begin(),
                                 consistency_check.error().end());
            }}
"""
    if variant.memory_vector is not None:
        checks += f"""            const auto memory_vector_check = check_memory_vector(
                {checker_variant_expr}.memory_vector, fields, operands, context);
            if (!memory_vector_check) {{
              diagnostics.insert(diagnostics.end(), memory_vector_check.error().begin(),
                                 memory_vector_check.error().end());
            }}
"""
    if variant.address_alignments:
        checks += f"""            for (const auto& alignment : {checker_variant_expr}.address_alignments) {{
              const auto alignment_check = check_address_alignment(
                  alignment, fields, operands, context);
              if (!alignment_check) {{
                diagnostics.insert(diagnostics.end(), alignment_check.error().begin(),
                                   alignment_check.error().end());
              }}
            }}
"""
    if variant.immediate_value is not None:
        checks += f"""            const auto immediate_value_check = check_immediate_value(
                {checker_variant_expr}.immediate_value, operands, context);
            if (!immediate_value_check) {{
              diagnostics.insert(diagnostics.end(), immediate_value_check.error().begin(),
                                 immediate_value_check.error().end());
            }}
"""
    if variant.immediate_ranges:
        checks += f"""            for (const auto& immediate_range : {checker_variant_expr}.immediate_ranges) {{
              const auto immediate_range_check = check_immediate_range(
                  immediate_range, operands, context);
              if (!immediate_range_check) {{
                diagnostics.insert(diagnostics.end(), immediate_range_check.error().begin(),
                                   immediate_range_check.error().end());
              }}
            }}
"""
    if variant.immediate_multiple_of is not None:
        checks += f"""            const auto immediate_multiple_of_check = check_immediate_multiple_of(
                {checker_variant_expr}.immediate_multiple_of, operands, context);
            if (!immediate_multiple_of_check) {{
              diagnostics.insert(diagnostics.end(), immediate_multiple_of_check.error().begin(),
                                 immediate_multiple_of_check.error().end());
            }}
"""
    if variant.fabric is not None and variant.fabric.operation in {
        FabricOperation.TRY_GET, FabricOperation.TRY_PUT,
        FabricOperation.TRY_RED, FabricOperation.TRY_PULLRED,
    }:
        handle_field = ("src" if variant.fabric.operation in {
            FabricOperation.TRY_GET, FabricOperation.TRY_PULLRED} else "dst")
        checks += _emit_named_rule_check(
            "fabric_handle_check",
            f"check_fabric_handle(selected.{handle_field}, "
            f"{'true' if variant.fabric.counted else 'false'}, context)",
        )
    if (
        variant.completion_kind is AsyncCompletionKind.BULK_GROUP
        and any(
            field.value_kind is ResolvedValueKind.TENSOR_OPERAND
            for layout in variant.operand_layouts
            for field in layout.fields
        )
    ):
        checks += """            for (const auto& operand : operands) {
              if (operand.actual_shape != OperandShape::TensorOperand)
                continue;
              const auto coordinate_check =
                  check_tensor_store_coordinates(operand, context);
              if (!coordinate_check) {
                diagnostics.insert(diagnostics.end(),
                                   coordinate_check.error().begin(),
                                   coordinate_check.error().end());
              }
            }
"""
    if variant.rule is SemanticRule.DATA_MOVEMENT_CVT:
        checks += """            const auto cvt_rule_check = check_cvt_rule(
                modifier_values, operands, context);
            if (!cvt_rule_check) {
              diagnostics.insert(diagnostics.end(), cvt_rule_check.error().begin(),
                                 cvt_rule_check.error().end());
            }
"""
    if variant.rule is SemanticRule.DATA_MOVEMENT_CREATEPOLICY:
        checks += """            const auto createpolicy_rule_check = check_createpolicy_rule(
                operands, context);
            if (!createpolicy_rule_check) {
              diagnostics.insert(diagnostics.end(), createpolicy_rule_check.error().begin(),
                                 createpolicy_rule_check.error().end());
            }
"""
    if variant.rule is SemanticRule.DATA_MOVEMENT_ST_BULK:
        checks += """            const auto size_width_check = check_st_bulk_size_width(
                operands, context);
            if (!size_width_check) {
              diagnostics.insert(diagnostics.end(), size_width_check.error().begin(),
                                 size_width_check.error().end());
            }
"""
    if variant.rule is SemanticRule.DATA_MOVEMENT_CP_ASYNC:
        checks += """            const auto cp_async_rule_check = check_cp_async_rule(
                fields, operands, context);
            if (!cp_async_rule_check) {
              diagnostics.insert(diagnostics.end(), cp_async_rule_check.error().begin(),
                                 cp_async_rule_check.error().end());
            }
"""
    if variant.rule is SemanticRule.PARALLEL_SYNC_AND_COMMUNICATION_RED_ASYNC_RELEASE:
        checks += """            const auto async_release_check = check_red_async_release_qualifiers(
                fields, context);
            if (!async_release_check) {
              diagnostics.insert(diagnostics.end(), async_release_check.error().begin(),
                                 async_release_check.error().end());
            }
"""
    if variant.rule is SemanticRule.DATA_MOVEMENT_TENSORMAP_REPLACE:
        checks += _emit_named_rule_check(
            "tensor_map_rule_check", "check_tensor_map_replace_rule(fields, operands, context)"
        )
        checks += _emit_named_rule_check(
            "tensor_map_address_check",
            "check_tensor_map_address_register_width(selected.tensor_map, context)",
        )
    if variant.rule is SemanticRule.DATA_MOVEMENT_TENSORMAP_CP_FENCEPROXY:
        checks += _emit_named_rule_check(
            "tensor_map_fence_check",
            "check_tensor_map_cp_fenceproxy_rule(operands, context)",
        )
        for name in ("dst", "src"):
            checks += _emit_named_rule_check(
                f"tensor_map_{name}_check",
                f"check_tensor_map_address_register_width(selected.{name}, context)",
            )
    if (variant.completion_kind is AsyncCompletionKind.BULK_GROUP and
        any(field.value_kind is ResolvedValueKind.TENSOR_OPERAND
            for layout in variant.operand_layouts for field in layout.fields)):
        checks += _emit_named_rule_check(
            "tensor_write_address_check",
            "check_tensor_reduction_addresses(selected.tensor, selected.src, context)",
        )
    allocation_rules = {
        SemanticRule.TENSOR_MEMORY_ALLOC,
        SemanticRule.TENSOR_MEMORY_DEALLOC,
        SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT,
    }
    if variant.rule in allocation_rules:
        checks += _emit_named_rule_check(
            "tcgen_allocation_check",
            "check_tcgen_allocation_rule(selected.allocation_action, operands, context)",
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_ALLOC:
        checks += _emit_named_rule_check(
            "tcgen_result_slot_check",
            "check_tcgen_allocation_result_slot(selected.dst, context)",
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_COMMIT:
        checks += _emit_named_rule_check(
            "tcgen_barrier_check", "check_tcgen_commit_address(selected.mbar, context)"
        )
        if variant.tcgen_commit_multicast:
            checks += _emit_named_rule_check(
                "tcgen_mask_check", "check_tcgen_commit_mask(selected.cta_mask, context)"
            )
    if variant.rule in {
        SemanticRule.TENSOR_MEMORY_LOAD,
        SemanticRule.TENSOR_MEMORY_STORE,
        SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION,
    }:
        reduction = "true" if variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION else "false"
        checks += _emit_named_rule_check(
            "tcgen_transfer_check",
            f"check_tcgen_transfer_rule(fields, operands, {reduction}, context)",
        )
        checks += _emit_named_rule_check(
            "tcgen_address_check", "check_tcgen_transfer_address(selected.taddr, context)"
        )
        checks += _emit_named_rule_check(
            "tcgen_fragment_check", "check_tcgen_transfer_fragment(selected.r, context)"
        )
        if variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION:
            checks += _emit_named_rule_check(
                "tcgen_result_check", "check_tcgen_reduction_result(selected.redval, context)"
            )
        if any(field.name == "splitoff" for layout in variant.operand_layouts
               for field in layout.fields):
            checks += _emit_named_rule_check(
                "tcgen_split_check", "check_tcgen_half_split_offset(selected.splitoff, context)"
            )
    if variant.rule is SemanticRule.TENSOR_MEMORY_COPY:
        checks += _emit_named_rule_check(
            "tcgen_copy_check",
            "check_tcgen_copy_rule(fields, selected.copy_pairs, selected.copy_format_masks, context)",
        )
        checks += _emit_named_rule_check(
            "tcgen_address_check", "check_tcgen_transfer_address(selected.taddr, context)"
        )
        checks += _emit_named_rule_check(
            "tcgen_descriptor_check", "check_tcgen_copy_descriptor(selected.s_desc, context)"
        )
    if variant.rule is SemanticRule.TENSOR_MEMORY_SHIFT:
        checks += _emit_named_rule_check(
            "tcgen_shift_check", "check_tcgen_shift_address(selected.taddr, context)"
        )
    return checks
