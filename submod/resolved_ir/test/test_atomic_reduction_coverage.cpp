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
  const auto& modern_cas =
      std::get<Atom::GlobalRelaxedCtaCasB32>(std::get<Atom>(body[13]).variant);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(legacy_cas.compare.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(legacy_cas.swap.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(modern_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(modern_cas.swap.value));
  const auto& atom_source =
      std::get<Atom::GlobalAddU32>(std::get<Atom>(body[0]).variant).src.value;
  const auto& red_source =
      std::get<Red::GlobalAddU32>(std::get<Red>(body[14]).variant).src.value;
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(atom_source));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(red_source));
}

/** Exercise all added one-source variants and preserve their public ordering. */
TEST(AtomicReductionCoverage, ResolvesExpandedScalarMatrix) {
  std::string source = R"ptx(
.version 9.3
.target sm_80
.global .align 4 .b32 global_value;
.entry kernel() {
  .reg .u32 %u<3>;
  .reg .b32 %b<3>;
)ptx";
  for (std::string_view opcode : {"atom", "red"}) {
    for (std::string_view operation :
         {"inc", "dec", "and", "or", "xor", "exch"}) {
      if (opcode == "red" && operation == "exch")
        continue;
      const std::string_view type =
          operation == "inc" || operation == "dec" ? "u32" : "b32";
      for (std::string_view qualifier : {"", ".relaxed.cta"}) {
        source += std::string(opcode) + ".global" + std::string(qualifier) +
                  "." + std::string(operation) + "." + std::string(type) + " ";
        if (opcode == "atom")
          source += type == "u32" ? "%u0, " : "%b0, ";
        source += "[global_value], ";
        source +=
            qualifier.empty() ? (type == "u32" ? "%u1;\n" : "%b1;\n") : "1;\n";
      }
    }
  }
  source += "}\n";
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 22u);
  for (size_t index = 0; index != 12; ++index)
    EXPECT_EQ(std::get<Atom>(body[index]).variant.index(), index + 14);
  for (size_t index = 0; index != 10; ++index)
    EXPECT_EQ(std::get<Red>(body[index + 12]).variant.index(), index + 12);
  const auto& inc =
      std::get<Atom::GlobalIncU32>(std::get<Atom>(body[0]).variant);
  const auto& exch =
      std::get<Atom::GlobalRelaxedCtaExchB32>(std::get<Atom>(body[11]).variant);
  const auto& red =
      std::get<Red::GlobalRelaxedCtaXorB32>(std::get<Red>(body.back()).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(inc.src.value));
  EXPECT_EQ(inc.type, ScalarType::U32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(exch.src.value));
  EXPECT_EQ(exch.type, ScalarType::B32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(red.src.value));
  EXPECT_EQ(red.type, ScalarType::B32);
}

/** Pin legacy and explicit target floors for the added operation families. */
TEST(AtomicReductionCoverage, ExpandedOperationsHonorTargetFloors) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 global_value;
.entry kernel() {
  .reg .b32 %b<2>;
  atom.global.inc.u32 %b0, [global_value], %b1;
  red.global.and.b32 [global_value], %b1;
  atom.relaxed.cta.global.exch.b32 %b0, [global_value], %b1;
  red.relaxed.cta.global.dec.u32 [global_value], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  const auto check_at = [&](size_t index, uint16_t major, uint16_t minor,
                            uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index]))
      return checker::check(*atom, context);
    return checker::check(std::get<Red>(body[index]), context);
  };
  EXPECT_TRUE(check_at(0, 1, 1, 11).has_value());
  EXPECT_EQ(check_at(0, 1, 0, 11).error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check_at(1, 1, 2, 11).has_value());
  EXPECT_EQ(check_at(1, 1, 1, 11).error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  for (size_t index = 0; index != 2; ++index)
    EXPECT_EQ(check_at(index, 1, 2, 10).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  for (size_t index = 2; index != 4; ++index) {
    EXPECT_TRUE(check_at(index, 6, 0, 70).has_value());
    EXPECT_EQ(check_at(index, 5, 9, 70).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_EQ(check_at(index, 6, 0, 69).error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
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
           "atom.global.inc.s32 %b0, [global_value], %b1;",
           "red.global.dec.s32 [global_value], %b1;",
           "atom.global.and.u32 %b0, [global_value], %b1;",
           "red.global.or.u32 [global_value], %b1;",
           "atom.global.exch.u32 %b0, [global_value], %b1;",
           "red.global.exch.b32 [global_value], %b1;",
           "atom.global.exch.b32 %b0, [global_value], %b1, %b2;",
           "red.global.inc.u32 %b0, [global_value], %b1;",
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

  const auto expanded = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 global_value;
.local .align 4 .b32 local_value;
.entry kernel() {
  .reg .b32 %b<2>;
  atom.global.exch.b32 %b0, [local_value], %b1;
  red.global.xor.b32 [global_value+2], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(expanded);
  const auto expanded_ir = resolveModule(*expanded);
  ASSERT_TRUE(expanded_ir.has_value()) << expanded_ir.error().front().message;
  const auto& expanded_body = expanded_ir->functions.front().body;
  EXPECT_EQ(checker::check(std::get<Atom>(expanded_body[0]), context)
                .error()
                .front()
                .kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(checker::check(std::get<Red>(expanded_body[1]), context)
                .error()
                .front()
                .kind,
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
  atom.global.xor.b32 %b0, [global_value], %b1;
  red.global.dec.u32 [global_value], %b1;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
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
  auto& xor_src = std::get<ResolvedRegisterRef>(
      std::get<Atom::GlobalXorB32>(std::get<Atom>(body[2]).variant).src.value);
  xor_src.declared_type = ScalarType::U64;
  const auto invalid_xor =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_xor.has_value());
  EXPECT_EQ(invalid_xor.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  xor_src.declared_type = ScalarType::B32;
  auto& red_src = std::get<ResolvedRegisterRef>(
      std::get<Red::GlobalDecU32>(std::get<Red>(body[3]).variant).src.value);
  red_src.declared_type = ScalarType::U64;
  const auto invalid_red =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_red.has_value());
  EXPECT_EQ(invalid_red.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  red_src.declared_type = ScalarType::B32;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
