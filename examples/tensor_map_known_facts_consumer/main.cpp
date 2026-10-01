#include <algorithm>
#include <cstdint>
#include <iostream>

#include <ptx_frontend/resolved_ir/ptx_tensor_map_known_facts.hpp>

int main() {
  namespace ir = ptx_frontend::resolved_ir;
  ir::TensorKnownAccessContext selected;
  selected.direction = ir::TensorKnownFactDirection::Load;
  selected.mode = ir::TensorAccessMode::Tiled;
  selected.rank = ir::TensorRank::Three;
  selected.coordinate_arity = 3;
  selected.coordinates[0].value = 0;
  selected.coordinates[1].value = 1;
  selected.coordinates[2].value = 2;
  selected.selected_variant_available = true;
  selected.selected_value_available = true;

  ir::TensorMapKnownFacts claimed;
  claimed.rank = ir::TensorRank::Three;
  ir::TensorKnownProjectedElement element;
  element.value = ir::TensorMapElementType::U32;
  element.code = 2;
  element.valid = true;
  claimed.element = element;
  claimed.swizzle = ir::TensorKnownProjectedField<ir::TensorMapSwizzleMode>{
      .value = ir::TensorMapSwizzleMode::None, .code = 0, .valid = true};
  claimed.atomicity =
      ir::TensorKnownProjectedField<ir::TensorMapSwizzleAtomicity>{
          .value = ir::TensorMapSwizzleAtomicity::Bytes16,
          .code = 0,
          .valid = true};
  claimed.accessed_box_bytes = 16;
  claimed.accessed_box_address_bytes = 16;
  // No shared address, dimensions, or fill claim is fabricated from a map.
  const auto report = ir::validate_tensor_access_facts(selected, claimed);
  const auto find = [&report](uint8_t id) {
    return std::find_if(report.outcomes.begin(), report.outcomes.end(),
                        [id](const ir::TensorKnownFactOutcome& item) {
                          return item.rule_id == id;
                        });
  };
  const auto box = find(3);
  const auto atomicity = find(10);
  const auto pattern = find(12);
  if (!report.diagnostics.empty() || box == report.outcomes.end() ||
      atomicity == report.outcomes.end() || pattern == report.outcomes.end() ||
      box->status != ir::TensorKnownFactStatus::Checked ||
      atomicity->status != ir::TensorKnownFactStatus::NotApplicable ||
      pattern->status != ir::TensorKnownFactStatus::NotApplicable) {
    std::cerr << "known-fact partial-result contract failed\n";
    return 1;
  }
  std::cout << "known-fact partial results passed\n";
}
