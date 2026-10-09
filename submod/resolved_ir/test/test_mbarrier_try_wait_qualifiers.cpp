#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/mbarrier.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Retain paired try-wait shapes, hints, and qualifiers after AST release. */
TEST(MbarrierTryWaitQualifiers, OwnsPairedShapesAndSourceMetadata) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value[2];
.entry k() {
  .reg .pred %p<3>;
  .reg .b8 %b0;
  .reg .u32 %phase, %hint;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.try_wait.acquire.cta.b64 %p0, [%rd0], %state;
  mbarrier.try_wait.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %state, 12;
  mbarrier.try_wait.parity.acquire.cluster.shared.b64 %p0, [shared_value], 1;
  mbarrier.try_wait.parity.relaxed.cta.shared::cta.b64 %p0, [shared_value], %phase, %hint;
  mbarrier.try_wait.phase_type::primary.acquire.cta.b64 %p0|%p1, %b0, [%rd0], %state, 20;
  mbarrier.try_wait.phase_type::primary.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %state;
  mbarrier.try_wait.parity.phase_type::primary.acquire.cluster.b64 %p0|%p1, %b0, [%rd0], 1, %hint;
  mbarrier.try_wait.parity.phase_type::primary.relaxed.cta.shared::cta.b64 %p0, [shared_value], 0;
  mbarrier.try_wait.parity.phase_type::conditional.acquire.cta.b64 %p0, [%rd0], 1, 8;
  mbarrier.try_wait.parity.phase_type::conditional.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %phase;
  mbarrier.try_wait.b64 %p0, [%rd0], %state;
  mbarrier.try_wait.parity.b64 %p0, [shared_value], 0;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }

  ASSERT_TRUE(owned.has_value());
  auto& function = owned->functions.front();
  ASSERT_EQ(function.body.size(), 12U);
  const std::array<InstructionKind, 10> expected{
      InstructionKind::MbarrierTryWaitTokenSemanticsGenericOrShared,
      InstructionKind::MbarrierTryWaitTokenSemanticsSharedCta,
      InstructionKind::MbarrierTryWaitParitySemanticsGenericOrShared,
      InstructionKind::MbarrierTryWaitParitySemanticsSharedCta,
      InstructionKind::MbarrierTryWaitTokenPrimarySemanticsGenericOrShared,
      InstructionKind::MbarrierTryWaitTokenPrimarySemanticsSharedCta,
      InstructionKind::MbarrierTryWaitParityPrimarySemanticsGenericOrShared,
      InstructionKind::MbarrierTryWaitParityPrimarySemanticsSharedCta,
      InstructionKind::MbarrierTryWaitParityConditionalSemanticsGenericOrShared,
      InstructionKind::MbarrierTryWaitParityConditionalSemanticsSharedCta,
  };
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
  };
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto& instruction = *function.body[index];
    EXPECT_EQ(instruction.instruction_kind(), expected[index]);
    EXPECT_TRUE(instruction.check(supported).has_value()) << index;
  }
  const auto& first_qualifiers =
      dynamic_cast<const MbarrierTryWaitTokenSemanticsGenericOrShared&>(
          *function.body[0]);
  const auto& second_qualifiers =
      dynamic_cast<const MbarrierTryWaitTokenSemanticsSharedCta&>(
          *function.body[1]);
  EXPECT_EQ(first_qualifiers.semantics.value, MemoryConsistency::Acquire);
  EXPECT_EQ(first_qualifiers.scope.value, MemoryScope::Cta);
  EXPECT_FALSE(first_qualifiers.semantics.locs.empty());
  EXPECT_FALSE(first_qualifiers.scope.locs.empty());
  EXPECT_EQ(second_qualifiers.semantics.value, MemoryConsistency::Relaxed);
  EXPECT_EQ(second_qualifiers.scope.value, MemoryScope::Cluster);
  EXPECT_FALSE(second_qualifiers.semantics.locs.empty());
  EXPECT_FALSE(second_qualifiers.scope.locs.empty());
  EXPECT_EQ(second_qualifiers.operand_layout.value, 1U);
  ASSERT_TRUE(second_qualifiers.time_hint.has_value());
  EXPECT_EQ(
      std::get<ResolvedImmediate>(second_qualifiers.time_hint->value).bits,
      12U);
  EXPECT_FALSE(second_qualifiers.time_hint->locs.empty());
  EXPECT_EQ(function.body[10]->instruction_kind(),
            InstructionKind::MbarrierTryWaitTokenGenericOrShared);
  EXPECT_EQ(function.body[11]->instruction_kind(),
            InstructionKind::MbarrierTryWaitParityGenericOrShared);

  const auto& report =
      dynamic_cast<const MbarrierTryWaitTokenPrimarySemanticsGenericOrShared&>(
          *function.body[4]);
  EXPECT_EQ(report.operand_layout.value, 5U);
  ASSERT_TRUE(report.time_hint.has_value());
  EXPECT_EQ(std::get<ResolvedImmediate>(report.time_hint->value).bits, 20U);
  const auto& conditional = dynamic_cast<
      const MbarrierTryWaitParityConditionalSemanticsGenericOrShared&>(
      *function.body[8]);
  EXPECT_EQ(conditional.operand_layout.value, 1U);
  EXPECT_EQ(conditional.phase_parity.value.index(), 1U);
  ASSERT_TRUE(conditional.time_hint.has_value());
  EXPECT_EQ(std::get<ResolvedImmediate>(conditional.time_hint->value).bits, 8U);

  /** Recheck one public instruction against an alternate target context. */
  const auto check_at = [&](std::size_t index, std::uint16_t major,
                            std::uint16_t minor, std::uint32_t sm,
                            bool cluster) {
    const checker::Context context{
        .target = {.ptx_version = {major, minor},
                   .sm_version = sm,
                   .capabilities = cluster
                                       ? std::span(cluster_capabilities)
                                       : std::span<const std::string_view>{}},
    };
    return function.body[index]->check(context).has_value();
  };
  EXPECT_TRUE(check_at(0, 8, 0, 90, false));
  EXPECT_FALSE(check_at(0, 7, 9, 90, true));
  EXPECT_FALSE(check_at(0, 8, 0, 89, false));
  EXPECT_TRUE(check_at(2, 8, 0, 90, true));
  EXPECT_FALSE(check_at(2, 8, 0, 90, false));
  EXPECT_FALSE(check_at(2, 8, 0, 80, true));
  EXPECT_TRUE(check_at(1, 8, 6, 90, true));
  EXPECT_FALSE(check_at(1, 8, 5, 90, true));
  EXPECT_FALSE(check_at(1, 8, 6, 80, true));
  EXPECT_TRUE(check_at(4, 9, 3, 90, false));
  EXPECT_FALSE(check_at(4, 9, 2, 90, true));
  EXPECT_FALSE(check_at(4, 9, 3, 89, true));

  auto& first = dynamic_cast<MbarrierTryWaitTokenSemanticsGenericOrShared&>(
      *function.body.front());
  first.semantics.value = MemoryConsistency::Release;
  const auto bad_semantics = function.body.front()->check(supported);
  ASSERT_FALSE(bad_semantics.has_value());
  EXPECT_EQ(bad_semantics.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
  first.semantics.value = MemoryConsistency::Acquire;
  first.scope.value = MemoryScope::Gpu;
  const auto bad_scope = function.body.front()->check(supported);
  ASSERT_FALSE(bad_scope.has_value());
  EXPECT_EQ(bad_scope.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
  first.scope.value = MemoryScope::Cta;
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto bad_layout = function.body.front()->check(supported);
  ASSERT_FALSE(bad_layout.has_value());
  EXPECT_EQ(bad_layout.error().front().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
}

/** Apply inherited parity and address checks to explicitly qualified waits. */
TEST(MbarrierTryWaitQualifiers, RejectsBadParityAndAddress) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value[2];
.entry k() {
  .reg .pred %p0;
  .reg .b64 %state;
  mbarrier.try_wait.parity.acquire.cta.shared.b64 %p0, [shared_value], 2, 8;
  mbarrier.try_wait.acquire.cta.shared.b64 %p0, [shared_value+4], %state, 8;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2U);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
  };
  const auto bad_parity = body[0]->check(supported);
  ASSERT_FALSE(bad_parity.has_value());
  EXPECT_EQ(bad_parity.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  const auto bad_address = body[1]->check(supported);
  ASSERT_FALSE(bad_address.has_value());
  EXPECT_EQ(bad_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto oversized_hint = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value;
.entry k() {
  .reg .pred %p0;
  .reg .b64 %state;
  mbarrier.try_wait.acquire.cta.shared.b64 %p0, [shared_value], %state, 4294967296;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(oversized_hint);
  EXPECT_FALSE(resolveModuleOnly(*oversized_hint).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
