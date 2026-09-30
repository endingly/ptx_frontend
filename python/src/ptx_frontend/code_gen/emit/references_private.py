"""Generate bounded private matrix reference traversal implementations."""

from __future__ import annotations

from pathlib import Path

from ptx_frontend.base.utils import generated_at_comment
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.matrix_storage import matrix_storage_plan
from ptx_frontend.code_gen.emit.references import _address_symbol_resolution_policy
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, ResolvedOperandLayout
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind


# A shard bounds each C++ translation unit's variant visitor instantiation.
MATRIX_REFERENCE_SHARD_SIZE = 64

_REFERENCE_KINDS = {
    ResolvedValueKind.PREDICATE: "Predicate",
    ResolvedValueKind.REGISTER_VECTOR: "RegisterVector",
    ResolvedValueKind.REGISTER: "Register",
    ResolvedValueKind.MATRIX_SCALE_SELECTOR: "ScaleSelector",
    ResolvedValueKind.SHARED_MATRIX_DESCRIPTOR: "SharedMatrixDescriptor",
    ResolvedValueKind.WGMMA_SCALE_D: "PredicateSource",
    ResolvedValueKind.ADDRESS: "Address",
    ResolvedValueKind.REG_OR_IMM: "RegisterOrImmediate",
}


def _instruction(context: GenerationContext, opcode: str) -> ResolvedInstruction:
    """Select the one matrix opcode model from a category-local snapshot."""

    matches = [entry.resolved for entry in context.entries
               if entry.specification.codegen_category == "matrix"
               and entry.specification.opcode == opcode]
    if len(matches) != 1:
        raise ValueError(f"expected one matrix opcode {opcode!r}")
    return matches[0]


def matrix_reference_shard_count(instruction: ResolvedInstruction) -> int:
    """Return the deterministic shard count for a nonempty variant vector."""

    if not instruction.variants:
        raise ValueError("matrix reference traversal requires variants")
    return (len(instruction.variants) + MATRIX_REFERENCE_SHARD_SIZE - 1) // MATRIX_REFERENCE_SHARD_SIZE


def _emit_fields(layout: ResolvedOperandLayout, selected: str) -> str:
    """Emit exact field callbacks with canonical address resolution policy."""

    lines: list[str] = []
    for field in layout.fields:
        if field.value_kind not in _REFERENCE_KINDS:
            if field.value_kind in {
                ResolvedValueKind.IMMEDIATE,
                ResolvedValueKind.BOOL,
                ResolvedValueKind.SCALAR_TYPE,
            }:
                continue
            # Every matrix reference-bearing payload must have an explicit
            # bridge identity; avoid silently dropping a future field kind.
            from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
            if field.value_kind in REFERENCE_VALUE_KINDS:
                raise ValueError(f"unsupported matrix reference payload {field.value_kind!r}")
            continue
        kind = _REFERENCE_KINDS[field.value_kind]
        policy = _address_symbol_resolution_policy(field, layout)
        lines.append(
            "      callback(MatrixReferenceView{"
            f".kind = MatrixReferenceKind::{kind}, "
            f".value = &{selected}.{field.name}.value, "
            f".locations = {selected}.{field.name}.locs, "
            f".address_resolution_policy = {policy}"
            "}, context);"
        )
    return "\n".join(lines)


def generate_matrix_reference_shard(
    context: GenerationContext, *, opcode: str, shard: int, output_path: Path
) -> None:
    """Write one bounded variant-index switch for a matrix opcode."""

    instruction = _instruction(context, opcode)
    storage = matrix_storage_plan(instruction, context.backend)
    if storage is None:
        raise ValueError(f"matrix opcode {opcode!r} lacks physical storage mapping")
    count = matrix_reference_shard_count(instruction)
    if shard < 0 or shard >= count:
        raise ValueError(f"matrix shard {shard} is outside {count} shards")
    first = shard * MATRIX_REFERENCE_SHARD_SIZE
    last = min(first + MATRIX_REFERENCE_SHARD_SIZE, len(instruction.variants))
    cases: list[str] = []
    for index in range(first, last):
        variant = instruction.variants[index]
        if len(variant.operand_layouts) == 1:
            body = _emit_fields(variant.operand_layouts[0], "selected")
        else:
            layouts = []
            for layout_index, layout in enumerate(variant.operand_layouts):
                fields = _emit_fields(layout, "payload")
                layouts.append(
                    f"      case {layout_index}: {{\n"
                    f"        const auto& payload = std::get<{layout_index}>(selected.operands);\n"
                    f"{fields}\n        break;\n      }}"
                )
            body = (
                "      switch (selected.operands.index()) {\n"
                + "\n".join(layouts)
                + "\n      default: throw std::bad_variant_access{};\n      }"
            )
        cases.append(
            f"    case {index}: {{\n"
            f"      const auto& selected = std::get<{storage.storage_indexes[index]}>(instruction.variant);\n"
            f"{body}\n      break;\n    }}"
        )
    source = f"""// Generated by python/scripts/gen_all.py. Do not edit.
{generated_at_comment()}

#include <ptx_frontend/resolved_ir/model/matrix/{opcode}/model.gen.hpp>
#include "ptx_module_matrix_references.hpp"

#include <variant>

namespace ptx_frontend::resolved_ir::detail {{

/** Visit selected reference payloads for a bounded matrix variant interval. */
void visit_{opcode}_matrix_references_shard_{shard:03d}(
    const {instruction.cpp_name}& instruction,
    MatrixReferenceCallback callback, void* context) {{
  const auto logical = instruction.matrix_logical_index();
  if (!logical)
    throw std::bad_variant_access{{}};
  switch (*logical) {{
{chr(10).join(cases)}
    default: throw std::bad_variant_access{{}};
  }}
}}

}}  // namespace ptx_frontend::resolved_ir::detail
"""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(source, encoding="utf-8")


def generate_matrix_reference_dispatcher(
    context: GenerationContext, *, category: str, opcode: str, output_path: Path
) -> None:
    """Write one predicate-once dispatcher over bounded matrix shards."""

    if category != "matrix":
        raise ValueError("matrix reference dispatcher requires matrix category")
    instruction = _instruction(context, opcode)
    count = matrix_reference_shard_count(instruction)
    declarations = "\n".join(
        f"void visit_{opcode}_matrix_references_shard_{index:03d}("
        f"const {instruction.cpp_name}&, MatrixReferenceCallback, void*);"
        for index in range(count)
    )
    cases = "\n".join(
        f"    case {index}: visit_{opcode}_matrix_references_shard_{index:03d}("
        "instruction, callback, context); break;"
        for index in range(count)
    )
    source = f"""// Generated by python/scripts/gen_all.py. Do not edit.
{generated_at_comment()}

#include <ptx_frontend/resolved_ir/model/matrix/{opcode}/model.gen.hpp>
#include "ptx_module_matrix_references.hpp"

#include <variant>

namespace ptx_frontend::resolved_ir::detail {{

{declarations}

/** Visit an owned matrix predicate once, then its exact selected shard. */
void visit_matrix_references(const {instruction.cpp_name}& instruction,
                             MatrixReferenceCallback callback, void* context) {{
  const auto logical = instruction.matrix_logical_index();
  if (!logical)
    throw std::bad_variant_access{{}};
  if (instruction.execution_predicate) {{
    callback(MatrixReferenceView{{
        .kind = MatrixReferenceKind::Predicate,
        .value = &instruction.execution_predicate->value,
        .locations = instruction.execution_predicate->locs,
        .address_resolution_policy =
            checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace,
    }}, context);
  }}
  switch (*logical / {MATRIX_REFERENCE_SHARD_SIZE}) {{
{cases}
    default: throw std::bad_variant_access{{}};
  }}
}}

}}  // namespace ptx_frontend::resolved_ir::detail
"""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(source, encoding="utf-8")
