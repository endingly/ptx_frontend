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
  const auto atom_legacy_ptx = check_at(0, 1, 0, 11);
  ASSERT_FALSE(atom_legacy_ptx.has_value());
  ASSERT_FALSE(atom_legacy_ptx.error().empty());
  EXPECT_EQ(atom_legacy_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check_at(1, 1, 2, 11).has_value());
  const auto red_legacy_ptx = check_at(1, 1, 1, 11);
  ASSERT_FALSE(red_legacy_ptx.has_value());
  ASSERT_FALSE(red_legacy_ptx.error().empty());
  EXPECT_EQ(red_legacy_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  for (size_t index = 0; index != 2; ++index) {
    const auto legacy_sm = check_at(index, 1, 2, 10);
    ASSERT_FALSE(legacy_sm.has_value());
    ASSERT_FALSE(legacy_sm.error().empty());
    EXPECT_EQ(legacy_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
  for (size_t index = 2; index != 4; ++index) {
    EXPECT_TRUE(check_at(index, 6, 0, 70).has_value());
    const auto modern_ptx = check_at(index, 5, 9, 70);
    ASSERT_FALSE(modern_ptx.has_value());
    ASSERT_FALSE(modern_ptx.error().empty());
    EXPECT_EQ(modern_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto modern_sm = check_at(index, 6, 0, 69);
    ASSERT_FALSE(modern_sm.has_value());
    ASSERT_FALSE(modern_sm.error().empty());
    EXPECT_EQ(modern_sm.error().front().kind,
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
    const auto legacy_ptx = check_at(index, 1, 0, 11);
    ASSERT_FALSE(legacy_ptx.has_value());
    ASSERT_FALSE(legacy_ptx.error().empty());
    EXPECT_EQ(legacy_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  }
  EXPECT_TRUE(check_at(1, 1, 2, 11).has_value());
  const auto red_legacy_ptx = check_at(1, 1, 1, 11);
  ASSERT_FALSE(red_legacy_ptx.has_value());
  ASSERT_FALSE(red_legacy_ptx.error().empty());
  EXPECT_EQ(red_legacy_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  for (size_t index = 0; index != 3; ++index) {
    const auto legacy_sm = check_at(index, 1, 2, 10);
    ASSERT_FALSE(legacy_sm.has_value());
    ASSERT_FALSE(legacy_sm.error().empty());
    EXPECT_EQ(legacy_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
  for (size_t index = 3; index != 6; ++index) {
    EXPECT_TRUE(check_at(index, 6, 0, 70).has_value());
    const auto modern_ptx = check_at(index, 5, 9, 70);
    ASSERT_FALSE(modern_ptx.has_value());
    ASSERT_FALSE(modern_ptx.error().empty());
    EXPECT_EQ(modern_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto modern_sm = check_at(index, 6, 0, 69);
    ASSERT_FALSE(modern_sm.has_value());
    ASSERT_FALSE(modern_sm.error().empty());
    EXPECT_EQ(modern_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Reject unsupported forms and validate known address provenance/alignment. */
TEST(AtomicReductionCoverage, RejectsInvalidTopologySpaceAndAlignment) {
  for (const std::string_view instruction : {
           "atom.global.cas.b32 %b0, [global_value], %b1;",
           "red.global.cas.b32 [global_value], %b1, %b2;",
           "atom.global.cas.u32 %b0, [global_value], %b1, %b2;",
           "atom.global.add.s64 %b0, [global_value], %b1;",
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

/** Resolve the complete 64-bit matrix in both supported qualifier orders. */
TEST(AtomicReductionCoverage, Resolves64BitScalarMatrix) {
  std::string source = R"ptx(
.version 9.3
.target sm_80
.global .align 8 .b64 global_value_64;
.entry kernel() {
  .reg .u64 %u<3>;
  .reg .s64 %s<3>;
  .reg .b64 %b<3>;
)ptx";
  for (std::string_view opcode : {"atom", "red"}) {
    for (std::string_view operation_type :
         {"add.u64", "min.u64", "min.s64", "max.u64", "max.s64", "and.b64",
          "or.b64", "xor.b64", "exch.b64", "cas.b64"}) {
      if (opcode == "red" &&
          (operation_type == "exch.b64" || operation_type == "cas.b64"))
        continue;
      const std::string_view type =
          operation_type.substr(operation_type.rfind('.') + 1);
      const std::string_view reg = type == "u64"   ? "%u"
                                   : type == "s64" ? "%s"
                                                   : "%b";
      for (std::string_view qualifier :
           {".global", ".relaxed.cta.global", ".global.relaxed.cta"}) {
        source += std::string(opcode) + std::string(qualifier) + "." +
                  std::string(operation_type) + " ";
        if (opcode == "atom")
          source += std::string(reg) + "0, ";
        source += "[global_value_64], ";
        if (operation_type == "cas.b64") {
          source += qualifier == ".global" ? "%b1, %b2;\n" : "1, 2;\n";
        } else {
          source += qualifier == ".global" ? std::string(reg) + "1;\n" : "1;\n";
        }
      }
    }
  }
  source += "}\n";

  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 54u);
  for (size_t pair = 0; pair != 10; ++pair) {
    for (size_t cohort = 0; cohort != 3; ++cohort)
      EXPECT_EQ(std::get<Atom>(body[pair * 3 + cohort]).variant.index(),
                26 + pair * 2 + (cohort != 0));
  }
  for (size_t pair = 0; pair != 8; ++pair) {
    for (size_t cohort = 0; cohort != 3; ++cohort)
      EXPECT_EQ(std::get<Red>(body[30 + pair * 3 + cohort]).variant.index(),
                22 + pair * 2 + (cohort != 0));
  }
  const auto& atom_add =
      std::get<Atom::GlobalAddU64>(std::get<Atom>(body[0]).variant);
  const auto& atom_cas =
      std::get<Atom::GlobalRelaxedCtaCasB64>(std::get<Atom>(body[29]).variant);
  const auto& red_xor =
      std::get<Red::GlobalRelaxedCtaXorB64>(std::get<Red>(body.back()).variant);
  EXPECT_EQ(atom_add.type, ScalarType::U64);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(atom_add.src.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(atom_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(atom_cas.swap.value));
  EXPECT_EQ(red_xor.type, ScalarType::B64);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(red_xor.src.value));
}

/** Pin the different legacy availability floors of the 64-bit families. */
TEST(AtomicReductionCoverage, Enforces64BitTargetFloors) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 8 .b64 global_value_64;
.entry kernel() {
  .reg .u64 %u<3>;
  .reg .s64 %s<3>;
  .reg .b64 %b<3>;
  atom.global.add.u64 %u0, [global_value_64], %u1;
  atom.global.exch.b64 %b0, [global_value_64], %b1;
  atom.global.cas.b64 %b0, [global_value_64], %b1, %b2;
  red.global.add.u64 [global_value_64], %u1;
  atom.global.min.s64 %s0, [global_value_64], %s1;
  atom.global.and.b64 %b0, [global_value_64], %b1;
  red.global.max.u64 [global_value_64], %u1;
  red.global.xor.b64 [global_value_64], %b1;
  atom.relaxed.cta.global.max.s64 %s0, [global_value_64], %s1;
  red.global.relaxed.cta.min.s64 [global_value_64], %s1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto check_at = [&](size_t index, uint16_t major, uint16_t minor,
                            uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index]))
      return checker::check(*atom, context);
    return checker::check(std::get<Red>(body[index]), context);
  };
  for (size_t index = 0; index != 4; ++index) {
    EXPECT_TRUE(check_at(index, 1, 2, 12).has_value());
    const auto old_ptx = check_at(index, 1, 1, 12);
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check_at(index, 1, 2, 11);
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
  for (size_t index = 4; index != 8; ++index) {
    EXPECT_TRUE(check_at(index, 3, 1, 32).has_value());
    const auto old_ptx = check_at(index, 3, 0, 32);
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check_at(index, 3, 1, 31);
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
  for (size_t index = 8; index != 10; ++index) {
    EXPECT_TRUE(check_at(index, 6, 0, 70).has_value());
    const auto old_ptx = check_at(index, 5, 9, 70);
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check_at(index, 6, 0, 69);
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Reject unsupported 64-bit pairs, malformed operands, and value widths. */
TEST(AtomicReductionCoverage, RejectsInvalid64BitForms) {
  for (const std::string_view instruction : {
           "atom.global.add.s64 %s0, [global_value_64], %s1;",
           "red.global.add.s64 [global_value_64], %s1;",
           "atom.global.inc.u64 %u0, [global_value_64], %u1;",
           "red.global.dec.u64 [global_value_64], %u1;",
           "atom.global.and.u64 %u0, [global_value_64], %u1;",
           "red.global.xor.s64 [global_value_64], %s1;",
           "red.global.exch.b64 [global_value_64], %b1;",
           "red.global.cas.b64 [global_value_64], %b1, %b2;",
           "atom.global.cas.b64 %b0, [global_value_64], %b1;",
           "atom.global.add.u64 %u0, [global_value_64], %u1, %u2;",
           "red.global.add.u64 %u0, [global_value_64], %u1;",
           "atom.add.u64 %u0, [global_value_64], %u1;",
           "atom.shared.add.u64 %u0, [global_value_64], %u1;",
           "red.acquire.cta.global.add.u64 [global_value_64], %u1;",
           "atom.global.relaxed.gpu.add.u64 %u0, [global_value_64], %u1;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_80\n"
        ".global .align 8 .b64 global_value_64;\n.entry kernel() {\n"
        ".reg .u64 %u<3>; .reg .s64 %s<3>; .reg .b64 %b<3>;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << instruction;
  }

  for (const std::string_view instruction : {
           "atom.global.add.u64 %u0, [global_value_64], %r1;",
           "atom.global.cas.b64 %b0, [global_value_64], %r1, %b2;",
           "red.global.max.s64 [global_value_64], %r1;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_80\n"
        ".global .align 8 .b64 global_value_64;\n.entry kernel() {\n"
        ".reg .u64 %u<3>; .reg .s64 %s<3>; .reg .b64 %b<3>;"
        ".reg .u32 %r<3>;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_FALSE(resolved.has_value()) << instruction;
    EXPECT_EQ(resolved.error().front().checker_kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

/** Check eight-byte alignment and known-global provenance. */
TEST(AtomicReductionCoverage, Checks64BitAddressContracts) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 8 .b64 global_value_64;
.local .align 8 .b64 local_value_64;
.entry kernel() {
  .reg .u64 %u<2>;
  .reg .b64 %b<2>;
  atom.global.add.u64 %u0, [local_value_64], %u1;
  red.global.xor.b64 [global_value_64+4], %b1;
  atom.global.cas.b64 %b0, [global_value_64+4], %b1, 2;
  red.global.add.u64 [local_value_64], %u1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  const auto atom_space = checker::check(std::get<Atom>(body[0]), context);
  ASSERT_FALSE(atom_space.has_value());
  EXPECT_EQ(atom_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const auto red_alignment = checker::check(std::get<Red>(body[1]), context);
  ASSERT_FALSE(red_alignment.has_value());
  EXPECT_EQ(red_alignment.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  const auto atom_alignment = checker::check(std::get<Atom>(body[2]), context);
  ASSERT_FALSE(atom_alignment.has_value());
  EXPECT_EQ(atom_alignment.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  const auto red_space = checker::check(std::get<Red>(body[3]), context);
  ASSERT_FALSE(red_space.has_value());
  EXPECT_EQ(red_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

/** Recheck typed 64-bit payloads after releasing their syntax owners. */
TEST(AtomicReductionCoverage, RechecksOwned64BitValueSources) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.global .align 8 .b64 global_value_64;
.entry kernel() {
  .reg .u64 %u<2>;
  .reg .b64 %b<3>;
  atom.global.add.u64 %u0, [global_value_64], %u1;
  atom.global.cas.b64 %b0, [global_value_64], %b1, %b2;
  red.global.xor.b64 [global_value_64], %b1;
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
  auto& add = std::get<Atom::GlobalAddU64>(std::get<Atom>(body[0]).variant);
  auto& cas = std::get<Atom::GlobalCasB64>(std::get<Atom>(body[1]).variant);
  auto& red = std::get<Red::GlobalXorB64>(std::get<Red>(body[2]).variant);
  auto& add_src = std::get<ResolvedRegisterRef>(add.src.value);
  add_src.declared_type = ScalarType::U32;
  const auto invalid_add =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_add.has_value());
  EXPECT_EQ(invalid_add.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  add_src.declared_type = ScalarType::U64;
  auto& compare = std::get<ResolvedRegisterRef>(cas.compare.value);
  compare.declared_type = ScalarType::B32;
  const auto invalid_compare =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_compare.has_value());
  EXPECT_EQ(invalid_compare.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  compare.declared_type = ScalarType::B64;
  auto& red_src = std::get<ResolvedRegisterRef>(red.src.value);
  red_src.declared_type = ScalarType::B32;
  const auto invalid_red =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_red.has_value());
  EXPECT_EQ(invalid_red.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  red_src.declared_type = ScalarType::B64;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Resolve every float tuple, qualifier order, and supported source spelling. */
TEST(AtomicReductionCoverage, ResolvesFloatAddMatrix) {
  std::string source = R"ptx(
.version 9.3
.target sm_80
.global .align 4 .b32 g32;
.global .align 8 .b64 g64;
.entry kernel() {
  .reg .f32 %f<2>;
  .reg .b32 %b<2>;
  .reg .f64 %fd<2>;
  .reg .b64 %bd<2>;
)ptx";
  for (std::string_view opcode : {"atom", "red"}) {
    for (std::string_view type : {"f32", "f64"}) {
      const bool is_32 = type == "f32";
      const std::string_view native = is_32 ? "%f" : "%fd";
      const std::string_view bits = is_32 ? "%b" : "%bd";
      const std::string_view address = is_32 ? "g32" : "g64";
      const std::string_view literal =
          is_32 ? "0f3f800000" : "0d3ff0000000000000";
      for (std::string_view qualifier :
           {".global", ".relaxed.cta.global", ".global.relaxed.cta"}) {
        for (std::string_view value :
             {native, bits, std::string_view{"1.0"}, literal}) {
          source += std::string(opcode) + std::string(qualifier) + ".add." +
                    std::string(type) + " ";
          if (opcode == "atom")
            source += std::string(native) + "0, ";
          source += "[" + std::string(address) + "], " +
                    (value.starts_with('%') ? std::string(value) + "1"
                                            : std::string(value)) +
                    ";\n";
        }
      }
    }
  }
  source += "}\n";
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 48u);
  for (size_t type = 0; type != 2; ++type) {
    for (size_t order = 0; order != 3; ++order) {
      for (size_t source_index = 0; source_index != 4; ++source_index) {
        const size_t local = type * 12 + order * 4 + source_index;
        const size_t expected = type * 2 + (order != 0);
        EXPECT_EQ(std::get<Atom>(body[local]).variant.index(), 46 + expected);
        EXPECT_EQ(std::get<Red>(body[24 + local]).variant.index(),
                  38 + expected);
      }
    }
  }
  const auto& atom_bits =
      std::get<Atom::GlobalAddF32>(std::get<Atom>(body[1]).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(atom_bits.src.value));
  const auto& red_literal =
      std::get<Red::GlobalAddF64>(std::get<Red>(body[24 + 12 + 3]).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(red_literal.src.value));
}

/** Pin float legacy and explicit availability at both dimensions. */
TEST(AtomicReductionCoverage, EnforcesFloatAddTargetFloors) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 g32;
.global .align 8 .b64 g64;
.entry kernel() {
  .reg .f32 %f<2>; .reg .f64 %fd<2>;
  atom.global.add.f32 %f0, [g32], %f1;
  red.global.add.f32 [g32], %f1;
  atom.global.add.f64 %fd0, [g64], %fd1;
  red.global.add.f64 [g64], %fd1;
  atom.global.relaxed.cta.add.f32 %f0, [g32], %f1;
  red.relaxed.cta.global.add.f64 [g64], %fd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto check_at = [&](size_t index, uint16_t major, uint16_t minor,
                            uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index]))
      return checker::check(*atom, context);
    return checker::check(std::get<Red>(body[index]), context);
  };
  for (size_t index = 0; index != 6; ++index) {
    const uint16_t major = index < 2 ? 2 : index < 4 ? 5 : 6;
    const uint32_t sm = index < 2 ? 20 : index < 4 ? 60 : 70;
    EXPECT_TRUE(check_at(index, major, 0, sm).has_value());
    const auto old_ptx = check_at(index, major - 1, 9, sm);
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check_at(index, major, 0, sm - 1);
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Reject unsupported float forms and check typed sources and address facts. */
TEST(AtomicReductionCoverage, RejectsInvalidFloatAddForms) {
  for (std::string_view instruction : {
           "atom.global.min.f32 %f0, [g32], %f1;",
           "red.global.max.f64 [g64], %fd1;",
           "atom.global.add.f64 %fd0, [g64], %fd1, %fd1;",
           "red.add.f32 [g32], %f1;",
           "atom.shared.add.f32 %f0, [g32], %f1;",
           "red.acquire.cta.global.add.f64 [g64], %fd1;",
           "atom.global.relaxed.gpu.add.f32 %f0, [g32], %f1;",
           "atom.global.add.f32 %f0, [g32], 1;",
           "red.global.add.f64 [g64], 1;",
           "atom.global.add.f32 %u0, [g32], %f1;",
           "red.global.add.f64 [g64], %ud1;",
           "atom.global.add.f32 %f0, [g32], %ud1;",
           "red.global.add.f32 [g32], %fd1;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_80\n"
        ".global .align 4 .b32 g32; .global .align 8 .b64 g64;\n"
        ".entry kernel() { .reg .f32 %f<2>; .reg .f64 %fd<2>; "
        ".reg .u32 %u<2>; .reg .u64 %ud<2>; " +
        std::string(instruction) + " }\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << instruction;
  }

  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .b32 g32;
.global .align 8 .b64 g64;
.local .align 8 .b64 local64;
.entry kernel() {
  .reg .f32 %f<2>; .reg .b32 %b<2>;
  .reg .f64 %fd<2>; .reg .b64 %bd<2>;
  atom.global.add.f64 %fd0, [local64], %bd1;
  red.global.add.f64 [g64+4], %fd1;
  atom.global.add.f32 %f0, [g32+2], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (size_t index = 0; index != 3; ++index) {
    const auto result =
        std::holds_alternative<Atom>(body[index])
            ? checker::check(std::get<Atom>(body[index]), context)
            : checker::check(std::get<Red>(body[index]), context);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().front().kind,
              index == 0
                  ? checker::CheckDiagnosticKind::AddressStateSpaceMismatch
                  : checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

/** Revalidate float bit-container payloads after syntax storage is released. */
TEST(AtomicReductionCoverage, RechecksOwnedFloatAddOperands) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.global .align 4 .b32 g32;
.global .align 8 .b64 g64;
.entry kernel() {
  .reg .f32 %f<2>; .reg .b32 %b<2>; .reg .b64 %bd<2>;
  atom.global.add.f32 %f0, [g32], %b1;
  red.global.add.f64 [g64], %bd1;
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
  auto& atom = std::get<Atom::GlobalAddF32>(std::get<Atom>(body[0]).variant);
  auto& red = std::get<Red::GlobalAddF64>(std::get<Red>(body[1]).variant);
  auto& atom_src = std::get<ResolvedRegisterRef>(atom.src.value);
  atom_src.declared_type = ScalarType::U32;
  const auto invalid_atom =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_atom.has_value());
  EXPECT_EQ(invalid_atom.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  atom_src.declared_type = ScalarType::B32;
  auto& red_src = std::get<ResolvedRegisterRef>(red.src.value);
  red_src.declared_type = ScalarType::U64;
  const auto invalid_red =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_red.has_value());
  EXPECT_EQ(invalid_red.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  red_src.declared_type = ScalarType::B64;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
