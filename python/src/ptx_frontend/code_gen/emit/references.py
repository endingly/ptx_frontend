"""Generate and validate resolved-instruction module-reference visitors."""

from __future__ import annotations

from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
from ptx_frontend.code_gen.reference_policy import (
    REFERENCE_VALUE_KINDS,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedInstruction,
    ResolvedOperandLayout,
    ResolvedValueKind,
)
from ptx_frontend.spec.model import CodegenUnit




def _address_symbol_resolution_policy(
    field: ResolvedField, layout: ResolvedOperandLayout
) -> str:
    """Return the immutable binding policy for one reference payload."""

    if field.value_kind is not ResolvedValueKind.MOV_SOURCE:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    binding = next(
        (binding for binding in layout.bindings
         if binding.target_field_id == field.name),
        None,
    )
    if binding is None or binding.preserve_parameter_address_space:
        return "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace"
    return "checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter"


def _emit_reference_fields(layout: ResolvedOperandLayout, object_name: str) -> str:
    """Emit callbacks for only explicitly reference-bearing operand payloads."""

    return "\n".join(
        f"      visitor({object_name}.{field.name}.value, "
        f"{object_name}.{field.name}.locs, "
        f"{_address_symbol_resolution_policy(field, layout)});"
        for field in layout.fields
        if field.value_kind in REFERENCE_VALUE_KINDS
    )


def _reference_payload_types(
    instruction: ResolvedInstruction, backend: CodegenUnit
) -> tuple[str, ...]:
    """Return reference-bearing payload types in deterministic visitor order."""

    payloads = ["ResolvedPredicate"]
    for variant in instruction.variants:
        for layout in variant.operand_layouts:
            for field in layout.fields:
                field_type = field_value_cpp_type(field, backend=backend)
                if field.value_kind in REFERENCE_VALUE_KINDS and field_type not in payloads:
                    payloads.append(field_type)
    return tuple(payloads)


def emit_reference_visitor(instruction: ResolvedInstruction, backend: CodegenUnit) -> str:
    """Emit typed operand visitation without a hand-maintained opcode switch."""

    variant_cases: list[str] = []
    for variant in instruction.variants:
        if len(variant.operand_layouts) == 1:
            body = _emit_reference_fields(variant.operand_layouts[0], "selected")
        else:
            layouts = "\n".join(
                f"        if constexpr (std::same_as<Payload, {instruction.cpp_name}::{variant.cpp_name}::{layout.cpp_name}Operands>) {{\n"
                f"{_emit_reference_fields(layout, 'payload')}\n        }}"
                for layout in variant.operand_layouts
            )
            body = f"""      std::visit([&]<typename Payload>(const Payload& payload) {{
{layouts}
      }}, selected.operands);"""
        variant_cases.append(
            f"    if constexpr (std::same_as<Variant, {instruction.cpp_name}::{variant.cpp_name}>) {{\n{body}\n    }}"
        )
    visitor_requirements = " &&\n         ".join(
        "std::invocable<Visitor&, const "
        f"{payload}&, std::span<const SourceRange>, checker::AddressSymbolResolutionPolicy>"
        for payload in _reference_payload_types(instruction, backend)
    )
    return f"""/**
 * Visit every binding-bearing operand selected by this resolved instruction.
 *
 * ``Visitor`` accepts every generated payload listed in the corresponding
 * operand layouts as ``(const Payload&, std::span<const SourceRange>,
 * checker::AddressSymbolResolutionPolicy)``.
 */
template <typename Visitor>
  requires ({visitor_requirements})
void visit_instruction_references(const {instruction.cpp_name}& instruction,
                                  Visitor&& visitor) {{
  if (instruction.execution_predicate)
    visitor(instruction.execution_predicate->value, instruction.execution_predicate->locs,
            checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);
  std::visit([&]<typename Variant>(const Variant& selected) {{
{" else ".join(variant_cases)}
  }}, instruction.variant);
}}"""
