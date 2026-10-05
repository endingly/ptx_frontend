"""Emit direct-form resolvers, checkers, visitors, and descriptor definitions."""

from __future__ import annotations

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
from ptx_frontend.code_gen.emit.resolved_model import INCLUDE_ROOT, form_name, method_name
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
)
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.spec.model import SemanticRule


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
          context);
{_append_result('operand_check')}
{cross_checks}
      break;
    }}"""


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


def _emit_cp_control_helper(entry, backend) -> str:
    """Emit a private, non-template exact Cp control-register traversal."""

    if entry.resolved.opcode != "cp":
        return ""
    cases = []
    for variant in entry.resolved.variants:
        name = form_name(entry, variant)
        slots = operand_slots(variant, backend)
        layouts = []
        for index, layout in enumerate(variant.operand_layouts):
            validity = [
                f"selected.{slot.member_name}.has_value() == "
                f"{'true' if index in slot.layout_indices else 'false'}"
                for slot in slots if slot.optional
            ]
            guard = (
                "        if (!(" + " &&\n              ".join(validity) + ")) return;\n"
                if validity else ""
            )
            callbacks = []
            for field in layout.fields:
                if field.name not in {"source_control", "cache_policy"}:
                    continue
                slot = operand_slot_for_field(slots, field, backend)
                member = (
                    f"(*selected.{slot.member_name})" if slot.optional
                    else f"selected.{slot.member_name}"
                )
                if field.value_kind is ResolvedValueKind.CP_ASYNC_SOURCE_CONTROL:
                    callbacks.append(f"""        std::visit([&](const auto& control) {{
          using T = std::remove_cvref_t<decltype(control)>;
          if constexpr (std::same_as<T, ResolvedRegisterRef>) {{
            observer.reg(control, {member}.locs,
                checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);
          }} else if constexpr (std::same_as<T, ResolvedPredicate> ||
                               std::same_as<T, ResolvedCpAsyncCachePolicy>) {{
            observer.reg(control.register_ref, {member}.locs,
                checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);
          }}
        }}, {member}.value);""")
                else:
                    callbacks.append(f"""        observer.reg({member}.value, {member}.locs,
            checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);""")
            body = "\n".join(callbacks)
            layouts.append(f"""      case {index}: {{
{guard}{body}
        return;
      }}""")
        layout_cases = "\n".join(layouts)
        cases.append(f"""    case InstructionKind::{name}: {{
      const auto& selected = dynamic_cast<const {name}&>(instruction);
      switch (selected.operand_layout.value) {{
{layout_cases}
        default:
          return;
      }}
    }}""")
    branch = "\n".join(cases)
    return f"""namespace detail {{
/** Borrow designated Cp control registers only after direct-layout validation. */
void visit_cp_control_registers(const Instruction& instruction,
                                IReferenceObserver& observer) {{
  switch (instruction.instruction_kind()) {{
{branch}
    default:
      return;
  }}
}}
}}  // namespace detail"""


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
    syntax_storage = emit_syntax_storage(
        from_InstructionSpec(entry.specification), backend, cpp_name=entry.cpp_name
    )
    resolved_storage = emit_resolved_storage(instruction, backend)
    checker_storage = emit_checker_storage(instruction, backend)
    methods = "\n\n".join(
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
        for index, variant in enumerate(instruction.variants)
    )
    resolver = _emit_resolve(entry, backend)
    cp_helper = _emit_cp_control_helper(entry, backend)
    cp_include = '#include "ptx_cp_control.hpp"\n' if opcode == "cp" else ""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#include <array>
#include <concepts>
#include <type_traits>
#include <variant>
#include <utility>
#include <{INCLUDE_ROOT}/model/{category}/{opcode}.gen.hpp>
#include <{INCLUDE_ROOT}/ptx_resolved_ir_resolution_detail.hpp>
{cp_include}

namespace ptx_frontend::resolved_ir {{

using namespace checker;

namespace {{
{syntax_storage}
}}  // namespace

/** Return static-lifetime syntax selection metadata for {opcode}. */
const check_end::SyntaxInstructionDescriptor& {prefix}_syntax_descriptor() noexcept {{
  return {entry.cpp_name}DescriptorStorage::descriptor;
}}

namespace generated_detail {{
{resolved_storage}
{checker_storage}
}}  // namespace generated_detail

/** Return static-lifetime resolved field metadata for {opcode}. */
const check_end::ResolvedInstructionDescriptor& {prefix}_resolved_descriptor() noexcept {{
  return generated_detail::{entry.cpp_name}ResolvedDescriptorStorage::descriptor;
}}
/** Return static-lifetime checker metadata for {opcode}. */
const checker::InstructionDescriptor& {prefix}_checker_descriptor() noexcept {{
  return generated_detail::{entry.cpp_name}CheckerDescriptorStorage::descriptor;
}}

{methods}

{resolver}

{cp_helper}

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
    return checks
