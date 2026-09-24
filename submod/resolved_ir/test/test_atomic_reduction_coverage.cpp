#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Exercise each supported syntax tuple through the complete module path. */
TEST(AtomicReductionCoverage, ResolvesEverySupportedTupleAndQualifierCohort) {
  std::string source = R"ptx(
.version 9.3
.target sm_80
.global .align 4 .b32 global_value;
.entry kernel() {
  .reg .b32 %b<4>;
  .reg .u32 %u<4>;
  .reg .s32 %s<4>;
)ptx";
  for (std::string_view opcode : {"atom", "red"}) {
    for (std::string_view operation : {"add", "min", "max"}) {
      for (std::string_view type : {"u32", "s32"}) {
        for (std::string_view qualifier : {"", ".relaxed.cta"}) {
          source += std::string(opcode) + std::string(qualifier) + ".global." +
                    std::string(operation) + "." + std::string(type) + " ";
          if (opcode == "atom") {
            source += type == "u32" ? "%u0, " : "%s0, ";
          }
          source += "[global_value], 1;\n";
        }
      }
    }
    if (opcode == "atom") {
      source += "atom.global.cas.b32 %b0, [global_value], %b1, %b2;\n";
      source += "atom.relaxed.cta.global.cas.b32 %b0, [global_value], 1, 2;\n";
    }
  }
  source += "}\n";

  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 26u);
  for (size_t index = 0; index != 14; ++index) {
    const auto& atom = std::get<Atom>(body[index]);
    EXPECT_EQ(atom.variant.index(), index);
  }
  for (size_t index = 0; index != 12; ++index) {
    const auto& red = std::get<Red>(body[index + 14]);
    EXPECT_EQ(red.variant.index(), index);
  }
  const auto& legacy_cas =
      std::get<Atom::GlobalCasB32>(std::get<Atom>(body[12]).variant);
  const auto& modern_cas = std::get<Atom::GlobalRelaxedCtaCasB32>(
      std::get<Atom>(body[13]).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(legacy_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(legacy_cas.swap.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(modern_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(modern_cas.swap.value));
  const auto& atom_source =
      std::get<Atom::GlobalAddU32>(std::get<Atom>(body[0]).variant).src.value;
  const auto& red_source =
      std::get<Red::GlobalAddU32>(std::get<Red>(body[14]).variant).src.value;
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(atom_source));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(red_source));
}

/** Pin the distinct target floors of omitted and explicit qualifiers. */
TEST(AtomicReductionCoverage, EnforcesLegacyAndModernTargetFloors) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 global_value;
.entry kernel() {
  .reg .b32 %b<3>;
  atom.global.add.u32 %b0, [global_value], %b1;
  red.global.min.s32 [global_value], %b1;
  atom.global.cas.b32 %b0, [global_value], %b1, %b2;
  atom.relaxed.cta.global.add.u32 %b0, [global_value], %b1;
  red.relaxed.cta.global.max.s32 [global_value], %b1;
  atom.relaxed.cta.global.cas.b32 %b0, [global_value], %b1, %b2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto check_at = [&](size_t index, uint16_t ptx_major,
                            uint16_t ptx_minor, uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {ptx_major, ptx_minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index])) {
      return checker::check(*atom, context);
    }
    return checker::check(std::get<Red>(body[index]), context);
  };
  for (size_t index : {0u, 2u}) {
    EXPECT_TRUE(check_at(index, 1, 1, 11).has_value());
    EXPECT_EQ(check_at(index, 1, 0, 11).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  }
  EXPECT_TRUE(check_at(1, 1, 2, 11).has_value());
  EXPECT_EQ(check_at(1, 1, 1, 11).error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  for (size_t index = 0; index != 3; ++index) {
    EXPECT_EQ(check_at(index, 1, 2, 10).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
  for (size_t index = 3; index != 6; ++index) {
    EXPECT_TRUE(check_at(index, 6, 0, 70).has_value());
    EXPECT_EQ(check_at(index, 5, 9, 70).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_EQ(check_at(index, 6, 0, 69).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Reject unsupported forms and validate known address provenance/alignment. */
TEST(AtomicReductionCoverage, RejectsInvalidTopologySpaceAndAlignment) {
  for (const std::string_view instruction : {
           "atom.global.cas.b32 %b0, [global_value], %b1;",
           "red.global.cas.b32 [global_value], %b1, %b2;",
           "atom.global.cas.u32 %b0, [global_value], %b1, %b2;",
           "atom.global.add.u64 %b0, [global_value], %b1;",
           "atom.add.u32 %b0, [global_value], %b1;",
           "red.acquire.cta.global.add.u32 [global_value], %b1;",
       }) {
    const std::string source =
        ".global .align 4 .b32 global_value;\n.entry kernel() {\n"
        ".reg .b32 %b<3>;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << instruction;
  }

  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 global_value;
.local .align 4 .b32 local_value;
.entry kernel() {
  .reg .b32 %b<3>;
  atom.global.cas.b32 %b0, [local_value], %b1, %b2;
  red.global.add.u32 [global_value+2], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  const auto atom = checker::check(std::get<Atom>(body[0]), context);
  ASSERT_FALSE(atom.has_value());
  EXPECT_EQ(atom.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const auto red = checker::check(std::get<Red>(body[1]), context);
  ASSERT_FALSE(red.has_value());
  EXPECT_EQ(red.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
}

/** Revalidate register payloads after the syntax owners have been released. */
TEST(AtomicReductionCoverage, RechecksMutatedOwnedValueSources) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.global .align 4 .b32 global_value;
.entry kernel() {
  .reg .b32 %b<3>;
  atom.global.add.u32 %b0, [global_value], %b1;
  atom.global.cas.b32 %b0, [global_value], %b1, 2;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());
  auto& body = owned->functions.front().body;
  auto& add = std::get<Atom::GlobalAddU32>(std::get<Atom>(body[0]).variant);
  auto& cas = std::get<Atom::GlobalCasB32>(std::get<Atom>(body[1]).variant);
  auto& add_source = std::get<ResolvedRegisterRef>(add.src.value);
  add_source.declared_type = ScalarType::U64;
  const auto invalid_add =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_add.has_value());
  EXPECT_EQ(invalid_add.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  add_source.declared_type = ScalarType::B32;
  auto& compare = std::get<ResolvedRegisterRef>(cas.compare.value);
  compare.declared_type = ScalarType::U64;
  const auto invalid_compare =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_compare.has_value());
  EXPECT_EQ(invalid_compare.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  compare.declared_type = ScalarType::B32;
  EXPECT_TRUE(validateModule(*owned,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
