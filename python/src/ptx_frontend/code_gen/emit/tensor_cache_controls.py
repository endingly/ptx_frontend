"""Emit the owned tensor cache query for exact direct instruction classes."""

from __future__ import annotations

from pathlib import Path

from ptx_frontend.code_gen.context import GenerationContext


def render_tensor_cache_controls_query(context: GenerationContext) -> str:
    """Render selected-form dispatch without decoding opaque tensor-map bytes."""

    cp = next(entry for entry in context.entries
              if entry.specification.codegen_category == "data_movement"
              and entry.specification.opcode == "cp")
    plain: list[str] = []
    hinted: list[str] = []
    for variant in cp.resolved.variants:
        if variant.tensor_access_mode is None:
            continue
        name = cp.cpp_name + variant.cpp_name
        if not any(field.name == "cache_hint"
                   for field in variant.modifier_fields):
            plain.append(f"    case InstructionKind::{name}:")
            continue
        policy_layouts = [index for index, layout in enumerate(variant.operand_layouts)
                          if any(field.name == "cache_policy" for field in layout.fields)]
        if not policy_layouts or len(policy_layouts) * 2 != len(variant.operand_layouts):
            raise ValueError(f"{variant.variant_id}: incomplete tensor policy layouts")
        cases = "\n".join(f"        case {index}: expected_policy = true; break;"
                          for index in policy_layouts)
        others = "\n".join(f"        case {index}: break;"
                           for index in range(len(variant.operand_layouts))
                           if index not in policy_layouts)
        hinted.append(f"""    case InstructionKind::{name}: {{
      result.applicable = true;
      const auto* selected = dynamic_cast<const {name}*>(&instruction);
      if (!selected) {{
        result.diagnostics.emplace_back("tensor cache identity and payload disagree");
        break;
      }}
      result.hint = selected->cache_hint;
      bool expected_policy = false;
      switch (selected->operand_layout.value) {{
{cases}
{others}
        default:
          result.diagnostics.emplace_back("tensor cache layout tag is out of range");
          break;
      }}
      if (selected->cache_policy.has_value() != expected_policy)
        result.diagnostics.emplace_back("tensor cache layout and policy disagree");
      if (selected->cache_policy) result.policy = *selected->cache_policy;
      break;
    }}""")
    return "\n".join([
        "// Generated from canonical selected direct Cp forms.",
        "#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>",
        "#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>",
        "#include <ptx_frontend/resolved_ir/ptx_tensor_cache_controls.hpp>",
        "namespace ptx_frontend::resolved_ir {",
        "/** Copy selected controls and diagnose inconsistent owned payloads. */",
        "TensorCacheControlsReport query_tensor_cache_controls(",
        "    const Instruction& instruction) {",
        "  TensorCacheControlsReport result;",
        "  switch (instruction.instruction_kind()) {",
        *hinted,
        *plain,
        "      result.applicable = true;",
        "      break;",
        "    default: break;",
        "  }",
        "  const checker::Context context{};",
        "  if (result.hint) {",
        "    auto checked = checker::check_tensor_cache_hint(*result.hint, context);",
        "    if (!checked) for (const auto& diagnostic : checked.error())",
        "      result.diagnostics.push_back(diagnostic.message);",
        "  }",
        "  if (result.policy) {",
        "    auto checked = checker::check_tensor_cache_policy(*result.policy, context);",
        "    if (!checked) for (const auto& diagnostic : checked.error())",
        "      result.diagnostics.push_back(diagnostic.message);",
        "  }",
        "  return result;",
        "}",
        "}  // namespace ptx_frontend::resolved_ir",
        "",
    ])


def generate_tensor_cache_controls_query(
    context: GenerationContext, *, output_path: Path,
) -> None:
    """Write the private query through the standard global artifact plan."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_tensor_cache_controls_query(context), encoding="utf-8")
