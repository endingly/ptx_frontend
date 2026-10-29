#pragma once

#include <cstdint>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <span>

namespace ptx_frontend::resolved_ir::detail {

/** Sorted spelling row; known includes canonical Absent slots independently of candidates. */
struct ModifierCandidateRow {
  /** Static spelling borrowed from the generated catalog. */
  std::string_view spelling;
  /** Legacy canonical known-spelling status, including Absent slots. */
  bool known;
};

/** Immutable generated index, borrowed for one call; words are row-major form bitsets. */
struct ModifierCandidatePlan {
  /** Sorted rows and their row-major masks have static lifetime. */
  std::span<const ModifierCandidateRow> rows;
  std::span<const uint64_t> words;
};

/** Return the canonical ordinal, preserving general errors; scratch has ceil(forms/64) words. */
std::expected<size_t, ResolveDiagnostic> select_indexed_variant(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction,
    const ModifierCandidatePlan& plan, std::span<uint64_t> candidates);

/** Resolve using already selected static descriptors; bind the winner again. */
std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_selected_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxVariantDescriptor& syntax_variant,
    const check_end::ResolvedVariantDescriptor& resolved_variant,
    const ResolveContext* context, NamedArrayAddressPolicy named_array_policy);

}  // namespace ptx_frontend::resolved_ir::detail
