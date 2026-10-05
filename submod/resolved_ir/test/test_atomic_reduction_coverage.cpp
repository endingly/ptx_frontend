#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/matrix/ldmatrix.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/atom.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/red.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Read the atomic suffix from one of the exact forms under test. */
template <typename... Forms>
  requires((std::derived_from<Forms, Instruction> && ...))
std::optional<AtomicAddressQualifier> atomicQualifier(
    const Instruction& instruction) {
  std::optional<AtomicAddressQualifier> qualifier;
  (([&] {
     if (const auto* form = dynamic_cast<const Forms*>(&instruction))
       qualifier = form->address_qualifier.value;
   }()), ...);
  return qualifier;
}

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
    EXPECT_EQ(body[index]->instruction_kind(), InstructionKind::AtomGlobalAddU32);
  for (size_t index = 5; index < 8; ++index)
    EXPECT_EQ(body[index]->instruction_kind(), InstructionKind::RedGlobalAddU32);
  const auto& generic = dynamic_cast<const AtomGlobalAddU32&>(*body[0]);
  const auto& scope_only = dynamic_cast<const AtomGlobalAddU32&>(*body[1]);
  const auto& sem_only = dynamic_cast<const AtomGlobalAddU32&>(*body[2]);
  const auto& cluster = dynamic_cast<const AtomGlobalAddU32&>(*body[3]);
  const auto& shared_cta = dynamic_cast<const AtomGlobalAddU32&>(*body[4]);
  EXPECT_EQ(generic.address_qualifier.value, AtomicAddressQualifier::Generic);
  EXPECT_TRUE(generic.address_qualifier.locs.empty());
  EXPECT_EQ(scope_only.address_qualifier.value, AtomicAddressQualifier::Global);
  EXPECT_EQ(
      scope_only.scope.value,
      MemoryScope::Cta);
  EXPECT_EQ(scope_only.semantics.value,
            MemoryConsistency::Omitted);
  EXPECT_EQ(
      sem_only.semantics.value,
      MemoryConsistency::Acquire);
  EXPECT_EQ(
      sem_only.scope.value,
      MemoryScope::None);
  EXPECT_EQ(cluster.address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  EXPECT_EQ(
      cluster.scope.value,
      MemoryScope::Cluster);
  EXPECT_EQ(shared_cta.address_qualifier.value,
            AtomicAddressQualifier::SharedCta);
  EXPECT_EQ(shared_cta.semantics.value,
            MemoryConsistency::Release);
  EXPECT_EQ(dynamic_cast<const RedGlobalAddU32&>(*body[5]).address_qualifier.value,
            AtomicAddressQualifier::Shared);
  EXPECT_EQ(dynamic_cast<const RedGlobalAddU32&>(*body[7]).address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  dynamic_cast<AtomGlobalAddU32&>(*body[3]).address_qualifier.value =
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
    return body[index]->check(context);
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

  auto& atom = dynamic_cast<AtomGlobalAddU32&>(*body[1]);
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
  constexpr std::array atom_kinds = {
      InstructionKind::AtomGlobalAddU32, InstructionKind::AtomGlobalAddS32,
      InstructionKind::AtomGlobalMinU32, InstructionKind::AtomGlobalMinS32,
      InstructionKind::AtomGlobalMaxU32, InstructionKind::AtomGlobalMaxS32,
      InstructionKind::AtomGlobalCasB32};
  constexpr std::array red_kinds = {
      InstructionKind::RedGlobalAddU32, InstructionKind::RedGlobalAddS32,
      InstructionKind::RedGlobalMinU32, InstructionKind::RedGlobalMinS32,
      InstructionKind::RedGlobalMaxU32, InstructionKind::RedGlobalMaxS32};
  for (size_t index = 0; index != 14; ++index)
    EXPECT_EQ(body[index]->instruction_kind(), atom_kinds[index / 2]);
  for (size_t index = 0; index != 12; ++index)
    EXPECT_EQ(body[index + 14]->instruction_kind(), red_kinds[index / 2]);
  const auto& legacy_cas = dynamic_cast<AtomGlobalCasB32&>(*body[12]);
  const auto& modern_cas = dynamic_cast<AtomGlobalCasB32&>(*body[13]);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      legacy_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      legacy_cas.swap.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      modern_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      modern_cas.swap.value));
  const auto& atom_source = dynamic_cast<const AtomGlobalAddU32&>(*body[0]).src.value;
  const auto& red_source = dynamic_cast<const RedGlobalAddU32&>(*body[14]).src.value;
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(atom_source));
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
  constexpr std::array atom_kinds = {
      InstructionKind::AtomGlobalIncU32, InstructionKind::AtomGlobalDecU32,
      InstructionKind::AtomGlobalAndB32, InstructionKind::AtomGlobalOrB32,
      InstructionKind::AtomGlobalXorB32, InstructionKind::AtomGlobalExchB32};
  constexpr std::array red_kinds = {
      InstructionKind::RedGlobalIncU32, InstructionKind::RedGlobalDecU32,
      InstructionKind::RedGlobalAndB32, InstructionKind::RedGlobalOrB32,
      InstructionKind::RedGlobalXorB32};
  for (size_t index = 0; index != 12; ++index)
    EXPECT_EQ(body[index]->instruction_kind(), atom_kinds[index / 2]);
  for (size_t index = 0; index != 10; ++index)
    EXPECT_EQ(body[index + 12]->instruction_kind(), red_kinds[index / 2]);
  const auto& inc = dynamic_cast<AtomGlobalIncU32&>(*body[0]);
  const auto& exch = dynamic_cast<AtomGlobalExchB32&>(*body[11]);
  const auto& red = dynamic_cast<RedGlobalXorB32&>(*body.back());
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      inc.src.value));
  EXPECT_EQ(inc.type, ScalarType::U32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      exch.src.value));
  EXPECT_EQ(exch.type, ScalarType::B32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      red.src.value));
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
    return body[index]->check(context);
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
    return body[index]->check(context);
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
  const auto atom = body[0]->check(context);
  ASSERT_FALSE(atom.has_value());
  EXPECT_EQ(atom.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const auto red = body[1]->check(context);
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
  EXPECT_EQ(expanded_body[0]->check(context)
                .error()
                .front()
                .kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(expanded_body[1]->check(context)
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
  auto& add = dynamic_cast<AtomGlobalAddU32&>(*body[0]);
  auto& cas = dynamic_cast<AtomGlobalCasB32&>(*body[1]);
  auto& add_source = std::get<ResolvedRegisterRef>(
      add.src.value);
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
      dynamic_cast<AtomGlobalXorB32&>(*body[2]).src.value);
  xor_src.declared_type = ScalarType::U64;
  const auto invalid_xor =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_xor.has_value());
  EXPECT_EQ(invalid_xor.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  xor_src.declared_type = ScalarType::B32;
  auto& red_src = std::get<ResolvedRegisterRef>(
      dynamic_cast<RedGlobalDecU32&>(*body[3]).src.value);
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
  constexpr std::array atom_kinds = {
      InstructionKind::AtomGlobalAddU64, InstructionKind::AtomGlobalMinU64,
      InstructionKind::AtomGlobalMinS64, InstructionKind::AtomGlobalMaxU64,
      InstructionKind::AtomGlobalMaxS64, InstructionKind::AtomGlobalAndB64,
      InstructionKind::AtomGlobalOrB64, InstructionKind::AtomGlobalXorB64,
      InstructionKind::AtomGlobalExchB64, InstructionKind::AtomGlobalCasB64};
  constexpr std::array red_kinds = {
      InstructionKind::RedGlobalAddU64, InstructionKind::RedGlobalMinU64,
      InstructionKind::RedGlobalMinS64, InstructionKind::RedGlobalMaxU64,
      InstructionKind::RedGlobalMaxS64, InstructionKind::RedGlobalAndB64,
      InstructionKind::RedGlobalOrB64, InstructionKind::RedGlobalXorB64};
  for (size_t pair = 0; pair != 10; ++pair) {
    for (size_t cohort = 0; cohort != 3; ++cohort)
      EXPECT_EQ(body[pair * 3 + cohort]->instruction_kind(), atom_kinds[pair]);
  }
  for (size_t pair = 0; pair != 8; ++pair) {
    for (size_t cohort = 0; cohort != 3; ++cohort)
      EXPECT_EQ(body[30 + pair * 3 + cohort]->instruction_kind(), red_kinds[pair]);
  }
  const auto& atom_add = dynamic_cast<AtomGlobalAddU64&>(*body[0]);
  const auto& atom_cas = dynamic_cast<AtomGlobalCasB64&>(*body[29]);
  const auto& red_xor = dynamic_cast<RedGlobalXorB64&>(*body.back());
  EXPECT_EQ(atom_add.type, ScalarType::U64);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      atom_add.src.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      atom_cas.compare.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      atom_cas.swap.value));
  EXPECT_EQ(red_xor.type, ScalarType::B64);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      red_xor.src.value));
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
    return body[index]->check(context);
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
  const auto atom_space =
      body[0]->check(context);
  ASSERT_FALSE(atom_space.has_value());
  EXPECT_EQ(atom_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const auto red_alignment =
      body[1]->check(context);
  ASSERT_FALSE(red_alignment.has_value());
  EXPECT_EQ(red_alignment.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  const auto atom_alignment =
      body[2]->check(context);
  ASSERT_FALSE(atom_alignment.has_value());
  EXPECT_EQ(atom_alignment.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  const auto red_space =
      body[3]->check(context);
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
  auto& add = dynamic_cast<AtomGlobalAddU64&>(*body[0]);
  auto& cas = dynamic_cast<AtomGlobalCasB64&>(*body[1]);
  auto& red = dynamic_cast<RedGlobalXorB64&>(*body[2]);
  auto& add_src = std::get<ResolvedRegisterRef>(
      add.src.value);
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
  auto& red_src = std::get<ResolvedRegisterRef>(
      red.src.value);
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
        EXPECT_EQ(body[local]->instruction_kind(),
                  type == 0 ? InstructionKind::AtomGlobalAddF32
                            : InstructionKind::AtomGlobalAddF64);
        EXPECT_EQ(body[24 + local]->instruction_kind(),
                  type == 0 ? InstructionKind::RedGlobalAddF32
                            : InstructionKind::RedGlobalAddF64);
      }
    }
  }
  const auto& atom_bits = dynamic_cast<AtomGlobalAddF32&>(*body[1]);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      atom_bits.src.value));
  const auto& red_literal = dynamic_cast<RedGlobalAddF64&>(*body[24 + 12 + 3]);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      red_literal.src.value));
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
    return body[index]->check(context);
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
    const auto result = body[index]->check(context);
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
  auto& atom = dynamic_cast<AtomGlobalAddF32&>(*body[0]);
  auto& red = dynamic_cast<RedGlobalAddF64&>(*body[1]);
  auto& atom_src = std::get<ResolvedRegisterRef>(
      atom.src.value);
  atom_src.declared_type = ScalarType::U32;
  const auto invalid_atom =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_atom.has_value());
  EXPECT_EQ(invalid_atom.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  atom_src.declared_type = ScalarType::B32;
  auto& red_src = std::get<ResolvedRegisterRef>(
      red.src.value);
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
  EXPECT_FALSE(dynamic_cast<AtomGlobalCasB16&>(*body[0])
                   .dst.value.register_ref.has_value());
  EXPECT_TRUE(dynamic_cast<AtomGlobalCasB128&>(*body[1])
                  .dst.value.register_ref.has_value());
  EXPECT_FALSE(dynamic_cast<AtomGlobalExchB128&>(*body[2])
                   .dst.value.register_ref.has_value());
  EXPECT_FALSE(dynamic_cast<AtomGlobalAddNoftzF16&>(*body[3])
                   .dst.value.register_ref.has_value());
  EXPECT_EQ(body[4]->instruction_kind(), InstructionKind::RedGlobalAddNoftzF16);
  EXPECT_EQ(body[5]->instruction_kind(), InstructionKind::AtomGlobalAddNoftzF16x2);
  EXPECT_EQ(body[6]->instruction_kind(), InstructionKind::RedGlobalAddNoftzF16x2);
  EXPECT_EQ(body[7]->instruction_kind(), InstructionKind::AtomGlobalAddNoftzBf16);
  EXPECT_EQ(body[8]->instruction_kind(), InstructionKind::RedGlobalAddNoftzBf16);
  EXPECT_EQ(body[9]->instruction_kind(), InstructionKind::AtomGlobalAddNoftzBf16x2);
  EXPECT_EQ(body[10]->instruction_kind(), InstructionKind::RedGlobalAddNoftzBf16x2);
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
    return body[index]->check(context);
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
  auto& cas = dynamic_cast<AtomGlobalCasB128&>(*body[1]);
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
  auto& add = dynamic_cast<AtomGlobalAddU32&>(*body[0]);
  EXPECT_TRUE(add.cache_hint.value);
  EXPECT_EQ(add.operand_layout.value, 1u);
  EXPECT_FALSE(
      add.dst.value.register_ref.has_value());
  ASSERT_TRUE(add.cache_policy.has_value());
  EXPECT_EQ(add.cache_policy->value.declared_type, ScalarType::B64);
  EXPECT_EQ(dynamic_cast<AtomGlobalAddU32&>(*body[8]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  EXPECT_EQ(dynamic_cast<RedGlobalAndB32&>(*body[9]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  for (size_t index = 0; index < body.size(); ++index) {
    const checker::Context supported{
        .target = {.ptx_version = {7, 4}, .sm_version = 80}};
    const checker::Context old_ptx{
        .target = {.ptx_version = {7, 3}, .sm_version = 80}};
    const checker::Context old_sm{
        .target = {.ptx_version = {7, 4}, .sm_version = 75}};
    const auto check_at = [&](const checker::Context& context) {
      return body[index]->check(context);
    };
    EXPECT_TRUE(check_at(supported).has_value()) << index;
    EXPECT_FALSE(check_at(old_ptx).has_value()) << index;
    EXPECT_FALSE(check_at(old_sm).has_value()) << index;
  }
  add.cache_hint.value = false;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  EXPECT_FALSE(
      body[0]->check(context).has_value());
  add.cache_hint.value = true;
  EXPECT_TRUE(
      body[0]->check(context).has_value());
  auto& policy = add.cache_policy->value;
  policy.declared_type = ScalarType::B32;
  EXPECT_FALSE(
      body[0]->check(context).has_value());
  policy.declared_type = ScalarType::B64;
  EXPECT_TRUE(
      body[0]->check(context).has_value());
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
  constexpr std::array atom_vector_kinds = {
      InstructionKind::AtomVectorAddNoftzF16,
      InstructionKind::AtomVectorMinNoftzF16,
      InstructionKind::AtomVectorMaxNoftzF16,
      InstructionKind::AtomVectorAddNoftzBf16,
      InstructionKind::AtomVectorMinNoftzBf16,
      InstructionKind::AtomVectorMaxNoftzBf16,
      InstructionKind::AtomVectorAddNoftzF16x2,
      InstructionKind::AtomVectorMinNoftzF16x2,
      InstructionKind::AtomVectorMaxNoftzF16x2,
      InstructionKind::AtomVectorAddNoftzBf16x2,
      InstructionKind::AtomVectorMinNoftzBf16x2,
      InstructionKind::AtomVectorMaxNoftzBf16x2,
      InstructionKind::AtomVectorAddF32};
  constexpr std::array red_vector_kinds = {
      InstructionKind::RedVectorAddNoftzF16,
      InstructionKind::RedVectorMinNoftzF16,
      InstructionKind::RedVectorMaxNoftzF16,
      InstructionKind::RedVectorAddNoftzBf16,
      InstructionKind::RedVectorMinNoftzBf16,
      InstructionKind::RedVectorMaxNoftzBf16,
      InstructionKind::RedVectorAddNoftzF16x2,
      InstructionKind::RedVectorMinNoftzF16x2,
      InstructionKind::RedVectorMaxNoftzF16x2,
      InstructionKind::RedVectorAddNoftzBf16x2,
      InstructionKind::RedVectorMinNoftzBf16x2,
      InstructionKind::RedVectorMaxNoftzBf16x2,
      InstructionKind::RedVectorAddF32};
  for (size_t index = 0; index < body.size(); ++index) {
    const auto kind = body[index]->instruction_kind();
    if (index < 77) {
      EXPECT_NE(std::ranges::find(atom_vector_kinds, kind), atom_vector_kinds.end());
      const auto qualifier = atomicQualifier<
          AtomVectorAddNoftzF16, AtomVectorMinNoftzF16,
          AtomVectorMaxNoftzF16, AtomVectorAddNoftzBf16,
          AtomVectorMinNoftzBf16, AtomVectorMaxNoftzBf16,
          AtomVectorAddNoftzF16x2, AtomVectorMinNoftzF16x2,
          AtomVectorMaxNoftzF16x2, AtomVectorAddNoftzBf16x2,
          AtomVectorMinNoftzBf16x2, AtomVectorMaxNoftzBf16x2,
          AtomVectorAddF32>(*body[index]);
      ASSERT_TRUE(qualifier.has_value());
      EXPECT_EQ(*qualifier, AtomicAddressQualifier::Global);
    } else {
      EXPECT_NE(std::ranges::find(red_vector_kinds, kind), red_vector_kinds.end());
      const auto qualifier = atomicQualifier<
          RedVectorAddNoftzF16, RedVectorMinNoftzF16,
          RedVectorMaxNoftzF16, RedVectorAddNoftzBf16,
          RedVectorMinNoftzBf16, RedVectorMaxNoftzBf16,
          RedVectorAddNoftzF16x2, RedVectorMinNoftzF16x2,
          RedVectorMaxNoftzF16x2, RedVectorAddNoftzBf16x2,
          RedVectorMinNoftzBf16x2, RedVectorMaxNoftzBf16x2,
          RedVectorAddF32>(*body[index]);
      ASSERT_TRUE(qualifier.has_value());
      EXPECT_EQ(*qualifier, AtomicAddressQualifier::Global);
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
  const auto& atom = dynamic_cast<const AtomVectorAddF32&>(**atom_resolved);
  for (const auto& lane : atom.dst.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  for (const auto& lane : atom.src.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  EXPECT_TRUE(atom.check(target).has_value());

  const auto red_ast = test_helpers::parseInstruction(
      "red.global.add.v2.f32 [%rd0], {%f2, %f3};");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(red_ast);
  const auto red_resolved = resolveInstruction(*red_ast);
  ASSERT_TRUE(red_resolved.has_value()) << red_resolved.error().message;
  const auto& red = dynamic_cast<const RedVectorAddF32&>(**red_resolved);
  for (const auto& lane : red.src.value.elements)
    ASSERT_FALSE(lane->declared_type.has_value());
  EXPECT_TRUE(red.check(target).has_value());
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
      [%a], %u, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.inc.u32
      [%a], %u, [%a];
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
  auto& shared_async = dynamic_cast<RedAsyncSharedIncU32&>(*body[0]);
  EXPECT_TRUE(shared_async.check(target).has_value());
  EXPECT_TRUE(
      body[1]->check(target).has_value());
  EXPECT_TRUE(
      body[2]->check(target).has_value());
  EXPECT_TRUE(
      body[3]->check(target).has_value());
  EXPECT_TRUE(
      body[4]->check(target).has_value());
  EXPECT_EQ(shared_async.address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  ASSERT_FALSE(shared_async.address_qualifier.locs.empty());
  for (auto invalid :
       {AtomicAddressQualifier::Shared, AtomicAddressQualifier::SharedCta,
        static_cast<AtomicAddressQualifier>(255)}) {
    shared_async.address_qualifier.value = invalid;
    const auto result = shared_async.check(target);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
    EXPECT_EQ(result.error().front().range,
              shared_async.address_qualifier.locs.front());
  }
  shared_async.address_qualifier.value = AtomicAddressQualifier::SharedCluster;
  EXPECT_TRUE(shared_async.check(target).has_value());
  auto& generic_async = dynamic_cast<RedAsyncSharedIncU32&>(*body[1]);
  generic_async.address_qualifier.value = AtomicAddressQualifier::Shared;
  EXPECT_FALSE(generic_async.check(target).has_value());
  generic_async.address_qualifier.value = AtomicAddressQualifier::Generic;
  EXPECT_TRUE(generic_async.check(target).has_value());
  auto& release = dynamic_cast<RedAsyncReleaseAddU32&>(*body[2]);
  release.address_qualifier.value = AtomicAddressQualifier::Shared;
  const auto release_check = release.check(target);
  ASSERT_FALSE(release_check.has_value());
  EXPECT_EQ(release_check.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
  auto& vector = dynamic_cast<AtomVectorAddF32&>(*body[3]);
  vector.address_qualifier.value = AtomicAddressQualifier::SharedCta;
  const auto vector_check = vector.check(target);
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
  auto& first = dynamic_cast<AtomVectorAddNoftzF16&>(*body[0]);
  EXPECT_EQ(first.vector.value, VectorArity::V2);
  EXPECT_FALSE(
      first.dst.value.elements[0].has_value());
  auto& hinted = dynamic_cast<AtomVectorMinNoftzF16x2&>(*body[1]);
  EXPECT_TRUE(hinted.cache_hint.value);
  EXPECT_EQ(hinted.vector.value, VectorArity::V4);
  ASSERT_TRUE(hinted.cache_policy.has_value());
  EXPECT_EQ(hinted.cache_policy->value.declared_type, ScalarType::B64);
  EXPECT_EQ(dynamic_cast<RedVectorAddF32&>(*body[2]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  const checker::Context supported{
      .target = {.ptx_version = {8, 1}, .sm_version = 90}};
  const checker::Context old_ptx{
      .target = {.ptx_version = {8, 0}, .sm_version = 90}};
  const checker::Context old_sm{
      .target = {.ptx_version = {8, 1}, .sm_version = 89}};
  for (auto& instruction : body) {
    const auto check = [&](const checker::Context& context) {
      return instruction->check(context);
    };
    EXPECT_TRUE(check(supported).has_value());
    EXPECT_FALSE(check(old_ptx).has_value());
    EXPECT_FALSE(check(old_sm).has_value());
  }
  hinted.vector.value = VectorArity::V8;
  EXPECT_FALSE(body[1]->check(supported)
                   .has_value());
  hinted.vector.value = VectorArity::V4;
  hinted.cache_hint.value = false;
  EXPECT_FALSE(body[1]->check(supported)
                   .has_value());
  hinted.cache_hint.value = true;
  hinted.cache_policy->value.declared_type =
      ScalarType::B32;
  EXPECT_FALSE(body[1]->check(supported)
                   .has_value());
  auto& integer_lanes = dynamic_cast<AtomVectorAddNoftzF16&>(*body[4]);
  integer_lanes.src.value.elements[0]
      ->declared_type = ScalarType::F16;
  EXPECT_FALSE(body[4]->check(supported)
                   .has_value());
}

/** Accept native half storage while keeping unrelated 16/32-bit types out. */
TEST(AtomicReductionCoverage, ChecksNativeHalfAtomicRegisters) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 4 .b32 value[4];
.entry kernel() {
  .reg .f16 %h<3>; .reg .b16 %b16<3>; .reg .f16x2 %p<3>;
  .reg .b32 %b32<3>; .reg .u16 %u16; .reg .u32 %u32;
  .reg .b64 %policy;
  atom.global.cas.b16 %h0, [value], %h1, %b160;
  atom.global.add.noftz.f16 %h0, [value], %h1;
  red.global.add.noftz.f16 [value], %h2;
  atom.global.add.noftz.f16x2 %p0, [value], %p1;
  red.global.add.noftz.f16x2 [value], %p2;
  atom.global.add.noftz.L2::cache_hint.f16 %h0, [value], %h1, %policy;
  red.global.add.noftz.L2::cache_hint.f16 [value], %h2, %policy;
  atom.global.add.noftz.L2::cache_hint.f16x2 %p0, [value], %p1, %policy;
  red.global.add.noftz.L2::cache_hint.f16x2 [value], %p2, %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto valid = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;

  for (std::string_view instruction : {
           "atom.global.add.noftz.f16 %u16, [value], %h1;",
           "red.global.add.noftz.f16 [value], %u16;",
           "atom.global.add.noftz.f16x2 %u32, [value], %p1;",
           "red.global.add.noftz.f16x2 [value], %u32;",
           "atom.global.add.noftz.bf16 %h0, [value], %h1;",
           "red.global.add.noftz.bf16x2 [value], %p1;",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_90\n"
        ".global .align 4 .b32 value[4];\n"
        ".entry kernel() { .reg .f16 %h<3>; .reg .f16x2 %p<3>; "
        ".reg .u16 %u16; .reg .u32 %u32;\n" +
        std::string(instruction) + "\n}\n";
    const auto candidate = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(candidate);
    EXPECT_FALSE(resolveAndValidateModule(*candidate).has_value())
        << instruction;
  }
}

/** Keep packed half lanes native or bit-typed, including hinted layouts. */
TEST(AtomicReductionCoverage, ChecksNativePackedHalfVectorLanes) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 16 .b8 value[16];
.entry kernel() {
  .reg .f16x2 %p<4>; .reg .b32 %b<4>; .reg .b64 %policy;
  atom.global.v2.f16x2.add.noftz {%p0, %p1}, [value], {%b0, %b1};
  red.global.v2.f16x2.add.noftz [value], {%p2, %p3};
  atom.global.add.noftz.L2::cache_hint.v2.f16x2
      {%b0, %b1}, [value], {%p0, %p1}, %policy;
  red.global.add.noftz.L2::cache_hint.v2.f16x2
      [value], {%p2, %p3}, %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto valid = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;

  for (std::string_view instruction : {
           "atom.global.v2.f16x2.add.noftz {%u0, %u1}, [value], {%p0, %p1};",
           "red.global.v2.f16x2.add.noftz [value], {%u0, %u1};",
           "atom.global.v2.bf16x2.add.noftz {%p0, %p1}, [value], {%b0, %b1};",
           "red.global.v2.bf16x2.add.noftz [value], {%p0, %p1};",
       }) {
    const std::string source =
        ".version 9.3\n.target sm_90\n"
        ".global .align 16 .b8 value[16];\n"
        ".entry kernel() { .reg .f16x2 %p<4>; .reg .u32 %u<2>; "
        ".reg .b32 %b<2>;\n" +
        std::string(instruction) + "\n}\n";
    const auto candidate = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(candidate);
    EXPECT_FALSE(resolveAndValidateModule(*candidate).has_value())
        << instruction;
  }
}

/** Reject repeated written lanes in both vector resolver paths and owned IR. */
TEST(AtomicReductionCoverage, ChecksDistinctDestinationVectorLanes) {
  for (std::string_view instruction : {
           "atom.global.v2.f32.add {%r0, %r0}, [%rd0], {%r1, %r1};",
           "ld.global.v2.u32 {%r0, %r0}, [%rd0];",
           "ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r0}, [%rd0];",
       }) {
    const auto parsed = test_helpers::parseInstruction(instruction);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value()) << instruction;
  }
  const auto standalone = test_helpers::parseInstruction(
      "atom.global.v4.f32.add {_, _, %r0, %r1}, [%rd0], "
      "{%r2, %r2, %r2, %r2};");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(standalone);
  const auto standalone_resolved = resolveInstruction(*standalone);
  ASSERT_TRUE(standalone_resolved.has_value())
      << standalone_resolved.error().message;
  const checker::Context target{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  EXPECT_TRUE((*standalone_resolved)->check(target).has_value());

  const auto bound = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 8 .b8 value[8];
.entry kernel() {
  .reg .f32 %f<4>;
  atom.global.v2.f32.add {%f0, %f1}, [value], {%f2, %f2};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(bound);
  auto owned = resolveAndValidateModule(*bound);
  ASSERT_TRUE(owned.has_value()) << owned.error().front().message;
  auto& atom = dynamic_cast<AtomVectorAddF32&>(
      *owned->functions.front().body.front());
  auto& destination = atom.dst.value.elements;
  destination[1] = destination[0];
  const auto invalid_owned = atom.check(target);
  ASSERT_FALSE(invalid_owned.has_value());
  EXPECT_EQ(invalid_owned.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  ASSERT_TRUE(destination[0]->symbol_id.has_value());
  destination[1]->symbol_id.reset();
  const auto missing_identity = atom.check(target);
  ASSERT_FALSE(missing_identity.has_value());
  EXPECT_EQ(missing_identity.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  destination[1]->symbol_id = destination[0]->symbol_id;
  destination[1]->parameterized_index =
      destination[0]->parameterized_index.value_or(0) + 1;
  const auto changed_index = atom.check(target);
  ASSERT_FALSE(changed_index.has_value());
  EXPECT_EQ(changed_index.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  const auto duplicate_bound = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.global .align 8 .b8 value[8];
.entry kernel() {
  .reg .f32 %f<4>;
  atom.global.v2.f32.add {%f0, %f0}, [value], {%f2, %f2};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(duplicate_bound);
  EXPECT_FALSE(resolveAndValidateModule(*duplicate_bound).has_value());

  const auto modern_bound = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.shared .align 16 .b16 shared_value[16];
.entry kernel() {
  .reg .b32 %r<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(modern_bound);
  auto modern_owned = resolveAndValidateModule(*modern_bound);
  ASSERT_TRUE(modern_owned.has_value()) << modern_owned.error().front().message;
  auto& matrix = dynamic_cast<LdmatrixSyncAlignedM8n8X2SharedB16&>(
      *modern_owned->functions.front().body.front());
  auto& modern_destination = matrix.dst.value.elements;
  modern_destination[1] = modern_destination[0];
  const auto invalid_matrix = matrix.check(target);
  ASSERT_FALSE(invalid_matrix.has_value());
  EXPECT_EQ(invalid_matrix.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
  EXPECT_FALSE(validateModule(*modern_owned,
                              ModuleValidationPolicy::RequireCompleteContext)
                   .has_value());
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
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.inc.u32 [%a], %u, [%a];
  red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes.dec.u32 [%a+4], %u, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.u32 [%a], %u, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.s32 [%a], %s, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.u32 [%a], %u, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.s32 [%a], %s, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.and.b32 [%a], %b, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.or.b32 [%a], %b, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.xor.b32 [%a], %b, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 [%a], 1, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.s32 [%a], %s, [%a];
  red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u64 [%a+8], %ud, [%a];
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
  EXPECT_EQ(dynamic_cast<RedAsyncSharedIncU32&>(*body[0]).address_qualifier.value,
            AtomicAddressQualifier::Generic);
  EXPECT_EQ(dynamic_cast<RedAsyncSharedDecU32&>(*body[1]).address_qualifier.value,
            AtomicAddressQualifier::SharedCluster);
  EXPECT_EQ(dynamic_cast<RedAsyncReleaseAddS32&>(*body[13]).address_qualifier.value,
            AtomicAddressQualifier::Global);
  EXPECT_TRUE(dynamic_cast<RedAsyncReleaseAddU64&>(*body[14])
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
    if (index < 12) {
      EXPECT_TRUE(body[index]->check(shared_target).has_value()) << index;
      EXPECT_FALSE(body[index]->check(old_shared_ptx).has_value()) << index;
      EXPECT_FALSE(body[index]->check(old_shared_sm).has_value()) << index;
    } else {
      EXPECT_TRUE(body[index]->check(release_target).has_value()) << index;
      EXPECT_FALSE(body[index]->check(old_release_ptx).has_value()) << index;
      EXPECT_FALSE(body[index]->check(old_release_sm).has_value()) << index;
      EXPECT_FALSE(body[index]->check(shared_target).has_value()) << index;
    }
  }
  auto& mmio = dynamic_cast<RedAsyncReleaseAddU64&>(*body[14]);
  mmio.scope.value = MemoryScope::Gpu;
  EXPECT_FALSE(
      body[14]->check(release_target)
          .has_value());
  mmio.scope.value = MemoryScope::Sys;
  dynamic_cast<RedAsyncSharedDecU32&>(*body[1]).address_qualifier.value =
      AtomicAddressQualifier::Global;
  EXPECT_FALSE(body[1]->check(release_target)
                   .has_value());
  auto& shared = dynamic_cast<RedAsyncSharedIncU32&>(*body[0]);
  shared.address.value.base = ResolvedSymbolRef{
      .spelling = "barrier",
      .address_state_space = base::DeclarationStateSpace::Shared,
  };
  EXPECT_FALSE(body[0]->check(shared_target)
                   .has_value());
  auto& mbarrier = dynamic_cast<RedAsyncSharedMinU32&>(*body[2])
                       .mbarrier.value;
  mbarrier.base = ResolvedSymbolRef{
      .spelling = "barrier",
      .address_state_space = base::DeclarationStateSpace::Shared,
  };
  EXPECT_FALSE(body[2]->check(shared_target)
                   .has_value());
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
           "[%a], %u, [barrier];",
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
      [%a+2147483647], %u, [%a-2147483648];
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
  auto& shared = dynamic_cast<RedAsyncSharedAddU32&>(*body[0]);
  ASSERT_TRUE(shared.address.value.offset.has_value());
  ASSERT_TRUE(shared.mbarrier.value.offset.has_value());
  shared.address.value.offset->value.bits = 2147483648ULL;
  const auto invalid_destination =
      body[0]->check(supported);
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
      body[0]->check(supported);
  ASSERT_FALSE(invalid_mbarrier.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      invalid_mbarrier.error(), [](const checker::CheckDiagnostic& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation;
      }));
  shared.mbarrier.value.offset->value.bits = 2147483648ULL;
  shared.mbarrier.value.offset->value.integer_source_bits = 2147483648ULL;
  auto& release = dynamic_cast<RedAsyncReleaseAddU32&>(*body[2]);
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
           "[%a+2147483648], %u, [%a];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a-2147483649], %u, [%a];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [%a+2147483648];",
           "red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 "
           "[%a], %u, [%a-2147483649];",
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
