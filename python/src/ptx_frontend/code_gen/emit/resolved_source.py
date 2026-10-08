"""Emit direct-form resolvers, checkers, visitors, and descriptor definitions."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext, GenerationInstruction
from ptx_frontend.code_gen.emit.checker_descriptors import (
    _emit_instruction_descriptor_storage as emit_checker_storage,
)
from ptx_frontend.code_gen.emit.operand_views import (
    emit_check_modifier_value_view,
    emit_check_modifier_view,
    emit_check_operand_view,
)
from ptx_frontend.code_gen.emit.resolved_descriptors import (
    _emit_resolved_descriptor_storage as emit_resolved_storage,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import (
    _emit_instruction_descriptor_storage as emit_syntax_storage,
)
from ptx_frontend.code_gen.emit.resolved_model import (
    INCLUDE_ROOT, form_name, form_shards, method_name,
)
from ptx_frontend.code_gen.resolved_layout import operand_slot_for_field, operand_slots
from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedInstruction,
    ResolvedOperandLayout,
    ResolvedValueKind,
    ResolvedVariant,
    TensorAccessMode,
)
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.spec.model import AsyncCompletionKind, SemanticRule


def _append_result(expression: str) -> str:
    """Append a checker result's diagnostics in source evaluation order."""

    return f"""  if (!{expression}) {{
    diagnostics.insert(diagnostics.end(), {expression}.error().begin(),
                       {expression}.error().end());
  }}"""


def _address_symbol_resolution_policy(
    field: ResolvedField, layout: ResolvedOperandLayout
) -> str:
    """Return the immutable address-symbol binding policy for one field."""

    if field.value_kind is not ResolvedValueKind.MOV_SOURCE:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    binding = next(
        (item for item in layout.bindings if item.target_field_id == field.name),
        None,
    )
    if binding is None:
        raise ValueError(
            f"layout {layout.layout_id!r} lacks a MOV_SOURCE binding for "
            f"field {field.name!r}"
        )
    if binding.preserve_parameter_address_space:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    return "checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter"


def _member_expr(field, slot, *, object_name: str = "selected") -> str:
    """Return an already-guarded direct member expression for one layout."""

    if slot.optional:
        return f"(*{object_name}.{slot.member_name})"
    return f"{object_name}.{slot.member_name}"


def _emit_operand_view(field, slot, backend) -> str:
    """Adapt the shared operand-view emitter to a direct typed slot."""

    view = emit_check_operand_view(field, "selected", backend)
    return view.replace(
        f"selected.{field.name}", _member_expr(field, slot)
    )


def _emit_presence_guard(variant, layout_index: int, slots) -> str:
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


def _emit_check_layout(entry, variant, variant_index: int, layout_index: int, backend) -> str:
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
    cross_checks = _emit_cross_rule_checks(entry.resolved, variant, checker_variant)
    cross_checks = cross_checks.replace("instruction.address_qualifier", "selected.address_qualifier")
    mma_check = _emit_tcgen_mma_source_check(variant, layout, slots, backend)
    tensor_checks = _emit_tensor_layout_checks(variant, layout, slots, backend)
    matrix = f"&{name}::matrix_topology" if variant.matrix is not None else "nullptr"
    return f"""    case {layout_index}: {{
      const auto availability_check = checker::check_operand_layout_availability(
          {checker_variant}, {layout_index}, context);
{_append_result('availability_check')}
      const auto layout_modifier_check = checker::check_operand_layout_modifiers(
          {checker_variant}, {layout_index}, modifier_values, context);
{_append_result('layout_modifier_check')}
{guard}
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
{cross_checks}
      break;
    }}"""


def _emit_named_rule_check(label: str, expression: str) -> str:
    """Emit one ordered checker call and append its diagnostics."""

    return f"      const auto {label} = {expression};\n{_append_result(label)}\n"


def _emit_tcgen_mma_source_check(variant, layout, slots, backend) -> str:
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
    return f"""      const auto mma_source_check = check_tcgen_mma_sources(
          selected.cta_group, {reference('d')[1:]}, {address}, {shared},
          {reference('b')[1:]}, {reference('idesc')[1:]}, {mask},
          {reference('enable_input_d')[1:]}, {scale}, context);
{_append_result('mma_source_check')}"""


def _emit_tensor_layout_checks(variant, layout, slots, backend) -> str:
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


def _emit_check(entry, variant, variant_index: int, backend) -> str:
    """Emit one final form's checker preserving prior diagnostic order."""

    name = form_name(entry, variant)
    modifier_fields = tuple(
        field for field in variant.modifier_fields
        if field.origin is ResolvedFieldOrigin.MODIFIER
    )
    old_prefix = f"{entry.cpp_name}::{variant.cpp_name}::"
    field_views = ",\n".join(
        emit_check_modifier_view(entry.resolved, variant, field, backend)
        .replace(old_prefix, f"{name}::")
        for field in modifier_fields
    )
    modifier_views = ",\n".join(
        emit_check_modifier_value_view(entry.resolved, variant, field, backend, index)
        .replace(old_prefix, f"{name}::")
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


def _emit_visit(entry, variant, backend) -> str:
    """Visit borrowed reference members after validating mutable layout state."""

    name = form_name(entry, variant)
    slots = operand_slots(variant, backend)
    cases = []
    for index, layout in enumerate(variant.operand_layouts):
        validity = [
            f"{slot.member_name}.has_value() == "
            f"{'true' if index in slot.layout_indices else 'false'}"
            for slot in slots if slot.optional
        ]
        guard = (
            "      if (!(" + " &&\n            ".join(validity) + ")) return;\n"
            if validity else ""
        )
        callbacks = []
        for field in layout.fields:
            if field.value_kind not in REFERENCE_VALUE_KINDS:
                continue
            slot = operand_slot_for_field(slots, field, backend)
            cpp_type = field_value_cpp_type(field, backend=backend)
            member = f"(*{slot.member_name})" if slot.optional else slot.member_name
            callbacks.append(
                f"      observer.{method_name(cpp_type)}({member}.value, "
                f"{member}.locs, {_address_symbol_resolution_policy(field, layout)});"
            )
        cases.append(
            f"    case {index}: {{\n{guard}"
            + "\n".join(callbacks)
            + "\n      return;\n    }"
        )
    body = "\n".join(cases)
    return f"""/** Borrow {name}'s predicate and selected operands in descriptor order. */
void {name}::visit_references(
    ::ptx_frontend::resolved_ir::detail::IReferenceObserver& observer) const {{
  if (execution_predicate)
    observer.predicate(execution_predicate->value, execution_predicate->locs,
        checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);
  switch (operand_layout.value) {{
{body}
    default:
      return;
  }}
}}"""


def _emit_resolve(entry, backend) -> str:
    """Resolve descriptor names once, then dispatch typed form/layout indices."""

    prefix = entry.specification.opcode.replace(".", "_")
    identity_map = "\n".join(
        f'    if (*selected == "{variant.cpp_name}") '
        f'return InstructionKind::{form_name(entry, variant)};'
        for variant in entry.resolved.variants
    )
    clauses = []
    for variant in entry.resolved.variants:
        name = form_name(entry, variant)
        slots = operand_slots(variant, backend)
        modifiers = "\n".join(
            f"      value->{field.name} = resolved_modifier<"
            f"{field_value_cpp_type(field, backend=backend)}>(*fields, "
            f'"{field.name}");'
            for field in variant.modifier_fields
            if field.storage is ResolvedFieldStorage.INSTANCE
        )
        layouts = []
        for index, layout in enumerate(variant.operand_layouts):
            assignments = "\n".join(
                f"        value->{operand_slot_for_field(slots, field, backend).member_name} = "
                f"resolved_operand<{field_value_cpp_type(field, backend=backend)}>("
                f'*fields, "{field.name}");'
                for field in layout.fields
                if field.storage is ResolvedFieldStorage.INSTANCE
            )
            layouts.append(f"""      case {index}:
{assignments}
        return value;""")
        layout_switch = "\n".join(layouts)
        atomic = (
            "      value->address_qualifier = atomic_address_qualifier_from_ast(ast);\n"
            if entry.resolved.atomic_address_qualifier is not None else ""
        )
        clauses.append(f"""    case InstructionKind::{name}: {{
    auto value = std::make_unique<{name}>();
    value->execution_predicate = std::move(fields->execution_predicate);
    value->operand_layout = fields->operand_layout;
{atomic}{modifiers}
    switch (fields->operand_layout.value) {{
{layout_switch}
      default:
        throw ResolveException("Unknown {name} operand layout.");
    }}
    }}""")
    cases = "\n".join(clauses)
    return f"""/** Resolve {entry.resolved.opcode} to one exact final form. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolve{entry.cpp_name}(
    const syntax_ast::AstInstruction& ast, const ResolveContext* context) {{
  const auto selected = select_variant_name(ast, {prefix}_syntax_descriptor());
  if (!selected) return std::unexpected(selected.error());
  const auto kind = [&]() -> InstructionKind {{
{identity_map}
    throw ResolveException("Unknown {entry.cpp_name} descriptor identity.");
  }}();
  auto fields = resolve_fields(ast, {prefix}_syntax_descriptor(),
      {prefix}_resolved_descriptor(), *selected, context);
  if (!fields) return std::unexpected(fields.error());
  switch (kind) {{
{cases}
    default:
      break;
  }}
  throw ResolveException("Unknown {entry.cpp_name} descriptor identity.");
}}"""


def generate_resolved_opcode_source(
    context: GenerationContext, *, category: str, opcode: str, output_path: Path
) -> None:
    """Emit one opcode's descriptors, final methods, and direct resolver."""

    entries = tuple(
        entry for entry in context.entries
        if entry.specification.codegen_category == category
        and entry.specification.opcode == opcode
    )
    if len(entries) != 1:
        raise ValueError(f"expected one {category}/{opcode} entry")
    entry = entries[0]
    instruction = entry.resolved
    backend = context.backend
    prefix = opcode.replace(".", "_")
    shards = form_shards(entry)
    syntax_storage = ("" if shards else emit_syntax_storage(
        from_InstructionSpec(entry.specification), backend, cpp_name=entry.cpp_name
    ))
    resolved_storage = "" if shards else emit_resolved_storage(instruction, backend)
    checker_storage = "" if shards else emit_checker_storage(instruction, backend)
    methods = "" if form_shards(entry) else _emit_form_methods(
        entry, enumerate(instruction.variants), backend
    )
    resolver = _emit_resolve(entry, backend)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor_getters = (
        _emit_sharded_descriptor_getters(entry, len(shards)) if shards else f"""
/** Return static-lifetime syntax selection metadata for {opcode}. */
const check_end::SyntaxInstructionDescriptor& {prefix}_syntax_descriptor() noexcept {{
  return {entry.cpp_name}DescriptorStorage::descriptor;
}}
/** Return static-lifetime resolved field metadata for {opcode}. */
const check_end::ResolvedInstructionDescriptor& {prefix}_resolved_descriptor() noexcept {{
  return generated_detail::{entry.cpp_name}ResolvedDescriptorStorage::descriptor;
}}
/** Return static-lifetime checker metadata for {opcode}. */
const checker::InstructionDescriptor& {prefix}_checker_descriptor() noexcept {{
  return generated_detail::{entry.cpp_name}CheckerDescriptorStorage::descriptor;
}}"""
    )
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#include <cassert>
#include <array>
#include <concepts>
#include <type_traits>
#include <variant>
#include <utility>
#include <{INCLUDE_ROOT}/model/{category}/{opcode}.gen.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_resolution_detail.hpp>

namespace ptx_frontend::resolved_ir {{

using namespace checker;

namespace {{
{syntax_storage}
}}  // namespace

namespace generated_detail {{
{resolved_storage}
{checker_storage}
}}  // namespace generated_detail

{descriptor_getters}

{methods}

{resolver}


}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")


def _emit_sharded_descriptor_getters(entry, shard_count: int) -> str:
    """Assemble immutable contiguous public catalogs from private static shards."""

    prefix = entry.specification.opcode.replace(".", "_")
    count = len(entry.resolved.variants)
    function_names = {
        "syntax": "check_end::SyntaxVariantDescriptor",
        "resolved": "check_end::ResolvedVariantDescriptor",
        "checker": "checker::VariantDescriptor",
    }
    declarations = "\n".join(
        f"std::span<const {type_name}> {prefix}_{domain}_variants_{index:03d}() noexcept;"
        for domain, type_name in function_names.items()
        for index in range(shard_count)
    )
    getters = []
    for domain, type_name in function_names.items():
        root_type = (
            f"check_end::{domain.title()}InstructionDescriptor"
            if domain != "checker" else "checker::InstructionDescriptor"
        )
        if domain == "syntax":
            root_type = "check_end::SyntaxInstructionDescriptor"
            opcode_member = "Opcode_name"
        else:
            opcode_member = "opcode_name"
        spans = ",\n".join(
            f"        generated_detail::{prefix}_{domain}_variants_{index:03d}()"
            for index in range(shard_count)
        )
        getters.append(f"""/** Return one stable contiguous {domain} catalog in canonical form order. */
const {root_type}& {prefix}_{domain}_descriptor() noexcept {{
  static_assert(std::is_trivially_copyable_v<{type_name}>);
  static const std::array<{type_name}, {count}> variants = []() noexcept {{
    std::array<{type_name}, {count}> ordered{{}};
    size_t next = 0;
    for (auto shard : std::array<std::span<const {type_name}>, {shard_count}>{{{{
{spans}
    }}}}) {{
      for (const auto& row : shard) {{
        assert(next < ordered.size());
        ordered[next++] = row;
      }}
    }}
    assert(next == ordered.size());
    return ordered;
  }}();
  static const {root_type} descriptor{{
      .{opcode_member} = "{entry.specification.opcode}", .variants = variants}};
  return descriptor;
}}""")
    return "namespace generated_detail {\n" + declarations + "\n}\n\n" + "\n\n".join(getters)


def generate_resolved_descriptor_shard_source(
    context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path,
) -> None:
    """Emit private immutable descriptor rows for one bounded form slice."""

    entry = next(item for item in context.entries
                 if item.specification.codegen_category == category
                 and item.specification.opcode == opcode)
    indices = form_shards(entry)[shard_index]
    resolved = replace(entry.resolved,
                       variants=tuple(entry.resolved.variants[index] for index in indices))
    full_syntax = from_InstructionSpec(entry.specification)
    syntax = replace(full_syntax,
                     variants=tuple(full_syntax.variants[index] for index in indices))
    syntax_storage = emit_syntax_storage(syntax, context.backend, cpp_name=entry.cpp_name)
    resolved_storage = emit_resolved_storage(resolved, context.backend)
    checker_storage = emit_checker_storage(resolved, context.backend)
    prefix = opcode.replace(".", "_")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#include <array>
#include <span>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_descriptors.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_checker_support.hpp>

namespace ptx_frontend::resolved_ir {{
namespace {{
{syntax_storage}
{resolved_storage}
{checker_storage}
}}  // namespace
namespace generated_detail {{
/** Borrow static syntax rows in canonical shard order. */
std::span<const check_end::SyntaxVariantDescriptor>
{prefix}_syntax_variants_{shard_index:03d}() noexcept {{
  return {entry.cpp_name}DescriptorStorage::variants;
}}
/** Borrow static resolved-field rows in canonical shard order. */
std::span<const check_end::ResolvedVariantDescriptor>
{prefix}_resolved_variants_{shard_index:03d}() noexcept {{
  return {entry.cpp_name}ResolvedDescriptorStorage::variants;
}}
/** Borrow static legality rows in canonical shard order. */
std::span<const checker::VariantDescriptor>
{prefix}_checker_variants_{shard_index:03d}() noexcept {{
  return {entry.cpp_name}CheckerDescriptorStorage::variants;
}}
}}  // namespace generated_detail
}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")


def _emit_form_methods(entry, indexed_variants, backend) -> str:
    """Render each exact class's methods once, retaining global descriptor indexes."""

    return "\n\n".join(
        f"""/** Return {form_name(entry, variant)}'s exact semantic identity. */
InstructionKind {form_name(entry, variant)}::instruction_kind() const noexcept {{
  return kind;
}}
/** Deep-copy the owned {form_name(entry, variant)} record. */
std::unique_ptr<Instruction> {form_name(entry, variant)}::clone() const {{
  return std::make_unique<{form_name(entry, variant)}>(*this);
}}
{_emit_check(entry, variant, index, backend)}
{_emit_visit(entry, variant, backend)}"""
        for index, variant in indexed_variants
    )


def generate_resolved_form_shard_source(
    context: GenerationContext, *, category: str, opcode: str,
    shard_index: int, output_path: Path,
) -> None:
    """Emit bounded methods for one canonical form slice without descriptors."""

    entry = next(
        item for item in context.entries
        if item.specification.codegen_category == category
        and item.specification.opcode == opcode
    )
    indices = form_shards(entry)[shard_index]
    prefix = opcode.replace(".", "_").replace("-", "_")
    methods = _emit_form_methods(
        entry,
        ((index, entry.resolved.variants[index]) for index in indices),
        context.backend,
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#include <array>
#include <concepts>
#include <type_traits>
#include <variant>
#include <utility>
#include <{INCLUDE_ROOT}/model/{category}/{opcode}_forms_{shard_index:03d}.gen.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_resolution_detail.hpp>

namespace ptx_frontend::resolved_ir {{

using namespace checker;

/** Borrow the opcode's contiguous resolved descriptor catalog. */
const check_end::ResolvedInstructionDescriptor&
{prefix}_resolved_descriptor() noexcept;
/** Borrow the opcode's contiguous checker descriptor catalog. */
const checker::InstructionDescriptor&
{prefix}_checker_descriptor() noexcept;

{methods}

}}  // namespace ptx_frontend::resolved_ir
""", encoding="utf-8")

def _emit_cross_rule_checks(
    instruction: ResolvedInstruction,
    variant: ResolvedVariant,
    checker_variant_expr: str,
) -> str:
    """Emit a variant's cross-rule checks in a fixed order."""

    checks = ""
    if instruction.atomic_address_qualifier is not None:
        checks += f"""            const auto atomic_check = check_atomic_qualifiers(
                {checker_variant_expr}.atomic_address_qualifier,
                instruction.address_qualifier, fields, operands, context);
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
