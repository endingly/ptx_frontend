#pragma once

#include <algorithm>
#include <map>
#include <vector>
#include "ptx_resolved_ir_selection_private.hpp"

namespace ptx_frontend::resolved_ir::test_helpers {
/** Independently derive an owned runtime index for differential tests and benchmarks. */
struct OwnedCandidatePlan {
  /** Owned rows borrow spelling literals from the descriptor. */
  std::vector<detail::ModifierCandidateRow> rows;
  /** Owned row-major masks, one bit per canonical form. */
  std::vector<uint64_t> words;
  /** Build the union from public descriptors without the Python emitter. */
  explicit OwnedCandidatePlan(
      const check_end::SyntaxInstructionDescriptor& descriptor) {
    const size_t count = (descriptor.variants.size() + 63) / 64;
    std::map<std::string_view, std::pair<bool, std::vector<uint64_t>>>
        by_spelling;
    for (size_t index = 0; index < descriptor.variants.size(); ++index) {
      const auto add =
          [&](std::span<const check_end::SyntaxModifierDescriptor> order,
              bool canonical) {
            for (const auto& modifier : order) {
              for (const auto spelling : modifier.allowed_values) {
                auto& row = by_spelling[spelling];
                row.first |= canonical;
                row.second.resize(count);
                if (modifier.presence != check_end::PresenceRequirement::Absent)
                  row.second[index / 64] |= uint64_t{1} << (index % 64);
              }
            }
          };
      add(descriptor.variants[index].modifiers, true);
      for (const auto& alias :
           descriptor.variants[index].modifier_order_aliases)
        add(alias.modifiers, false);
    }
    for (const auto& [spelling, row] : by_spelling) {
      rows.push_back({spelling, row.first});
      words.insert(words.end(), row.second.begin(), row.second.end());
    }
  }
  /** Borrow owned immutable storage; this object must outlive the returned view. */
  detail::ModifierCandidatePlan view() const { return {rows, words}; }
};
}  // namespace ptx_frontend::resolved_ir::test_helpers
