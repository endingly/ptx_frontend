#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tensor_map_known_facts.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Locate one result in the generated catalog's stable numeric order. */
const TensorKnownFactOutcome* outcome(const TensorKnownFactsReport& report,
                                      uint8_t rule_id) {
  const auto selected =
      std::find_if(report.outcomes.begin(), report.outcomes.end(),
                   [rule_id](const TensorKnownFactOutcome& item) {
                     return item.rule_id == rule_id;
                   });
  return selected == report.outcomes.end() ? nullptr : &*selected;
}

/** Supply one valid claim already projected by the accepted Table 33 helper. */
template <TensorKnownEncodedField Value>
TensorKnownProjectedField<Value> projected(Value value, uint64_t code) {
  return {.value = value, .code = code, .valid = true};
}

/** Supply code 15 with an independent directional interpretation claim. */
TensorKnownProjectedElement code15(
    std::optional<TensorKnownCode15Interpretation> meaning) {
  TensorKnownProjectedElement result;
  result.value = TensorMapElementType::B6x16P32OrB6p2x16;
  result.code = 15;
  result.valid = true;
  result.code15_interpretation = meaning;
  return result;
}

/** Supply a non-15 element identity with its accepted raw source code. */
TensorKnownProjectedElement element(TensorMapElementType value, uint64_t code) {
  TensorKnownProjectedElement result;
  result.value = value;
  result.code = code;
  result.valid = true;
  return result;
}

/** Raw atomicity storage does not mean selected active atomicity use. */
TEST(TensorMapKnownFacts, NoSwizzleRawAtomicityIsNotApplicable) {
  TensorKnownAccessContext access;
  access.direction = TensorKnownFactDirection::Load;
  access.rank = TensorRank::Three;
  access.coordinate_arity = 3;
  access.coordinates[0].value = 0;
  access.coordinates[1].value = 1;
  access.coordinates[2].value = 2;
  access.selected_variant_available = true;
  access.selected_value_available = true;
  TensorMapKnownFacts supplied;
  supplied.rank = TensorRank::Three;
  supplied.element = element(TensorMapElementType::U32, 2);
  supplied.swizzle = projected(TensorMapSwizzleMode::None, 0);
  supplied.atomicity = projected(TensorMapSwizzleAtomicity::Bytes32, 1);
  const auto report = validate_tensor_access_facts(access, supplied);
  const auto* atomicity = outcome(report, 10);
  ASSERT_NE(atomicity, nullptr);
  EXPECT_EQ(atomicity->status, TensorKnownFactStatus::NotApplicable);
  const auto* availability = outcome(report, 22);
  ASSERT_NE(availability, nullptr);
  EXPECT_EQ(availability->status, TensorKnownFactStatus::Checked);
}

/** Exact packed-store override cannot also apply the generic packed rule. */
TEST(TensorMapKnownFacts, ExactPackedStoreOverridesGenericRule) {
  TensorKnownAccessContext access;
  access.direction = TensorKnownFactDirection::Store;
  access.rank = TensorRank::Three;
  access.coordinate_arity = 3;
  access.coordinates[0].value = 64;
  access.coordinates[1].value = 0;
  access.coordinates[2].value = 0;
  const auto target = base::find_target_profile("sm_103a");
  ASSERT_TRUE(target);
  access.target_identity = target->identity;
  access.active_atomicity_use = true;
  TensorMapKnownFacts supplied;
  supplied.rank = TensorRank::Three;
  supplied.element = code15(TensorKnownCode15Interpretation::Store);
  supplied.swizzle = projected(TensorMapSwizzleMode::Bytes64, 2);
  supplied.atomicity = projected(TensorMapSwizzleAtomicity::Bytes16, 0);
  supplied.inner_box_bytes = 48;
  supplied.inner_tensor_bytes = 48;
  supplied.data_base_address_bytes = 16;
  TensorKnownUnsignedArray strides;
  strides.arity = 2;
  strides.values[0] = 16;
  strides.values[1] = 16;
  supplied.tensor_strides_bytes = strides;
  const auto report = validate_tensor_access_facts(access, supplied);
  const auto* generic = outcome(report, 7);
  const auto* exact = outcome(report, 15);
  ASSERT_NE(generic, nullptr);
  ASSERT_NE(exact, nullptr);
  EXPECT_EQ(generic->status, TensorKnownFactStatus::NotApplicable);
  EXPECT_EQ(exact->status, TensorKnownFactStatus::Checked);
}

/** Both directions preserve one code but check the supplied semantic claim. */
TEST(TensorMapKnownFacts, Code15DirectionalClaimAndAbsentInterpretation) {
  TensorKnownAccessContext access;
  access.rank = TensorRank::Three;
  access.coordinate_arity = 3;
  for (auto& coordinate : access.coordinates)
    coordinate.value = 0;
  TensorMapKnownFacts supplied;
  supplied.rank = TensorRank::Three;
  for (const auto direction :
       {TensorKnownFactDirection::Load, TensorKnownFactDirection::Prefetch,
        TensorKnownFactDirection::Store, TensorKnownFactDirection::Reduce}) {
    access.direction = direction;
    const bool reading = direction == TensorKnownFactDirection::Load ||
                         direction == TensorKnownFactDirection::Prefetch;
    supplied.element = code15(reading ? TensorKnownCode15Interpretation::Load
                                      : TensorKnownCode15Interpretation::Store);
    const auto good = validate_tensor_access_facts(access, supplied);
    ASSERT_NE(outcome(good, 1), nullptr);
    EXPECT_EQ(outcome(good, 1)->status, TensorKnownFactStatus::Checked);

    supplied.element = code15(reading ? TensorKnownCode15Interpretation::Store
                                      : TensorKnownCode15Interpretation::Load);
    const auto wrong = validate_tensor_access_facts(access, supplied);
    ASSERT_NE(outcome(wrong, 1), nullptr);
    EXPECT_EQ(outcome(wrong, 1)->status, TensorKnownFactStatus::Violated);
    ASSERT_NE(outcome(wrong, 7), nullptr);
    EXPECT_EQ(outcome(wrong, 7)->status, TensorKnownFactStatus::Unresolved);

    supplied.element = code15(std::nullopt);
    const auto missing = validate_tensor_access_facts(access, supplied);
    ASSERT_NE(outcome(missing, 1), nullptr);
    EXPECT_EQ(outcome(missing, 1)->status, TensorKnownFactStatus::Unresolved);
    EXPECT_NE(outcome(missing, 1)->detail.find("interpretation"),
              std::string::npos);
    ASSERT_NE(outcome(missing, 7), nullptr);
    EXPECT_EQ(outcome(missing, 7)->status, TensorKnownFactStatus::Unresolved);
  }
}

/** Invalid projection claims cannot turn into compatible descriptor facts. */
TEST(TensorMapKnownFacts, RejectedAndContradictoryProjectionsDiagnose) {
  TensorKnownAccessContext access;
  access.rank = TensorRank::Three;
  access.selected_variant_available = true;
  access.selected_value_available = true;
  TensorMapKnownFacts supplied;
  supplied.rank = TensorRank::Three;
  supplied.element = element(TensorMapElementType::U32, 2);
  supplied.element->valid = false;
  auto report = validate_tensor_access_facts(access, supplied);
  EXPECT_FALSE(report.diagnostics.empty());
  ASSERT_NE(outcome(report, 1), nullptr);
  EXPECT_EQ(outcome(report, 1)->status, TensorKnownFactStatus::Unresolved);
  ASSERT_NE(outcome(report, 22), nullptr);
  EXPECT_EQ(outcome(report, 22)->status, TensorKnownFactStatus::Checked);

  supplied.element = element(TensorMapElementType::U32, 15);
  report = validate_tensor_access_facts(access, supplied);
  EXPECT_FALSE(report.diagnostics.empty());
  ASSERT_NE(outcome(report, 1), nullptr);
  EXPECT_EQ(outcome(report, 1)->status, TensorKnownFactStatus::Unresolved);

  supplied.element = element(static_cast<TensorMapElementType>(255), 2);
  report = validate_tensor_access_facts(access, supplied);
  EXPECT_FALSE(report.diagnostics.empty());
  ASSERT_NE(outcome(report, 1), nullptr);
  EXPECT_EQ(outcome(report, 1)->status, TensorKnownFactStatus::Unresolved);

  supplied.element = code15(static_cast<TensorKnownCode15Interpretation>(255));
  report = validate_tensor_access_facts(access, supplied);
  EXPECT_FALSE(report.diagnostics.empty());
  ASSERT_NE(outcome(report, 1), nullptr);
  EXPECT_EQ(outcome(report, 1)->status, TensorKnownFactStatus::Unresolved);

  supplied.element = element(TensorMapElementType::U32, 2);
  supplied.swizzle = projected(TensorMapSwizzleMode::Bytes64, 2);
  supplied.swizzle->valid = false;
  report = validate_tensor_access_facts(access, supplied);
  EXPECT_FALSE(report.diagnostics.empty());
  ASSERT_NE(outcome(report, 10), nullptr);
  EXPECT_EQ(outcome(report, 10)->status, TensorKnownFactStatus::Unresolved);
}

/** Pattern offsets retain their independently expected numeric values. */
TEST(TensorMapKnownFacts, RepeatingPatternDerivedValues) {
  TensorKnownAccessContext access;
  access.direction = TensorKnownFactDirection::Load;
  TensorMapKnownFacts supplied;
  supplied.swizzle = projected(TensorMapSwizzleMode::Bytes32, 1);
  supplied.shared_destination_address_bytes = 256;
  auto report = validate_tensor_access_facts(access, supplied);
  ASSERT_NE(outcome(report, 12), nullptr);
  EXPECT_EQ(outcome(report, 12)->status, TensorKnownFactStatus::Checked);
  EXPECT_EQ(outcome(report, 12)->derived_value, 0u);

  supplied.shared_destination_address_bytes = 128;
  report = validate_tensor_access_facts(access, supplied);
  ASSERT_NE(outcome(report, 12), nullptr);
  EXPECT_EQ(outcome(report, 12)->status, TensorKnownFactStatus::Checked);
  EXPECT_EQ(outcome(report, 12)->derived_value, 1u);

  supplied.swizzle = projected(TensorMapSwizzleMode::Bytes128, 3);
  supplied.shared_destination_address_bytes = 896;
  report = validate_tensor_access_facts(access, supplied);
  ASSERT_NE(outcome(report, 12), nullptr);
  EXPECT_EQ(outcome(report, 12)->status, TensorKnownFactStatus::Checked);
  EXPECT_EQ(outcome(report, 12)->derived_value, 7u);
}

/** Retain an owned selected instruction after the parsed syntax tree dies. */
std::optional<ResolvedModule> selected_tensor_module(
    std::string_view instruction) {
  const std::string source =
      ".version 9.3\n.target sm_100a\n.address_size 64\n"
      ".global .align 64 .b8 tensor_map[128];\n"
      ".shared .align 16 .b8 dst[1024];\n"
      ".shared .align 16 .b8 src[1024];\n"
      ".shared .align 8 .b64 mbar;\n"
      ".entry kernel() {\n.reg .s32 %r<5>;\n.reg .u16 %h<3>;\n" +
      std::string(instruction) + "\n}\n";
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Construct a checker context from the accepted exact target catalogue. */
checker::Context selected_tensor_context(std::string_view target) {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Exact selected variants preserve CTA, cluster, and absent destinations. */
TEST(TensorMapKnownFacts, SelectedDestinationAndAvailabilityAfterAstDeath) {
  /** One literal selected spelling and its independent directional outcome. */
  struct Case {
    std::string_view instruction;
    TensorKnownFactDirection direction;
    std::optional<TensorKnownFactDestination> destination;
  };
  constexpr Case cases[]{
      {"cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
       "bytes [dst], [tensor_map, {%r0}], [mbar];",
       TensorKnownFactDirection::Load, TensorKnownFactDestination::Cta},
      {"cp.async.bulk.tensor.1d.shared::cluster.global.tile.mbarrier::complete_"
       "tx::bytes [dst], [tensor_map, {%r0}], [mbar];",
       TensorKnownFactDirection::Load, TensorKnownFactDestination::Cluster},
      {"cp.async.bulk.prefetch.tensor.1d.L2.global.tile [tensor_map, {%r0}];",
       TensorKnownFactDirection::Prefetch, std::nullopt},
      {"cp.async.bulk.tensor.1d.global.shared::cta.tile.bulk_group "
       "[tensor_map, {%r0}], [src];",
       TensorKnownFactDirection::Store, std::nullopt},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(std::string(item.instruction));
    auto owned = selected_tensor_module(item.instruction);
    ASSERT_TRUE(owned);
    const auto& copy = *owned->functions.front().body.front();
    auto projected = project_tensor_known_access_context(
        copy, selected_tensor_context("sm_100a"));
    ASSERT_TRUE(projected.diagnostics.empty());
    ASSERT_TRUE(projected.access);
    EXPECT_EQ(projected.access->direction, item.direction);
    EXPECT_EQ(projected.access->destination, item.destination);
    EXPECT_EQ(projected.access->rank, TensorRank::One);
    EXPECT_EQ(projected.access->coordinate_arity, 1u);
    ASSERT_TRUE(projected.access->selected_variant_available.has_value());
    ASSERT_TRUE(projected.access->selected_value_available.has_value());
    EXPECT_TRUE(*projected.access->selected_variant_available);
    EXPECT_TRUE(*projected.access->selected_value_available);
    EXPECT_EQ(projected.access->target_identity->source_spelling, "sm_100a");
  }
}

/** Target override and damaged layout tag are independent selected facts. */
TEST(TensorMapKnownFacts, SelectedTargetOverrideAndMalformedTag) {
  auto owned = selected_tensor_module(
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes [dst], [tensor_map, {%r0}], [mbar];");
  ASSERT_TRUE(owned);
  auto* copy = dynamic_cast<CpAsyncBulkTensor1dSharedCta*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(copy, nullptr);
  const auto override_context = selected_tensor_context("sm_80");
  const auto* descriptor = checker::find_variant_descriptor(
      cp_checker_descriptor(), "AsyncBulkTensor1dSharedCta");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->variant_name, "AsyncBulkTensor1dSharedCta");
  EXPECT_EQ(override_context.target.sm_version, 80u);
  EXPECT_FALSE(checker::check_availability(*descriptor, override_context));
  auto projected = project_tensor_known_access_context(*copy, override_context);
  ASSERT_TRUE(projected.diagnostics.empty());
  ASSERT_TRUE(projected.access);
  ASSERT_TRUE(projected.access->selected_variant_available.has_value());
  EXPECT_FALSE(*projected.access->selected_variant_available);
  EXPECT_EQ(projected.access->target_identity->source_spelling, "sm_80");

  copy->operand_layout.value = 999;
  projected = project_tensor_known_access_context(
      *copy, selected_tensor_context("sm_100a"));
  EXPECT_FALSE(projected.access);
  EXPECT_FALSE(projected.diagnostics.empty());
}

/** Selected converted coordinates and information remain owned after parsing. */
TEST(TensorMapKnownFacts, SelectedConvertedValuesAndLayoutStorageGuard) {
  auto owned = selected_tensor_module(
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes [dst], [tensor_map, {4294967295, 0, 1}], [mbar], "
      "{65535};");
  ASSERT_TRUE(owned);
  auto* copy = dynamic_cast<CpAsyncBulkTensor3dSharedClusterIm2col*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(copy, nullptr);
  auto projected = project_tensor_known_access_context(
      *copy, selected_tensor_context("sm_100a"));
  ASSERT_TRUE(projected.diagnostics.empty());
  ASSERT_TRUE(projected.access);
  ASSERT_TRUE(projected.access->coordinates[0].value);
  EXPECT_EQ(projected.access->coordinates[0].value, -1);
  EXPECT_EQ(projected.access->coordinates[0].source_bits, 4294967295u);
  EXPECT_TRUE(projected.access->info_known);
  EXPECT_EQ(projected.access->info_arity, 1u);
  EXPECT_EQ(projected.access->info[0].value, 65535u);
  EXPECT_EQ(projected.access->info[0].source_bits, 65535u);

  copy->operand_layout.value = copy->operand_layout.value == 0 ? 1 : 0;
  projected = project_tensor_known_access_context(
      *copy, selected_tensor_context("sm_100a"));
  EXPECT_FALSE(projected.access);
  EXPECT_FALSE(projected.diagnostics.empty());
}

// The test-only generated include comes from literal Python fixture inputs.
#include <ptx_frontend/resolved_ir/test/tensor_map_known_facts_cases.gen.inc>

}  // namespace
}  // namespace ptx_frontend::resolved_ir
