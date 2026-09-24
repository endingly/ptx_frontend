#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Preserve independent suffix spelling through owned module resolution. */
TEST(AtomicReductionCoverage, ResolvesIndependentScalarQualifiers) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 4 .u32 global_value;
.shared .align 4 .u32 shared_value;
.entry kernel() {
  .reg .u32 %u<2>;
  atom.add.u32 %u0, [global_value], %u1;
  atom.global.cta.add.u32 %u0, [global_value], %u1;
  atom.acquire.global.add.u32 %u0, [global_value], %u1;
  atom.acq_rel.cluster.shared::cluster.add.u32 %u0, [shared_value], %u1;
  atom.shared::cta.release.sys.add.u32 %u0, [shared_value], %u1;
  red.release.shared.add.u32 [shared_value], %u1;
  red.gpu.global.add.u32 [global_value], %u1;
  red.shared::cluster.cta.add.u32 [shared_value], %u1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  for (size_t index = 0; index < 5; ++index)
    EXPECT_EQ(std::get<Atom>(body[index]).variant.index(), 0u);
  for (size_t index = 5; index < 8; ++index)
    EXPECT_EQ(std::get<Red>(body[index]).variant.index(), 0u);
  const auto& generic = std::get<Atom>(body[0]);
  const auto& scope_only = std::get<Atom>(body[1]);
  const auto& sem_only = std::get<Atom>(body[2]);
  const auto& cluster = std::get<Atom>(body[3]);
  const auto& shared_cta = std::get<Atom>(body[4]);
  EXPECT_EQ(generic.address_qualifier.value, AtomicAddressQualifier::Generic);
  EXPECT_TRUE(generic.address_qualifier.locs.empty());
  EXPECT_EQ(scope_only.address_qualifier.value, AtomicAddressQualifier::Global);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(scope_only.variant).scope.value,
            MemoryScope::Cta);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(scope_only.variant).semantics.value,
            MemoryConsistency::Omitted);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(sem_only.variant).semantics.value,
            MemoryConsistency::Acquire);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(sem_only.variant).scope.value,
            MemoryScope::None);
  EXPECT_EQ(cluster.address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(cluster.variant).scope.value,
            MemoryScope::Cluster);
  EXPECT_EQ(shared_cta.address_qualifier.value,
            AtomicAddressQualifier::SharedCta);
  EXPECT_EQ(std::get<Atom::GlobalAddU32>(shared_cta.variant).semantics.value,
            MemoryConsistency::Release);
  EXPECT_EQ(std::get<Red>(body[5]).address_qualifier.value,
            AtomicAddressQualifier::Shared);
  EXPECT_EQ(std::get<Red>(body[7]).address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  std::get<Atom>(body[3]).address_qualifier.value =
      AtomicAddressQualifier::Global;
  const auto invalid_owned =
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_owned.has_value());
  EXPECT_EQ(invalid_owned.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
}

/** Pin independent suffix floors and reject invalid owned qualifier edits. */
TEST(AtomicReductionCoverage, ChecksIndependentScalarQualifierFloors) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.global .align 4 .u32 global_value;
.shared .align 4 .u32 shared_value;
.shared .align 8 .b64 shared64;
.entry kernel() {
  .reg .u32 %u<2>;
  .reg .u64 %ud<2>;
  .reg .b64 %b<3>;
  .local .align 4 .u32 local_value;
  atom.add.u32 %u0, [global_value], %u1;
  atom.cta.global.add.u32 %u0, [global_value], %u1;
  atom.acquire.global.add.u32 %u0, [global_value], %u1;
  atom.cluster.shared.add.u32 %u0, [shared_value], %u1;
  red.shared::cta.add.u32 [shared_value], %u1;
  red.shared::cluster.add.u32 [shared_value], %u1;
  atom.shared.add.u64 %ud0, [shared64], %ud1;
  atom.shared.cas.b64 %b0, [shared64], %b1, %b2;
  red.shared.add.u64 [shared64], %ud1;
  red.shared.add.u32 [shared_value+2], %u1;
  atom.add.u32 %u0, [local_value], %u1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  const auto check_at = [&](size_t index, uint16_t major, uint16_t minor,
                            uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index]))
      return checker::check(*atom, context);
    return checker::check(std::get<Red>(body[index]), context);
  };
  EXPECT_TRUE(check_at(0, 2, 0, 20).has_value());
  EXPECT_FALSE(check_at(0, 1, 2, 20).has_value());
  EXPECT_FALSE(check_at(0, 2, 0, 19).has_value());
  EXPECT_TRUE(check_at(1, 5, 0, 60).has_value());
  EXPECT_FALSE(check_at(1, 4, 9, 60).has_value());
  EXPECT_TRUE(check_at(2, 6, 0, 70).has_value());
  EXPECT_FALSE(check_at(2, 5, 9, 70).has_value());
  EXPECT_TRUE(check_at(3, 7, 8, 90).has_value());
  EXPECT_FALSE(check_at(3, 7, 7, 90).has_value());
  EXPECT_TRUE(check_at(4, 7, 8, 30).has_value());
  EXPECT_FALSE(check_at(4, 7, 8, 29).has_value());
  EXPECT_TRUE(check_at(5, 7, 8, 90).has_value());
  EXPECT_FALSE(check_at(5, 7, 8, 89).has_value());
  for (size_t index : {6u, 7u, 8u}) {
    EXPECT_TRUE(check_at(index, 2, 0, 20).has_value());
    EXPECT_FALSE(check_at(index, 1, 2, 20).has_value());
    EXPECT_FALSE(check_at(index, 2, 0, 19).has_value());
  }
  const auto misaligned = check_at(9, 9, 3, 90);
  ASSERT_FALSE(misaligned.has_value());
  EXPECT_EQ(misaligned.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  const auto wrong_generic = check_at(10, 9, 3, 90);
  ASSERT_FALSE(wrong_generic.has_value());
  EXPECT_EQ(wrong_generic.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  auto& atom = std::get<Atom>(body[1]);
  atom.address_qualifier.value = AtomicAddressQualifier::Shared;
  const auto mismatch = check_at(1, 9, 3, 90);
  ASSERT_FALSE(mismatch.has_value());
  EXPECT_EQ(mismatch.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
}

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
    EXPECT_EQ(atom.variant.index(), index / 2);
  }
  for (size_t index = 0; index != 12; ++index) {
    const auto& red = std::get<Red>(body[index + 14]);
    EXPECT_EQ(red.variant.index(), index / 2);
  }
  const auto& legacy_cas =
      std::get<Atom::GlobalCasB32>(std::get<Atom>(body[12]).variant);
  const auto& modern_cas =
      std::get<Atom::GlobalCasB32>(std::get<Atom>(body[13]).variant);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(legacy_cas.compare.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(legacy_cas.swap.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(modern_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(modern_cas.swap.value));
  const auto& atom_source =
      std::get<0>(std::get<Atom::GlobalAddU32>(std::get<Atom>(body[0]).variant)
                      .operands)
          .src.value;
  const auto& red_source =
      std::get<0>(
          std::get<Red::GlobalAddU32>(std::get<Red>(body[14]).variant).operands)
          .src.value;
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
    EXPECT_EQ(std::get<Atom>(body[index]).variant.index(), index / 2 + 7);
  for (size_t index = 0; index != 10; ++index)
    EXPECT_EQ(std::get<Red>(body[index + 12]).variant.index(), index / 2 + 6);
  const auto& inc =
      std::get<Atom::GlobalIncU32>(std::get<Atom>(body[0]).variant);
  const auto& exch =
      std::get<Atom::GlobalExchB32>(std::get<Atom>(body[11]).variant);
  const auto& red =
      std::get<Red::GlobalXorB32>(std::get<Red>(body.back()).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      std::get<0>(inc.operands).src.value));
  EXPECT_EQ(inc.type, ScalarType::U32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      std::get<0>(exch.operands).src.value));
  EXPECT_EQ(exch.type, ScalarType::B32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      std::get<0>(red.operands).src.value));
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
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value()) << instruction;
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
  auto& add_source =
      std::get<ResolvedRegisterRef>(std::get<0>(add.operands).src.value);
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
      std::get<0>(std::get<Atom::GlobalXorB32>(std::get<Atom>(body[2]).variant)
                      .operands)
          .src.value);
  xor_src.declared_type = ScalarType::U64;
  const auto invalid_xor =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_xor.has_value());
  EXPECT_EQ(invalid_xor.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  xor_src.declared_type = ScalarType::B32;
  auto& red_src = std::get<ResolvedRegisterRef>(
      std::get<0>(
          std::get<Red::GlobalDecU32>(std::get<Red>(body[3]).variant).operands)
          .src.value);
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
                13 + pair);
  }
  for (size_t pair = 0; pair != 8; ++pair) {
    for (size_t cohort = 0; cohort != 3; ++cohort)
      EXPECT_EQ(std::get<Red>(body[30 + pair * 3 + cohort]).variant.index(),
                11 + pair);
  }
  const auto& atom_add =
      std::get<Atom::GlobalAddU64>(std::get<Atom>(body[0]).variant);
  const auto& atom_cas =
      std::get<Atom::GlobalCasB64>(std::get<Atom>(body[29]).variant);
  const auto& red_xor =
      std::get<Red::GlobalXorB64>(std::get<Red>(body.back()).variant);
  EXPECT_EQ(atom_add.type, ScalarType::U64);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      std::get<0>(atom_add.operands).src.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(atom_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(atom_cas.swap.value));
  EXPECT_EQ(red_xor.type, ScalarType::B64);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      std::get<0>(red_xor.operands).src.value));
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
           "atom.shared.add.u64 %u0, [global_value_64], %u1;",
           "red.acquire.cta.global.add.u64 [global_value_64], %u1;",
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
  auto& add_src =
      std::get<ResolvedRegisterRef>(std::get<0>(add.operands).src.value);
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
  auto& red_src =
      std::get<ResolvedRegisterRef>(std::get<0>(red.operands).src.value);
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
        const size_t expected = type;
        EXPECT_EQ(std::get<Atom>(body[local]).variant.index(), 23 + expected);
        EXPECT_EQ(std::get<Red>(body[24 + local]).variant.index(),
                  19 + expected);
      }
    }
  }
  const auto& atom_bits =
      std::get<Atom::GlobalAddF32>(std::get<Atom>(body[1]).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      std::get<0>(atom_bits.operands).src.value));
  const auto& red_literal =
      std::get<Red::GlobalAddF64>(std::get<Red>(body[24 + 12 + 3]).variant);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      std::get<0>(red_literal.operands).src.value));
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
           "atom.shared.add.f32 %f0, [g32], %f1;",
           "red.acquire.cta.global.add.f64 [g64], %fd1;",
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
  auto& atom_src =
      std::get<ResolvedRegisterRef>(std::get<0>(atom.operands).src.value);
  atom_src.declared_type = ScalarType::U32;
  const auto invalid_atom =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_atom.has_value());
  EXPECT_EQ(invalid_atom.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  atom_src.declared_type = ScalarType::B32;
  auto& red_src =
      std::get<ResolvedRegisterRef>(std::get<0>(red.operands).src.value);
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

/** Resolve the added scalar tuples and preserve discarded atomic results. */
TEST(AtomicReductionCoverage, ResolvesHalfBfloatAndWideScalarForms) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 2 .b16 global16;
.global .align 4 .b32 global32;
.global .align 16 .b128 global128;
.shared .align 2 .b16 shared16;
.shared .align 4 .b32 shared32;
.shared .align 16 .b128 shared128;
.entry kernel() {
  .reg .b16 %h<4>;
  .reg .b32 %b<4>;
  .reg .b128 %q<4>;
  atom.global.cas.b16 _, [global16], %h1, %h2;
  atom.global.cas.b128 %q0, [global128], %q1, %q2;
  atom.shared.exch.b128 _, [shared128], %q1;
  atom.global.add.noftz.f16 _, [global16], %h1;
  red.shared.add.noftz.f16 [shared16], %h1;
  atom.global.add.noftz.f16x2 %b0, [global32], %b1;
  red.shared.add.noftz.f16x2 [shared32], %b1;
  atom.acquire.cluster.global.add.noftz.bf16 %h0, [global16], %h1;
  red.release.shared.add.noftz.bf16 [shared16], %h1;
  atom.global.add.noftz.bf16x2 %b0, [global32], %b1;
  red.shared.add.noftz.bf16x2 [shared32], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 11u);
  EXPECT_FALSE(std::get<Atom::GlobalCasB16>(std::get<Atom>(body[0]).variant)
                   .dst.value.register_ref.has_value());
  EXPECT_TRUE(std::get<Atom::GlobalCasB128>(std::get<Atom>(body[1]).variant)
                  .dst.value.register_ref.has_value());
  EXPECT_FALSE(std::get<0>(std::get<Atom::GlobalExchB128>(
                               std::get<Atom>(body[2]).variant)
                               .operands)
                   .dst.value.register_ref.has_value());
  EXPECT_FALSE(std::get<0>(std::get<Atom::GlobalAddNoftzF16>(
                               std::get<Atom>(body[3]).variant)
                               .operands)
                   .dst.value.register_ref.has_value());
  EXPECT_TRUE(std::holds_alternative<Red::GlobalAddNoftzF16>(
      std::get<Red>(body[4]).variant));
  EXPECT_TRUE(std::holds_alternative<Atom::GlobalAddNoftzF16x2>(
      std::get<Atom>(body[5]).variant));
  EXPECT_TRUE(std::holds_alternative<Red::GlobalAddNoftzF16x2>(
      std::get<Red>(body[6]).variant));
  EXPECT_TRUE(std::holds_alternative<Atom::GlobalAddNoftzBf16>(
      std::get<Atom>(body[7]).variant));
  EXPECT_TRUE(std::holds_alternative<Red::GlobalAddNoftzBf16>(
      std::get<Red>(body[8]).variant));
  EXPECT_TRUE(std::holds_alternative<Atom::GlobalAddNoftzBf16x2>(
      std::get<Atom>(body[9]).variant));
  EXPECT_TRUE(std::holds_alternative<Red::GlobalAddNoftzBf16x2>(
      std::get<Red>(body[10]).variant));
}

/** Recheck tuple floors and reject invalid scalar shapes after resolution. */
TEST(AtomicReductionCoverage, ChecksHalfBfloatAndWideScalarContracts) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 2 .b16 global16;
.global .align 4 .b32 global32;
.global .align 16 .b128 global128;
.entry kernel() {
  .reg .b16 %h<4>;
  .reg .b32 %b<4>;
  .reg .b128 %q<4>;
  atom.global.cas.b16 _, [global16], %h1, %h2;
  atom.global.cas.b128 %q0, [global128], %q1, %q2;
  atom.sys.global.exch.b128 %q0, [global128], %q1;
  atom.global.add.noftz.f16x2 %b0, [global32], %b1;
  red.global.add.noftz.f16x2 [global32], %b1;
  atom.global.add.noftz.bf16 %h0, [global16], %h1;
  red.global.add.noftz.bf16x2 [global32], %b1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  const auto check_at = [&](size_t index, uint16_t major, uint16_t minor,
                            uint32_t sm) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor}, .sm_version = sm}};
    if (const auto* atom = std::get_if<Atom>(&body[index]))
      return checker::check(*atom, context);
    return checker::check(std::get<Red>(body[index]), context);
  };
  for (const auto& [index, major, minor, sm] :
       {std::tuple<size_t, uint16_t, uint16_t, uint32_t>{0, 6, 3, 70},
        {1, 8, 3, 90},
        {2, 8, 4, 90},
        {3, 6, 2, 60},
        {4, 6, 2, 60},
        {5, 7, 8, 90},
        {6, 7, 8, 90}}) {
    EXPECT_TRUE(check_at(index, major, minor, sm).has_value()) << index;
    EXPECT_FALSE(check_at(index, major, minor - 1, sm).has_value()) << index;
    EXPECT_FALSE(check_at(index, major, minor, sm - 1).has_value()) << index;
  }
  auto& cas = std::get<Atom::GlobalCasB128>(std::get<Atom>(body[1]).variant);
  cas.compare.value.declared_type = ScalarType::B64;
  EXPECT_FALSE(check_at(1, 9, 3, 90).has_value());
  cas.compare.value.declared_type = ScalarType::B128;
  EXPECT_TRUE(check_at(1, 9, 3, 90).has_value());
}

/** Reject unsupported tuples, missing noftz, and malformed wide operands. */
TEST(AtomicReductionCoverage, RejectsInvalidHalfBfloatAndWideScalarForms) {
  for (std::string_view instruction : {
           "atom.global.add.f16 %h0, [global16], %h1;",
           "red.global.add.bf16 [global16], %h1;",
           "atom.global.add.noftz.f32 %b0, [global32], %b1;",
           "atom.global.cas.b128 %q0, [global128], %q1;",
           "atom.global.cas.b128 %q0, [global128], 1, %q2;",
           "atom.global.cas.b128 %q0, [global128], %d0, %q2;",
           "red.global.cas.b16 [global16], %h1, %h2;",
           "atom.global.cas.b128 %q0, [global128+8], %q1, %q2;",
           "atom.shared.add.noftz.f16 %h0, [global16], %h1;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_90\n"
        ".global .align 2 .b16 global16;\n"
        ".global .align 4 .b32 global32;\n"
        ".global .align 16 .b128 global128;\n"
        ".entry kernel() {\n"
        ".reg .b16 %h<3>; .reg .b32 %b<3>; .reg .b64 %d<3>; "
        ".reg .b128 %q<3>;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value()) << instruction;
  }
}

/** Keep cache hints, policy registers, and target floors in owned scalar IR. */
TEST(AtomicReductionCoverage, ResolvesScalarCacheHints) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 4 .b32 global32;
.global .align 2 .b16 global16;
.entry kernel() {
  .reg .b16 %h<2>;
  .reg .b32 %b<3>;
  .reg .b64 %policy;
  .reg .u64 %unknown_address;
  .reg .f32 %f<2>;
  atom.global.add.L2::cache_hint.u32 _, [global32], %b1, %policy;
  red.global.and.L2::cache_hint.b32 [global32], %b1, %policy;
  atom.global.add.L2::cache_hint.f32 %f0, [global32], %f1, %policy;
  red.global.add.noftz.L2::cache_hint.f16 [global16], %h1, %policy;
  atom.global.exch.L2::cache_hint.b32 %b0, [global32], %b1, %policy;
  atom.add.L2::cache_hint.u32 _, [global32], %b1, %policy;
  atom.global.relaxed.cta.add.L2::cache_hint.u32 %b0, [global32], %b1, %policy;
  red.release.gpu.global.and.L2::cache_hint.b32 [global32], %b1, %policy;
  atom.add.L2::cache_hint.u32 _, [%unknown_address], %b1, %policy;
  red.and.L2::cache_hint.b32 [%unknown_address], %b1, %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  auto& add = std::get<Atom::GlobalAddU32>(std::get<Atom>(body[0]).variant);
  EXPECT_TRUE(add.cache_hint.value);
  EXPECT_EQ(add.operand_layout.value, 1u);
  EXPECT_FALSE(std::get<1>(add.operands).dst.value.register_ref.has_value());
  EXPECT_EQ(std::get<1>(add.operands).cache_policy.value.declared_type,
            ScalarType::B64);
  EXPECT_EQ(std::get<Atom>(body[8]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  EXPECT_EQ(std::get<Red>(body[9]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  for (size_t index = 0; index < body.size(); ++index) {
    const checker::Context supported{
        .target = {.ptx_version = {7, 4}, .sm_version = 80}};
    const checker::Context old_ptx{
        .target = {.ptx_version = {7, 3}, .sm_version = 80}};
    const checker::Context old_sm{
        .target = {.ptx_version = {7, 4}, .sm_version = 75}};
    const auto check_at = [&](const checker::Context& context) {
      if (const auto* atom = std::get_if<Atom>(&body[index]))
        return checker::check(*atom, context);
      return checker::check(std::get<Red>(body[index]), context);
    };
    EXPECT_TRUE(check_at(supported).has_value()) << index;
    EXPECT_FALSE(check_at(old_ptx).has_value()) << index;
    EXPECT_FALSE(check_at(old_sm).has_value()) << index;
  }
  add.cache_hint.value = false;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  EXPECT_FALSE(checker::check(std::get<Atom>(body[0]), context).has_value());
  add.cache_hint.value = true;
  EXPECT_TRUE(checker::check(std::get<Atom>(body[0]), context).has_value());
  auto& policy = std::get<1>(add.operands).cache_policy.value;
  policy.declared_type = ScalarType::B32;
  EXPECT_FALSE(checker::check(std::get<Atom>(body[0]), context).has_value());
  policy.declared_type = ScalarType::B64;
  EXPECT_TRUE(checker::check(std::get<Atom>(body[0]), context).has_value());
}

/** Reject cache-policy operands that lack an eligible suffix or address. */
TEST(AtomicReductionCoverage, RejectsInvalidScalarCacheHints) {
  for (std::string_view instruction : {
           "atom.global.add.L2::cache_hint.u32 %b0, [global32], %b1;",
           "atom.global.add.u32 %b0, [global32], %b1, %policy;",
           "atom.shared.add.L2::cache_hint.u32 %b0, [shared32], %b1, %policy;",
           "atom.add.L2::cache_hint.u32 %b0, [shared32], %b1, %policy;",
           "red.shared.and.L2::cache_hint.b32 [shared32], %b1, %policy;",
           "atom.global.add.L2::cache_hint.u32 %b0, [global32], %b1, %b2;",
           "atom.global.add.L2::cache_hint.u32 %b0, [global32], %policy, %b1;",
           "atom.global.cas.L2::cache_hint.b32 %b0, [global32], %b1, %b2, "
           "%policy;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_90\n"
        ".global .align 4 .b32 global32;\n"
        ".shared .align 4 .b32 shared32;\n"
        ".entry kernel() {\n"
        ".reg .b32 %b<3>; .reg .b64 %policy;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value()) << instruction;
  }
}

/** Resolve legal vector tuples in both supported suffix orders. */
TEST(AtomicReductionCoverage, ResolvesCompleteVectorAtomicMatrix) {
  std::string source = R"ptx(
.version 9.3
.target sm_90
.global .align 16 .b8 global_value[32];
.entry kernel() {
  .reg .b16 %h<16>;
  .reg .b32 %b<16>;
  .reg .f32 %f<16>;
  .reg .b64 %policy;
)ptx";
  const auto lanes = [](std::string_view prefix, unsigned start,
                        unsigned count) {
    std::string value = "{";
    for (unsigned index = 0; index < count; ++index) {
      if (index != 0)
        value += ", ";
      value += std::string(prefix) + std::to_string(start + index);
    }
    return value + "}";
  };
  size_t count = 0;
  for (std::string_view opcode : {"atom", "red"}) {
    for (std::string_view type : {"f16", "bf16", "f16x2", "bf16x2", "f32"}) {
      const unsigned maximum_width = type == "f16" || type == "bf16" ? 8 : 4;
      const std::string_view prefix = type == "f32"        ? "%f"
                                      : maximum_width == 8 ? "%h"
                                                           : "%b";
      for (std::string_view operation : {"add", "min", "max"}) {
        if (type == "f32" && operation != "add")
          continue;
        for (unsigned width : {2u, 4u, 8u}) {
          if (width > maximum_width)
            continue;
          for (bool official_order : {false, true}) {
            for (bool hinted : {false, true}) {
              if (hinted && (!official_order || width != 2))
                continue;
              source += std::string(opcode) + ".global";
              if (official_order)
                source += "." + std::string(operation);
              if (official_order && type != "f32")
                source += ".noftz";
              if (official_order && hinted)
                source += ".L2::cache_hint";
              source += ".v" + std::to_string(width) + "." + std::string(type);
              if (!official_order)
                source += "." + std::string(operation);
              if (!official_order && type != "f32")
                source += ".noftz";
              source += " ";
              if (opcode == "atom")
                source += lanes(prefix, 0, width) + ", ";
              source += "[global_value], " + lanes(prefix, 8, width);
              if (hinted)
                source += ", %policy";
              source += ";\n";
              ++count;
            }
          }
        }
      }
    }
  }
  source += "}\n";
  ASSERT_EQ(count, 154u);
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 154u);
  for (size_t index = 0; index < body.size(); ++index) {
    if (index < 77) {
      const auto& atom = std::get<Atom>(body[index]);
      EXPECT_EQ(atom.address_qualifier.value, AtomicAddressQualifier::Global);
      EXPECT_GE(atom.variant.index(), 32u);
    } else {
      const auto& red = std::get<Red>(body[index]);
      EXPECT_EQ(red.address_qualifier.value, AtomicAddressQualifier::Global);
      EXPECT_GE(red.variant.index(), 25u);
    }
  }
}

/** Preserve unknown lane types through standalone atomic resolution and checking. */
TEST(AtomicReductionCoverage, KeepsStandaloneVectorLaneTypesUnknown) {
  const checker::Context target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  const auto atom_ast = test_helpers::parseInstruction(
      "atom.global.add.v2.f32 {%f0, %f1}, [%rd0], {%f2, %f3};");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(atom_ast);
  const auto atom_resolved = resolveInstruction(*atom_ast);
  ASSERT_TRUE(atom_resolved.has_value()) << atom_resolved.error().message;
  const auto& atom = std::get<Atom>(*atom_resolved);
  const auto& atom_operands =
      std::get<0>(std::get<Atom::VectorAddF32>(atom.variant).operands);
  for (const auto& lane : atom_operands.dst.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  for (const auto& lane : atom_operands.src.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  EXPECT_TRUE(checker::check(atom, target).has_value());

  const auto red_ast = test_helpers::parseInstruction(
      "red.global.add.v2.f32 [%rd0], {%f2, %f3};");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(red_ast);
  const auto red_resolved = resolveInstruction(*red_ast);
  ASSERT_TRUE(red_resolved.has_value()) << red_resolved.error().message;
  const auto& red = std::get<Red>(*red_resolved);
  const auto& red_operands =
      std::get<0>(std::get<Red::VectorAddF32>(red.variant).operands);
  for (const auto& lane : red_operands.src.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  EXPECT_TRUE(checker::check(red, target).has_value());
}

/** Recheck written atomic suffix domains after syntax ownership is released. */
TEST(AtomicReductionCoverage, RejectsOwnedAtomicQualifierDomainMutations) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.shared .align 8 .b64 barrier;
.shared .align 4 .u32 shared_value;
.global .align 8 .b8 global_value[16];
.entry kernel() {
  .reg .u64 %a;
  .reg .u32 %u;
  .reg .f32 %f<4>;
  red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes.inc.u32
      [%a], %u, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.inc.u32
      [%a], %u, [barrier];
  red.async.release.gpu.global.add.u32 [%a], %u;
  atom.global.add.v2.f32 {%f0, %f1}, [global_value], {%f2, %f3};
  atom.shared.add.u32 %u, [shared_value], %u;
}
)ptx";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const checker::Context target{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  auto& shared_async = std::get<Red>(body[0]);
  EXPECT_TRUE(checker::check(shared_async, target).has_value());
  EXPECT_TRUE(checker::check(std::get<Red>(body[1]), target).has_value());
  EXPECT_TRUE(checker::check(std::get<Red>(body[2]), target).has_value());
  EXPECT_TRUE(checker::check(std::get<Atom>(body[3]), target).has_value());
  EXPECT_TRUE(checker::check(std::get<Atom>(body[4]), target).has_value());
  EXPECT_EQ(shared_async.address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  ASSERT_FALSE(shared_async.address_qualifier.locs.empty());
  for (auto invalid :
       {AtomicAddressQualifier::Shared, AtomicAddressQualifier::SharedCta,
        static_cast<AtomicAddressQualifier>(255)}) {
    shared_async.address_qualifier.value = invalid;
    const auto result = checker::check(shared_async, target);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
    EXPECT_EQ(result.error().front().range,
              shared_async.address_qualifier.locs.front());
  }
  shared_async.address_qualifier.value = AtomicAddressQualifier::SharedCluster;
  EXPECT_TRUE(checker::check(shared_async, target).has_value());
  auto& generic_async = std::get<Red>(body[1]);
  generic_async.address_qualifier.value = AtomicAddressQualifier::Shared;
  EXPECT_FALSE(checker::check(generic_async, target).has_value());
  generic_async.address_qualifier.value = AtomicAddressQualifier::Generic;
  EXPECT_TRUE(checker::check(generic_async, target).has_value());
  auto& release = std::get<Red>(body[2]);
  release.address_qualifier.value = AtomicAddressQualifier::Shared;
  const auto release_check = checker::check(release, target);
  ASSERT_FALSE(release_check.has_value());
  EXPECT_EQ(release_check.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
  auto& vector = std::get<Atom>(body[3]);
  vector.address_qualifier.value = AtomicAddressQualifier::SharedCta;
  const auto vector_check = checker::check(vector, target);
  ASSERT_FALSE(vector_check.has_value());
  EXPECT_EQ(vector_check.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
}

/** Check cache policy, generic addresses, lane sinks, and vector target floors. */
TEST(AtomicReductionCoverage, ChecksVectorAtomicOwnedContracts) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 16 .b8 global_value[32];
.entry kernel() {
  .reg .b16 %h<8>;
  .reg .f16 %hf<2>;
  .reg .u16 %uh<2>;
  .reg .s16 %sh<2>;
  .reg .b32 %b<8>;
  .reg .f32 %f<8>;
  .reg .b64 %policy;
  .reg .u64 %unknown_address;
  atom.global.v2.f16.add.noftz {_, %h1}, [global_value], {%h2, %h3};
  atom.acquire.cta.global.v4.f16x2.min.noftz.L2::cache_hint
      {%b0, %b1, %b2, %b3}, [global_value], {%b4, %b5, %b6, %b7}, %policy;
  red.release.gpu.v2.f32.add.L2::cache_hint
      [%unknown_address], {%f4, %f5}, %policy;
  atom.global.v2.f16.add.noftz {%hf0, %hf1}, [global_value], {%hf0, %hf1};
  atom.global.v2.f16.add.noftz {%uh0, %uh1}, [global_value], {%uh0, %uh1};
  atom.global.v2.f16.add.noftz {%sh0, %sh1}, [global_value], {%sh0, %sh1};
  atom.global.v2.f16.add.noftz {%h0, %uh0}, [global_value], {%h1, %uh1};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 7u);
  auto& first =
      std::get<Atom::VectorAddNoftzF16>(std::get<Atom>(body[0]).variant);
  EXPECT_EQ(first.vector.value, VectorArity::V2);
  EXPECT_FALSE(std::get<0>(first.operands).dst.value.elements[0].has_value());
  auto& hinted =
      std::get<Atom::VectorMinNoftzF16x2>(std::get<Atom>(body[1]).variant);
  EXPECT_TRUE(hinted.cache_hint.value);
  EXPECT_EQ(hinted.vector.value, VectorArity::V4);
  EXPECT_EQ(std::get<1>(hinted.operands).cache_policy.value.declared_type,
            ScalarType::B64);
  EXPECT_EQ(std::get<Red>(body[2]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  const checker::Context supported{
      .target = {.ptx_version = {8, 1}, .sm_version = 90}};
  const checker::Context old_ptx{
      .target = {.ptx_version = {8, 0}, .sm_version = 90}};
  const checker::Context old_sm{
      .target = {.ptx_version = {8, 1}, .sm_version = 89}};
  for (auto& instruction : body) {
    const auto check = [&](const checker::Context& context) {
      if (const auto* atom = std::get_if<Atom>(&instruction))
        return checker::check(*atom, context);
      return checker::check(std::get<Red>(instruction), context);
    };
    EXPECT_TRUE(check(supported).has_value());
    EXPECT_FALSE(check(old_ptx).has_value());
    EXPECT_FALSE(check(old_sm).has_value());
  }
  hinted.vector.value = VectorArity::V8;
  EXPECT_FALSE(checker::check(std::get<Atom>(body[1]), supported).has_value());
  hinted.vector.value = VectorArity::V4;
  hinted.cache_hint.value = false;
  EXPECT_FALSE(checker::check(std::get<Atom>(body[1]), supported).has_value());
  hinted.cache_hint.value = true;
  std::get<1>(hinted.operands).cache_policy.value.declared_type =
      ScalarType::B32;
  EXPECT_FALSE(checker::check(std::get<Atom>(body[1]), supported).has_value());
  auto& integer_lanes =
      std::get<Atom::VectorAddNoftzF16>(std::get<Atom>(body[4]).variant);
  std::get<0>(integer_lanes.operands).src.value.elements[0]->declared_type =
      ScalarType::F16;
  EXPECT_FALSE(checker::check(std::get<Atom>(body[4]), supported).has_value());
}

/** Reject invalid vector widths, lanes, address spaces, and policy layouts. */
TEST(AtomicReductionCoverage, RejectsInvalidVectorAtomicForms) {
  for (std::string_view instruction : {
           "atom.global.v8.f16x2.add.noftz {%b0, %b1}, [global_value], {%b2, "
           "%b3};",
           "red.global.v8.f32.add [global_value], {%f0, %f1};",
           "atom.global.v2.f32.min {%f0, %f1}, [global_value], {%f2, %f3};",
           "atom.global.add.f32.v2 {%f0, %f1}, [global_value], {%f2, %f3};",
           "red.global.add.v2.noftz.f32 [global_value], {%f0, %f1};",
           "red.global.v2.f16.add [global_value], {%h0, %h1};",
           "atom.global.v2.f16.add.noftz {%h0}, [global_value], {%h1, %h2};",
           "atom.global.v2.f16.add.noftz {%h0, %h1}, [global_value], {%b0, "
           "%b1};",
           "atom.global.v2.f16.add.noftz {%hf0, %uh0}, [global_value], {%h1, "
           "%h2};",
           "red.global.v2.bf16.add.noftz [global_value], {%uh0, %uh1};",
           "atom.global.v2.f16x2.add.noftz {%f0, %f1}, [global_value], {%b0, "
           "%b1};",
           "atom.global.v2.f16.add.noftz {_, _}, [global_value], {%h1, %h2};",
           "red.global.v2.f16.add.noftz [global_value], {_, %h1};",
           "atom.global.v2.f16.add.noftz {%h0, %h1}, [misaligned], {%h2, %h3};",
           "red.shared.v2.f16.add.noftz [shared_value], {%h0, %h1};",
           "red.v2.f16.add.noftz [shared_value], {%h0, %h1};",
           "atom.global.v2.f32.add.L2::cache_hint {%f0, %f1}, [global_value], "
           "{%f2, %f3};",
           "red.global.v2.f32.add [global_value], {%f0, %f1}, %policy;",
           "red.global.v2.f32.add.L2::cache_hint [global_value], {%f0, %f1}, "
           "%b0;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_90\n"
        ".global .align 16 .b8 global_value[32];\n"
        ".global .align 2 .b16 misaligned;\n"
        ".shared .align 16 .b8 shared_value[32];\n"
        ".entry kernel() {\n"
        ".reg .b16 %h<8>; .reg .f16 %hf<4>; .reg .u16 %uh<4>; "
        ".reg .b32 %b<8>; .reg .f32 %f<8>; "
        ".reg .b64 %policy;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value()) << instruction;
  }
}

/** Resolve both asynchronous reduction modes and retain their written suffixes. */
TEST(AtomicReductionCoverage, ResolvesAsyncReductionModes) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.shared .align 8 .b64 barrier;
.entry kernel() {
  .reg .u64 %a;
  .reg .u32 %u;
  .reg .s32 %s;
  .reg .b32 %b;
  .reg .u64 %ud;
  .reg .s64 %sd;
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.inc.u32 [%a], %u, [barrier];
  red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes.dec.u32 [%a+4], %u, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.u32 [%a], %u, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.s32 [%a], %s, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.u32 [%a], %u, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.s32 [%a], %s, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.and.b32 [%a], %b, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.or.b32 [%a], %b, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.xor.b32 [%a], %b, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 [%a], 1, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.s32 [%a], %s, [barrier];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u64 [%a+8], %ud, [barrier];
  red.async.release.gpu.add.u32 [%a], 1;
  red.async.release.sys.global.add.s32 [%a], %s;
  red.async.mmio.release.sys.global.add.u64 [%a+8], %ud;
  red.async.release.sys.add.s64 [%a], %sd;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 16u);
  EXPECT_EQ(std::get<Red>(body[0]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  EXPECT_EQ(std::get<Red>(body[1]).address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  EXPECT_EQ(std::get<Red>(body[13]).address_qualifier.value,
            AtomicAddressQualifier::Global);
  EXPECT_TRUE(std::get<Red::AsyncReleaseAddU64>(std::get<Red>(body[14]).variant)
                  .mmio.value);
  const checker::Context shared_target{
      .target = {.ptx_version = {8, 1}, .sm_version = 90}};
  const checker::Context release_target{
      .target = {.ptx_version = {8, 7}, .sm_version = 100}};
  const checker::Context old_shared_ptx{
      .target = {.ptx_version = {8, 0}, .sm_version = 90}};
  const checker::Context old_shared_sm{
      .target = {.ptx_version = {8, 1}, .sm_version = 89}};
  const checker::Context old_release_ptx{
      .target = {.ptx_version = {8, 6}, .sm_version = 100}};
  const checker::Context old_release_sm{
      .target = {.ptx_version = {8, 7}, .sm_version = 99}};
  for (size_t index = 0; index < body.size(); ++index) {
    const auto& red = std::get<Red>(body[index]);
    if (index < 12) {
      EXPECT_TRUE(checker::check(red, shared_target).has_value()) << index;
      EXPECT_FALSE(checker::check(red, old_shared_ptx).has_value()) << index;
      EXPECT_FALSE(checker::check(red, old_shared_sm).has_value()) << index;
    } else {
      EXPECT_TRUE(checker::check(red, release_target).has_value()) << index;
      EXPECT_FALSE(checker::check(red, old_release_ptx).has_value()) << index;
      EXPECT_FALSE(checker::check(red, old_release_sm).has_value()) << index;
      EXPECT_FALSE(checker::check(red, shared_target).has_value()) << index;
    }
  }
  auto& mmio =
      std::get<Red::AsyncReleaseAddU64>(std::get<Red>(body[14]).variant);
  mmio.scope.value = MemoryScope::Gpu;
  EXPECT_FALSE(
      checker::check(std::get<Red>(body[14]), release_target).has_value());
  mmio.scope.value = MemoryScope::Sys;
  std::get<Red>(body[1]).address_qualifier.value =
      AtomicAddressQualifier::Global;
  EXPECT_FALSE(
      checker::check(std::get<Red>(body[1]), release_target).has_value());
  auto& shared =
      std::get<Red::AsyncSharedIncU32>(std::get<Red>(body[0]).variant);
  shared.address.value.base =
      std::get<ResolvedSymbolRef>(shared.mbarrier.value.base);
  EXPECT_FALSE(
      checker::check(std::get<Red>(body[0]), shared_target).has_value());
  auto& mbarrier =
      std::get<Red::AsyncSharedMinU32>(std::get<Red>(body[2]).variant)
          .mbarrier.value;
  std::get<ResolvedSymbolRef>(mbarrier.base).address_state_space =
      base::DeclarationStateSpace::Global;
  EXPECT_FALSE(
      checker::check(std::get<Red>(body[2]), shared_target).has_value());
}

/** Reject mode mixing, nonregister destinations, and invalid barrier addresses. */
TEST(AtomicReductionCoverage, RejectsInvalidAsyncReductionForms) {
  for (std::string_view instruction : {
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.s64 "
           "[%a], %sd, [barrier];",
           "red.async.relaxed.cluster.add.u32 [%a], %u, [barrier];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u;",
           "red.async.relaxed.gpu.mbarrier::complete_tx::bytes.add.u32 [%a], "
           "%u, [barrier];",
           "red.async.mmio.relaxed.cluster.mbarrier::complete_tx::bytes.add."
           "u32 [%a], %u, [barrier];",
           "red.async.relaxed.cluster.global.mbarrier::complete_tx::bytes.add."
           "u32 [%a], %u, [barrier];",
           "red.async.release.cta.add.u32 [%a], %u;",
           "red.async.mmio.release.gpu.add.u32 [%a], %u;",
           "red.async.release.sys.shared::cluster.add.u32 [%a], %u;",
           "red.async.release.sys.add.b32 [%a], %b;",
           "red.async.release.sys.min.u32 [%a], %u;",
           "red.async.release.sys.add.u32 [%a], %sd;",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u64 "
           "[%a], %u, [barrier];",
           "red.async.release.sys.add.u32 [global_value], %u;",
           "red.async.release.sys.add.u32 [0], %u;",
           "red.async.release.sys.add.u32 [%a+4294967296], %u;",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[shared_value], %u, [barrier];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [global_value];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [barrier+4];",
           "red.async.release.sys.add.u32 [%a], %u, [barrier];",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_100\n"
        ".shared .align 8 .b64 barrier;\n"
        ".shared .align 4 .u32 shared_value;\n"
        ".global .align 4 .u32 global_value;\n"
        ".entry kernel() {\n.reg .u64 %a; .reg .u64 %ud; "
        ".reg .u32 %u; .reg .s64 %sd; .reg .b32 %b;\n" +
        std::string(instruction) + "\n}\n";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value()) << instruction;
  }
}

/** Keep signed-32 address boundaries valid in source and owned async IR. */
TEST(AtomicReductionCoverage, RechecksAsyncAddressOffsetDomain) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.shared .align 8 .b64 barrier;
.entry kernel() {
  .reg .u64 %a;
  .reg .u32 %u;
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32
      [%a+2147483647], %u, [barrier-2147483648];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32
      [%a-2147483648], %u, [%a+2147483647];
  red.async.release.sys.add.u32 [%a+2147483647], %u;
  red.async.release.sys.add.u32 [%a-2147483648], %u;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  auto& shared =
      std::get<Red::AsyncSharedAddU32>(std::get<Red>(body[0]).variant);
  ASSERT_TRUE(shared.address.value.offset.has_value());
  ASSERT_TRUE(shared.mbarrier.value.offset.has_value());
  shared.address.value.offset->value.bits = 2147483648ULL;
  const auto invalid_destination =
      checker::check(std::get<Red>(body[0]), supported);
  ASSERT_FALSE(invalid_destination.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      invalid_destination.error(),
      [](const checker::CheckDiagnostic& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation;
      }));
  shared.address.value.offset->value.bits = 2147483647ULL;
  shared.address.value.offset->value.integer_source_bits = 2147483647ULL;
  shared.mbarrier.value.offset->value.bits = 2147483649ULL;
  shared.mbarrier.value.offset->value.integer_source_bits = 2147483649ULL;
  const auto invalid_mbarrier =
      checker::check(std::get<Red>(body[0]), supported);
  ASSERT_FALSE(invalid_mbarrier.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      invalid_mbarrier.error(), [](const checker::CheckDiagnostic& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation;
      }));
  shared.mbarrier.value.offset->value.bits = 2147483648ULL;
  shared.mbarrier.value.offset->value.integer_source_bits = 2147483648ULL;
  auto& release =
      std::get<Red::AsyncReleaseAddU32>(std::get<Red>(body[2]).variant);
  release.address.value.offset->value.bits = 2147483648ULL;
  release.address.value.offset->value.integer_source_bits = 2147483648ULL;
  const auto invalid_owned =
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_owned.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      invalid_owned.error(), [](const checker::CheckDiagnostic& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation;
      }));

  for (std::string_view instruction : {
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a+2147483648], %u, [barrier];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a-2147483649], %u, [barrier];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [barrier+2147483648];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [barrier-2147483649];",
           "red.async.release.sys.add.u32 [%a+2147483648], %u;",
           "red.async.release.sys.add.u32 [%a-2147483649], %u;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_100\n.shared .align 8 .b64 barrier;\n"
        ".entry kernel() { .reg .u64 %a; .reg .u32 %u;\n" +
        std::string(instruction) + "\n}\n";
    const auto invalid = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
    EXPECT_FALSE(resolveAndValidateModule(*invalid).has_value()) << instruction;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
