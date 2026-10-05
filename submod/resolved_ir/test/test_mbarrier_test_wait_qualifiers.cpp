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
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Retain all paired wait shapes and their qualifier locations after AST release. */
TEST(MbarrierTestWaitQualifiers, OwnsPairedShapesAndSourceMetadata) {
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
  .reg .u32 %phase;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.test_wait.acquire.cta.b64 %p0, [%rd0], %state;
  mbarrier.test_wait.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %state;
  mbarrier.test_wait.parity.acquire.cluster.shared.b64 %p0, [shared_value], 1;
  mbarrier.test_wait.parity.relaxed.cta.shared::cta.b64 %p0, [shared_value], %phase;
  mbarrier.test_wait.phase_type::primary.acquire.cta.b64 %p0|%p1, %b0, [%rd0], %state;
  mbarrier.test_wait.phase_type::primary.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %state;
  mbarrier.test_wait.parity.phase_type::primary.acquire.cluster.b64 %p0|%p1, %b0, [%rd0], 1;
  mbarrier.test_wait.parity.phase_type::primary.relaxed.cta.shared::cta.b64 %p0, [shared_value], 0;
  mbarrier.test_wait.parity.phase_type::conditional.acquire.cta.b64 %p0, [%rd0], 1;
  mbarrier.test_wait.parity.phase_type::conditional.relaxed.cluster.shared::cta.b64 %p0, [shared_value], %phase;
  mbarrier.test_wait.b64 %p0, [%rd0], %state;
  mbarrier.test_wait.parity.b64 %p0, [shared_value], 0;
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
      InstructionKind::MbarrierTestWaitTokenSemanticsGenericOrShared,
      InstructionKind::MbarrierTestWaitTokenSemanticsSharedCta,
      InstructionKind::MbarrierTestWaitParitySemanticsGenericOrShared,
      InstructionKind::MbarrierTestWaitParitySemanticsSharedCta,
      InstructionKind::MbarrierTestWaitTokenPrimarySemanticsGenericOrShared,
      InstructionKind::MbarrierTestWaitTokenPrimarySemanticsSharedCta,
      InstructionKind::MbarrierTestWaitParityPrimarySemanticsGenericOrShared,
      InstructionKind::MbarrierTestWaitParityPrimarySemanticsSharedCta,
      InstructionKind::MbarrierTestWaitParityConditionalSemanticsGenericOrShared,
      InstructionKind::MbarrierTestWaitParityConditionalSemanticsSharedCta,
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
  const auto& first_qualifiers = dynamic_cast<const
      MbarrierTestWaitTokenSemanticsGenericOrShared&>(*function.body[0]);
  const auto& second_qualifiers = dynamic_cast<const
      MbarrierTestWaitTokenSemanticsSharedCta&>(*function.body[1]);
  EXPECT_EQ(first_qualifiers.semantics.value, MemoryConsistency::Acquire);
  EXPECT_EQ(first_qualifiers.scope.value, MemoryScope::Cta);
  EXPECT_FALSE(first_qualifiers.semantics.locs.empty());
  EXPECT_FALSE(first_qualifiers.scope.locs.empty());
  EXPECT_EQ(second_qualifiers.semantics.value, MemoryConsistency::Relaxed);
  EXPECT_EQ(second_qualifiers.scope.value, MemoryScope::Cluster);
  EXPECT_FALSE(second_qualifiers.semantics.locs.empty());
  EXPECT_FALSE(second_qualifiers.scope.locs.empty());
  EXPECT_EQ(function.body[10]->instruction_kind(),
            InstructionKind::MbarrierTestWaitTokenGenericOrShared);
  EXPECT_EQ(function.body[11]->instruction_kind(),
            InstructionKind::MbarrierTestWaitParityGenericOrShared);

  const auto& report = dynamic_cast<const
      MbarrierTestWaitTokenPrimarySemanticsGenericOrShared&>(*function.body[4]);
  EXPECT_EQ(report.operand_layout.value, 2U);
  const auto& conditional = dynamic_cast<const
      MbarrierTestWaitParityConditionalSemanticsGenericOrShared&>(
          *function.body[8]);
  EXPECT_EQ(conditional.phase_parity.value.index(), 1U);

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
  EXPECT_TRUE(check_at(0, 8, 0, 80, false));
  EXPECT_FALSE(check_at(0, 7, 9, 90, true));
  EXPECT_FALSE(check_at(0, 8, 0, 79, false));
  EXPECT_TRUE(check_at(2, 8, 0, 90, true));
  EXPECT_FALSE(check_at(2, 8, 0, 90, false));
  EXPECT_FALSE(check_at(2, 8, 0, 80, true));
  EXPECT_TRUE(check_at(1, 8, 6, 90, true));
  EXPECT_FALSE(check_at(1, 8, 5, 90, true));
  EXPECT_FALSE(check_at(1, 8, 6, 80, true));
  EXPECT_TRUE(check_at(4, 9, 3, 90, false));
  EXPECT_FALSE(check_at(4, 9, 2, 90, true));
  EXPECT_FALSE(check_at(4, 9, 3, 89, true));

  auto& first = dynamic_cast<MbarrierTestWaitTokenSemanticsGenericOrShared&>(
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
TEST(MbarrierTestWaitQualifiers, RejectsBadParityAndAddress) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value[2];
.entry k() {
  .reg .pred %p0;
  .reg .b64 %state;
  mbarrier.test_wait.parity.acquire.cta.shared.b64 %p0, [shared_value], 2;
  mbarrier.test_wait.acquire.cta.shared.b64 %p0, [shared_value+4], %state;
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
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
