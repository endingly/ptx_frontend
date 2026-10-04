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
    if binding is None:
        raise ValueError(
            f"layout {layout.layout_id!r} is missing a binding for "
            f"MOV_SOURCE field {field.name!r}"
        )
    if binding.preserve_parameter_address_space:
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
    """Emit index-based operand visitation in canonical variant order."""

    variant_cases: list[str] = []
    for variant_index, variant in enumerate(instruction.variants):
        if len(variant.operand_layouts) == 1:
            body = _emit_reference_fields(variant.operand_layouts[0], "selected")
        else:
            layouts = "\n".join(
                f"      case {layout_index}: {{\n"
                f"        const auto& payload = std::get<{layout_index}>(selected.operands);\n"
                f"{_emit_reference_fields(layout, 'payload')}\n"
                "        break;\n      }"
                for layout_index, layout in enumerate(variant.operand_layouts)
            )
            body = f"""      switch (selected.operands.index()) {{
{layouts}
      default:
        throw std::bad_variant_access{{}};
      }}"""
        variant_cases.append(
            f"    case {variant_index}: {{\n"
            f"      const auto& selected = std::get<{variant_index}>(value.variant);\n"
            f"{body}\n      break;\n    }}"
        )
    visitor_requirements = " &&\n         ".join(
        "std::invocable<Visitor&, const "
        f"{payload}&, std::span<const SourceRange>, checker::AddressSymbolResolutionPolicy>"
        for payload in _reference_payload_types(instruction, backend)
    )
    cases = "\n".join(variant_cases)
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
  const auto dispatch = [&]<typename Instruction>(const Instruction& value)
      requires std::same_as<Instruction, {instruction.cpp_name}> {{
  switch (value.variant.index()) {{
{cases}
    default:
      throw std::bad_variant_access{{}};
  }}
  }};
  dispatch(instruction);
}}"""
