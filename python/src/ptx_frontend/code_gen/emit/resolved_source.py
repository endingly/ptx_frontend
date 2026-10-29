"""Assemble sources and render resolvers, construction, and reference visits."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.emit.checker_descriptors import (
    _emit_instruction_descriptor_storage as emit_checker_storage,
)
from ptx_frontend.code_gen.emit.resolved_checks import emit_check, named_array_policy
from ptx_frontend.code_gen.emit.resolved_descriptors import (
    _emit_resolved_descriptor_storage as emit_resolved_storage,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import (
    _emit_instruction_descriptor_storage as emit_syntax_storage,
    _cpp_string,
)
from ptx_frontend.code_gen.artifact_layout import INCLUDE_ROOT, form_shards
from ptx_frontend.code_gen.cpp_names import form_name, method_name
from ptx_frontend.code_gen.selection_index import modifier_candidate_rows
from ptx_frontend.code_gen.resolved_layout import operand_slot_for_field, operand_slots
from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldStorage,
    ResolvedOperandLayout,
    ResolvedValueKind,
)
from ptx_frontend.ir.syntax_ast import from_InstructionSpec


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


def _emit_construction_cases(entry, variants, backend, *, borrowed_fields: bool) -> str:
    """Build exact-form cases using the caller's scratch-field access mode."""

    field_access = "fields." if borrowed_fields else "fields->"
    field_argument = "fields" if borrowed_fields else "*fields"
    clauses = []
    for variant in variants:
        name = form_name(entry, variant)
        slots = operand_slots(variant, backend)
        modifiers = "\n".join(
            f"      value->{field.name} = resolved_modifier<"
            f"{field_value_cpp_type(field, backend=backend)}>({field_argument}, "
            f'"{field.name}");'
            for field in variant.modifier_fields
            if field.storage is ResolvedFieldStorage.INSTANCE
        )
        layouts = []
        for index, layout in enumerate(variant.operand_layouts):
            assignments = "\n".join(
                f"        value->{operand_slot_for_field(slots, field, backend).member_name} = "
                f"resolved_operand<{field_value_cpp_type(field, backend=backend)}>("
                f'{field_argument}, "{field.name}");'
                for field in layout.fields
                if field.storage is ResolvedFieldStorage.INSTANCE
            )
            operand_names = ", ".join(field.name for field in layout.fields) or "(none)"
            layouts.append(f"""      case {index}:
        // Operands: {operand_names}
{assignments}
        return value;""")
        layout_switch = "\n".join(layouts)
        atomic = (
            "      value->address_qualifier = atomic_address_qualifier_from_ast(ast);\n"
            if entry.resolved.atomic_address_qualifier is not None else ""
        )
        clauses.append(f"""    case {name}::kind: {{
    auto value = std::make_unique<{name}>();
    value->execution_predicate = std::move({field_access}execution_predicate);
    value->operand_layout = {field_access}operand_layout;
{atomic}{modifiers}
    switch ({field_access}operand_layout.value) {{
{layout_switch}
      default:
        throw ResolveException("Unknown {name} operand layout.");
    }}
    }}""")
    return "\n".join(clauses)


def _wgmma_construction_helper_name(shard_index: int) -> str:
    """Name one private WGMMA construction helper by canonical physical shard."""

    return f"wgmma_construct_{shard_index:03d}"


def _emit_wgmma_construction_helper(entry, shard_index: int, backend) -> str:
    """Emit one exact-kind builder over an existing WGMMA methods shard."""

    if entry.resolved.atomic_address_qualifier is not None:
        raise ValueError("WGMMA shard construction cannot access an AST qualifier")
    variants = tuple(entry.resolved.variants[index]
                     for index in form_shards(entry)[shard_index])
    cases = _emit_construction_cases(
        entry, variants, backend, borrowed_fields=True
    )
    helper = _wgmma_construction_helper_name(shard_index)
    return f"""/** Construct an exact WGMMA form owned by this physical methods shard. */
std::unique_ptr<Instruction> {helper}(
    InstructionKind kind, ResolvedInstructionFields& fields) {{
  switch (kind) {{
{cases}
    default:
      throw ResolveException("Unknown {entry.cpp_name} descriptor identity.");
  }}
}}"""


def _emit_wgmma_construction_declarations(entry) -> str:
    """Declare private shard builders at the WGMMA entrypoint only."""

    declarations = "\n".join(
        f"/** Construct one exact form from borrowed WGMMA scratch fields. */\n"
        f"std::unique_ptr<Instruction> {_wgmma_construction_helper_name(index)}("
        "InstructionKind kind, ResolvedInstructionFields& fields);"
        for index, _ in enumerate(form_shards(entry))
    )
    return f"namespace generated_detail {{\n{declarations}\n}}\n\n"


def _emit_resolve(entry, backend, context) -> str:
    """Emit conservative selection and dispatch directly through typed static catalogs."""

    prefix = entry.specification.opcode.replace(".", "_")
    syntax = from_InstructionSpec(entry.specification)
    if len(syntax.variants) != len(entry.resolved.variants) or any(
        syntax_variant.variant_id != variant.variant_id
        for syntax_variant, variant in zip(syntax.variants, entry.resolved.variants, strict=True)
    ):
        raise ValueError("selection syntax/resolved catalog order mismatch")
    rows = modifier_candidate_rows(syntax)
    word_count = (len(syntax.variants) + 63) // 64
    row_entries = ",\n".join(
        f'    ::ptx_frontend::resolved_ir::detail::ModifierCandidateRow{{{_cpp_string(spelling)}, {str(known).lower()}}}'
        for spelling, known, _ in rows
    )
    words = ",\n".join(
        f"    0x{(mask >> (word * 64)) & ((1 << 64) - 1):016x}ULL"
        for _, _, mask in rows for word in range(word_count)
    )
    ordinals = ",\n".join(
        f"    {variant.cpp_name}" for variant in entry.resolved.variants
    )
    kinds = ",\n".join(
        f"    {form_name(entry, variant)}::kind" for variant in entry.resolved.variants
    )
    identity_checks = "\n".join(
        f"static_assert(static_cast<uint32_t>({form_name(entry, variant)}::kind) == "
        f"{context.identities.form_value(entry, variant)}u);"
        for variant in entry.resolved.variants
    )
    selection_storage = f"""namespace {{
/** Canonical opcode-local ordinals, independent of public kind encoding. */
enum class SelectedFormOrdinal : uint16_t {{
{ordinals}
}};
/** Selection borrows immutable catalogs with process lifetime. */
struct SelectedForm {{
  /** Canonical opcode-local form identity. */
  SelectedFormOrdinal ordinal;
  /** Public identity from the context-validated form table. */
  InstructionKind kind;
  /** Borrowed immutable descriptors; both catalogs have process lifetime. */
  const check_end::SyntaxVariantDescriptor& syntax;
  const check_end::ResolvedVariantDescriptor& resolved;
}};
{identity_checks}
constexpr std::array<InstructionKind, {len(syntax.variants)}> selected_kinds = {{{{
{kinds}
}}}};
constexpr std::array<::ptx_frontend::resolved_ir::detail::ModifierCandidateRow, {len(rows)}> candidate_rows = {{{{
{row_entries}
}}}};
constexpr std::array<uint64_t, {len(rows) * word_count}> candidate_words = {{{{
{words}
}}}};
constexpr ::ptx_frontend::resolved_ir::detail::ModifierCandidatePlan candidate_plan{{candidate_rows, candidate_words}};
/** Select and retain direct static descriptor references for construction. */
std::expected<SelectedForm, ResolveDiagnostic> select_form(
    const syntax_ast::AstInstruction& ast) {{
  const auto& syntax = {prefix}_syntax_descriptor();
  std::array<uint64_t, {word_count}> candidates;
  const auto selected = ::ptx_frontend::resolved_ir::detail::select_indexed_variant(ast, syntax, candidate_plan, candidates);
  if (!selected) return std::unexpected(selected.error());
  assert(syntax.variants.size() == selected_kinds.size());
  const auto& resolved = {prefix}_resolved_descriptor();
  assert(resolved.variants.size() == selected_kinds.size());
  return SelectedForm{{static_cast<SelectedFormOrdinal>(*selected),
      selected_kinds[*selected], syntax.variants[*selected], resolved.variants[*selected]}};
}}
}}  // namespace

"""
    if entry.specification.opcode == "wgmma" and form_shards(entry):
        cases = "\n".join(
            f"    case {form_name(entry, entry.resolved.variants[index])}::kind: "
            f"return generated_detail::{_wgmma_construction_helper_name(shard_index)}(kind, *fields);"
            for shard_index, indices in enumerate(form_shards(entry))
            for index in indices
        )
    else:
        cases = _emit_construction_cases(
            entry, entry.resolved.variants, backend, borrowed_fields=False
        )
    named_policies = "\n".join(
        f"    case {form_name(entry, variant)}::kind: return {named_array_policy(entry, variant)};"
        for variant in entry.resolved.variants
    )
    return selection_storage + f"""/** Resolve {entry.resolved.opcode} to one exact final form. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolve{entry.cpp_name}(
    const syntax_ast::AstInstruction& ast, const ResolveContext* context) {{
  const auto selected = select_form(ast);
  if (!selected) return std::unexpected(selected.error());
  const auto kind = selected->kind;
  const auto named_array_policy = [&]() {{
    switch (kind) {{
{named_policies}
      default: return NamedArrayAddressPolicy::Reject;
    }}
  }}();
  auto fields = ::ptx_frontend::resolved_ir::detail::resolve_selected_fields(ast, selected->syntax,
      selected->resolved, context, named_array_policy);
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
    resolver = (
        _emit_wgmma_construction_declarations(entry)
        if opcode == "wgmma" and shards else ""
    ) + _emit_resolve(entry, backend, context)
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
#include "ptx_resolved_ir_selection_private.hpp"

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
{emit_check(entry, variant, index, backend)}
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
    if opcode == "wgmma":
        methods += (
            "\n\nnamespace generated_detail {\n"
            + _emit_wgmma_construction_helper(entry, shard_index, context.backend)
            + "\n}  // namespace generated_detail"
        )
    support_headers = (
        f"#include <{INCLUDE_ROOT}/ptx_resolved_ir_resolution_detail.hpp>"
        if opcode == "wgmma" else
        f"#include <{INCLUDE_ROOT}/ptx_resolved_ir_checker_support.hpp>\n"
        f"#include <{INCLUDE_ROOT}/ptx_resolved_ir_descriptors.hpp>"
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(f"""// Generated by ptx_frontend resolved IR code generation. Do not edit.
#include <array>
#include <concepts>
#include <type_traits>
#include <variant>
#include <utility>
#include <{INCLUDE_ROOT}/model/{category}/{opcode}_forms_{shard_index:03d}.gen.hpp>
{support_headers}

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
