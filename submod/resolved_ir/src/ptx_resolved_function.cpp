#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

#include <utility>

namespace ptx_frontend::resolved_ir {

/** Deep-copy every owned body object while preserving null slots for validation. */
ResolvedFunction::ResolvedFunction(const ResolvedFunction& other)
    : symbol_id(other.symbol_id),
      name(other.name),
      is_entry(other.is_entry),
      is_prototype(other.is_prototype),
      contract(other.contract),
      branch_target_sets(other.branch_target_sets),
      call_target_sets(other.call_target_sets),
      call_prototypes(other.call_prototypes),
      label_positions(other.label_positions),
      range(other.range),
      parameter_declarations(other.parameter_declarations),
      declaration_scope(other.declaration_scope),
      instruction_ranges(other.instruction_ranges),
      instruction_opcodes(other.instruction_opcodes),
      source_target(other.source_target),
      source_version(other.source_version),
      source_region(other.source_region),
      source_identity(other.source_identity) {
  body.reserve(other.body.size());
  for (const auto& instruction : other.body)
    body.push_back(instruction ? instruction->clone() : nullptr);
}

/** Commit a fully cloned replacement only after all copies succeed. */
ResolvedFunction& ResolvedFunction::operator=(const ResolvedFunction& other) {
  if (this != &other) {
    ResolvedFunction replacement(other);
    *this = std::move(replacement);
  }
  return *this;
}

}  // namespace ptx_frontend::resolved_ir
