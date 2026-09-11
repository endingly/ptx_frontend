#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov::Scalar& mov) {
  return std::get<Mov::Scalar::ScalarOperands>(mov.operands);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov& mov) {
  return scalarMovOperands(std::get<Mov::Scalar>(mov.variant));
}

const Mov::Scalar::PackOperands& packMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

Mov::Scalar::PackOperands& packMovOperands(Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

const Mov::Scalar::UnpackOperands& unpackMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::UnpackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierInitLayouts) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mbarrier.init.b64 [%rd0], %r0;
  mbarrier.init.shared.b64 [shared_value], 1;
  mbarrier.init.shared::cta.b64 [shared_value+8], 511;
  mbarrier.init.layout::v1.b64 [%rd0], 1;
  mbarrier.init.layout::v1.shared.b64 [shared_value], 511;
  mbarrier.init.layout::v1.shared::cta.b64 [shared_value], 1;
  mbarrier.init.layout::v0.b64 [shared_value], 1048575;
  mbarrier.init.b64 [shared_value], 512;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  const auto& generic_v0 =
      std::get<Mbarrier::InitGenericV0>(std::get<Mbarrier>(body[0]).variant);
  const auto& generic_v1 =
      std::get<Mbarrier::InitGenericV1>(std::get<Mbarrier>(body[3]).variant);
  const auto& explicit_v0 =
      std::get<Mbarrier::InitGenericV0>(std::get<Mbarrier>(body[6]).variant);
  EXPECT_TRUE(generic_v0.init);
  EXPECT_EQ(generic_v0.layout.value, MbarrierLayout::V0);
  EXPECT_EQ(generic_v1.layout.value, MbarrierLayout::V1);
  EXPECT_EQ(generic_v0.type, ScalarType::B64);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      generic_v0.count.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      explicit_v0.count.value));

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context baseline{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  const auto baseline_generic = checker::check(std::get<Mbarrier>(body[0]), baseline);
  ASSERT_TRUE(baseline_generic.has_value()) << baseline_generic.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Mbarrier>(body[1]), baseline).has_value());
  EXPECT_FALSE(checker::check(std::get<Mbarrier>(body[2]), baseline).has_value());
  EXPECT_FALSE(checker::check(std::get<Mbarrier>(body[3]), baseline).has_value());
  EXPECT_FALSE(checker::check(std::get<Mbarrier>(body[6]), baseline).has_value());
  const checker::Context shared_cta_target{
      .target = {.ptx_version = {7, 8}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(
      checker::check(std::get<Mbarrier>(body[2]), shared_cta_target).has_value());
  const checker::Context old_layout_target{
      .target = {.ptx_version = {9, 2}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  EXPECT_FALSE(
      checker::check(std::get<Mbarrier>(body[3]), old_layout_target).has_value());
  const auto old_explicit_v0 =
      checker::check(std::get<Mbarrier>(body[6]), old_layout_target);
  ASSERT_FALSE(old_explicit_v0.has_value());
  ASSERT_EQ(old_explicit_v0.error().size(), 1u);
  EXPECT_EQ(old_explicit_v0.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context narrow_layout_target{
      .target = {.ptx_version = {9, 3}, .sm_version = 89},
      .instruction_range = ast.range,
  };
  EXPECT_FALSE(
      checker::check(std::get<Mbarrier>(body[3]), narrow_layout_target).has_value());
  const auto narrow_explicit_v0 =
      checker::check(std::get<Mbarrier>(body[6]), narrow_layout_target);
  ASSERT_FALSE(narrow_explicit_v0.has_value());
  ASSERT_EQ(narrow_explicit_v0.error().size(), 1u);
  EXPECT_EQ(narrow_explicit_v0.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const checker::Context old_sm_target{
      .target = {.ptx_version = {7, 0}, .sm_version = 79},
      .instruction_range = ast.range,
  };
  const auto old_sm_generic =
      checker::check(std::get<Mbarrier>(body[0]), old_sm_target);
  ASSERT_FALSE(old_sm_generic.has_value());
  ASSERT_EQ(old_sm_generic.error().size(), 1u);
  EXPECT_EQ(old_sm_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto check_range = [&supported](std::string_view source) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto invalid = resolveModule(*parsed_module_2);
    ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
    const auto checked = checker::check(
        std::get<Mbarrier>(invalid->functions.front().body.front()), supported);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  };
  check_range(R"ptx(
.entry kernel() { .reg .u64 %rd0; mbarrier.init.b64 [%rd0], 0; }
)ptx");
  check_range(R"ptx(
.entry kernel() { .reg .u64 %rd0; mbarrier.init.b64 [%rd0], 1048576; }
)ptx");
  check_range(R"ptx(
.entry kernel() { .reg .u64 %rd0; mbarrier.init.layout::v1.b64 [%rd0], 0; }
)ptx");
  check_range(R"ptx(
.entry kernel() { .reg .u64 %rd0; mbarrier.init.layout::v1.b64 [%rd0], 512; }
)ptx");

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u64 %count;
  mbarrier.init.b64 [%rd0], %count;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_width = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto wrong_width_checked = checker::check(
      std::get<Mbarrier>(wrong_width->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_width_checked.has_value());
  EXPECT_EQ(wrong_width_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() {
  mbarrier.init.b64 [global_value], 1;
  mbarrier.init.shared.b64 [global_value+8], 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_space = resolveModule(*parsed_module_4);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto parsed_module_5 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() {
  mbarrier.init.b64 [unaligned_value], 1;
  mbarrier.init.shared.b64 [aligned_value+4], 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto unaligned = resolveModule(*parsed_module_5);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierInvalSpaces) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u64 %rd0;
  mbarrier.inval.b64 [%rd0];
  mbarrier.inval.shared.b64 [shared_value];
  mbarrier.inval.shared::cta.b64 [shared_value+8];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& generic =
      std::get<Mbarrier::InvalGeneric>(std::get<Mbarrier>(body[0]).variant);
  const auto& shared =
      std::get<Mbarrier::InvalShared>(std::get<Mbarrier>(body[1]).variant);
  const auto& shared_cta =
      std::get<Mbarrier::InvalSharedCta>(std::get<Mbarrier>(body[2]).variant);
  EXPECT_TRUE(generic.inval);
  EXPECT_TRUE(shared.shared);
  EXPECT_TRUE(shared_cta.shared_cta);
  EXPECT_EQ(generic.type, ScalarType::B64);

  const checker::Context supported{
      .target = {.ptx_version = {7, 8}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context base{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Mbarrier>(body[0]), base).has_value());
  EXPECT_TRUE(checker::check(std::get<Mbarrier>(body[1]), base).has_value());
  const auto old_cta = checker::check(std::get<Mbarrier>(body[2]), base);
  ASSERT_FALSE(old_cta.has_value());
  EXPECT_EQ(old_cta.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context old_ptx{
      .target = {.ptx_version = {6, 9}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  const auto old_ptx_generic = checker::check(std::get<Mbarrier>(body[0]), old_ptx);
  ASSERT_FALSE(old_ptx_generic.has_value());
  EXPECT_EQ(old_ptx_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context old_sm{
      .target = {.ptx_version = {7, 0}, .sm_version = 79},
      .instruction_range = ast.range,
  };
  const auto old_sm_generic = checker::check(std::get<Mbarrier>(body[0]), old_sm);
  ASSERT_FALSE(old_sm_generic.has_value());
  EXPECT_EQ(old_sm_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const checker::Context old_cta_ptx{
      .target = {.ptx_version = {7, 7}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  const auto old_cta_version =
      checker::check(std::get<Mbarrier>(body[2]), old_cta_ptx);
  ASSERT_FALSE(old_cta_version.has_value());
  EXPECT_EQ(old_cta_version.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() {
  mbarrier.inval.b64 [global_value];
  mbarrier.inval.shared.b64 [global_value+8];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() {
  mbarrier.inval.b64 [unaligned_value];
  mbarrier.inval.shared.b64 [aligned_value+4];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierExpectTxSemanticsAndSpaces) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mbarrier.expect_tx.b64 [%rd0], %r0;
  mbarrier.expect_tx.shared.b64 [shared_value], 1;
  mbarrier.expect_tx.shared::cta.b64 [shared_value+8], 2;
  mbarrier.expect_tx.shared::cluster.b64 [shared_value], 3;
  mbarrier.expect_tx.relaxed.cta.shared.b64 [shared_value], 4;
  mbarrier.expect_tx.relaxed.cluster.shared::cluster.b64 [shared_value+8], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& generic = std::get<Mbarrier::ExpectTxGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& shared = std::get<Mbarrier::ExpectTxGenericOrShared>(
      std::get<Mbarrier>(body[1]).variant);
  const auto& shared_cta = std::get<Mbarrier::ExpectTxSharedCta>(
      std::get<Mbarrier>(body[2]).variant);
  const auto& shared_cluster = std::get<Mbarrier::ExpectTxSharedCluster>(
      std::get<Mbarrier>(body[3]).variant);
  const auto& relaxed_cta = std::get<Mbarrier::ExpectTxRelaxedCtaGenericOrShared>(
      std::get<Mbarrier>(body[4]).variant);
  const auto& relaxed_cluster = std::get<Mbarrier::ExpectTxRelaxedClusterSharedCluster>(
      std::get<Mbarrier>(body[5]).variant);
  EXPECT_EQ(generic.state_space.value, MemoryStateSpace::Generic);
  EXPECT_TRUE(generic.state_space.locs.empty());
  EXPECT_EQ(shared.state_space.value, MemoryStateSpace::Shared);
  EXPECT_FALSE(shared.state_space.locs.empty());
  EXPECT_TRUE(shared_cta.shared_cta);
  EXPECT_TRUE(shared_cluster.shared_cluster);
  EXPECT_EQ(relaxed_cta.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(relaxed_cta.scope, MemoryScope::Cta);
  EXPECT_EQ(relaxed_cluster.scope, MemoryScope::Cluster);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(generic.tx_count.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(shared.tx_count.value));

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {8, 0},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context old_ptx{
      .target = {.ptx_version = {7, 9}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  const auto old_ptx_generic = checker::check(std::get<Mbarrier>(body[0]), old_ptx);
  ASSERT_FALSE(old_ptx_generic.has_value());
  EXPECT_EQ(old_ptx_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context old_sm{
      .target = {.ptx_version = {8, 0}, .sm_version = 89},
      .instruction_range = ast.range,
  };
  const auto old_sm_generic = checker::check(std::get<Mbarrier>(body[0]), old_sm);
  ASSERT_FALSE(old_sm_generic.has_value());
  EXPECT_EQ(old_sm_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0, %count;
  mbarrier.expect_tx.b64 [%rd0], %count;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_count = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_count.has_value()) << wrong_count.error().front().message;
  const auto wrong_count_checked = checker::check(
      std::get<Mbarrier>(wrong_count->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_count_checked.has_value());
  EXPECT_EQ(wrong_count_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() {
  mbarrier.expect_tx.b64 [global_value], 1;
  mbarrier.expect_tx.shared.b64 [global_value+8], 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_space = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() {
  mbarrier.expect_tx.b64 [unaligned_value], 1;
  mbarrier.expect_tx.shared.b64 [aligned_value+4], 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto unaligned = resolveModule(*parsed_module_4);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }

  const auto parsed_module_5 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value;
.entry kernel() { mbarrier.expect_tx.shared.b64 [shared_value], 4294967296; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto out_of_range_tx_count = resolveModule(*parsed_module_5);
  ASSERT_FALSE(out_of_range_tx_count.has_value());
  EXPECT_FALSE(out_of_range_tx_count.error().front().checker_kind.has_value());
  EXPECT_EQ(out_of_range_tx_count.error().front().message,
            "Integer literal '4294967296' is out of range for scalar type 'U32'.");
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierCompleteTxSemanticsAndSpaces) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mbarrier.complete_tx.b64 [%rd0], %r0;
  mbarrier.complete_tx.shared.b64 [shared_value], 1;
  mbarrier.complete_tx.shared::cta.b64 [shared_value+8], 2;
  mbarrier.complete_tx.shared::cluster.b64 [shared_value], 3;
  mbarrier.complete_tx.relaxed.cta.shared.b64 [shared_value], 4;
  mbarrier.complete_tx.relaxed.cluster.shared::cluster.b64 [shared_value+8], %r0;
}

)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& generic = std::get<Mbarrier::CompleteTxGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& shared = std::get<Mbarrier::CompleteTxGenericOrShared>(
      std::get<Mbarrier>(body[1]).variant);
  const auto& shared_cta = std::get<Mbarrier::CompleteTxSharedCta>(
      std::get<Mbarrier>(body[2]).variant);
  const auto& shared_cluster = std::get<Mbarrier::CompleteTxSharedCluster>(
      std::get<Mbarrier>(body[3]).variant);
  const auto& relaxed_cta = std::get<Mbarrier::CompleteTxRelaxedCtaGenericOrShared>(
      std::get<Mbarrier>(body[4]).variant);
  const auto& relaxed_cluster = std::get<Mbarrier::CompleteTxRelaxedClusterSharedCluster>(
      std::get<Mbarrier>(body[5]).variant);
  EXPECT_EQ(generic.state_space.value, MemoryStateSpace::Generic);
  EXPECT_TRUE(generic.state_space.locs.empty());
  EXPECT_EQ(shared.state_space.value, MemoryStateSpace::Shared);
  EXPECT_FALSE(shared.state_space.locs.empty());
  EXPECT_TRUE(shared_cta.shared_cta);
  EXPECT_TRUE(shared_cluster.shared_cluster);
  EXPECT_EQ(relaxed_cta.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(relaxed_cta.scope, MemoryScope::Cta);
  EXPECT_EQ(relaxed_cluster.scope, MemoryScope::Cluster);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(generic.tx_count.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(shared.tx_count.value));

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {8, 0},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context old_ptx{
      .target = {.ptx_version = {7, 9}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  const auto old_ptx_generic = checker::check(std::get<Mbarrier>(body[0]), old_ptx);
  ASSERT_FALSE(old_ptx_generic.has_value());
  EXPECT_EQ(old_ptx_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context old_sm{
      .target = {.ptx_version = {8, 0}, .sm_version = 89},
      .instruction_range = ast.range,
  };
  const auto old_sm_generic = checker::check(std::get<Mbarrier>(body[0]), old_sm);
  ASSERT_FALSE(old_sm_generic.has_value());
  EXPECT_EQ(old_sm_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0, %count;
  mbarrier.complete_tx.b64 [%rd0], %count;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_count = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_count.has_value()) << wrong_count.error().front().message;
  const auto wrong_count_checked = checker::check(
      std::get<Mbarrier>(wrong_count->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_count_checked.has_value());
  EXPECT_EQ(wrong_count_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() {
  mbarrier.complete_tx.b64 [global_value], 1;
  mbarrier.complete_tx.shared.b64 [global_value+8], 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_space = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() {
  mbarrier.complete_tx.b64 [unaligned_value], 1;
  mbarrier.complete_tx.shared.b64 [aligned_value+4], 2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto unaligned = resolveModule(*parsed_module_4);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierArriveForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u32 %count;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.arrive.b64 %state, [%rd0];
  mbarrier.arrive.b64 _, [shared_value];
  mbarrier.arrive.b64 %state, [shared_value], %count;
  mbarrier.arrive.shared::cta.b64 %state, [shared_value+8];
  mbarrier.arrive.release.cta.b64 %state, [shared_value], %count;
  mbarrier.arrive.shared::cluster.b64 _, [shared_value], 1;
  mbarrier.arrive.expect_tx.release.cluster.shared.b64 _, [shared_value], 1;
  mbarrier.arrive.noComplete.release.cta.shared::cta.b64 _, [shared_value], 1;
  mbarrier.arrive.relaxed.cluster.shared::cluster.b64 _, [shared_value], 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 9u);
  const auto& register_result = std::get<Mbarrier::ArriveGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& sink_result = std::get<Mbarrier::ArriveGenericOrShared>(
      std::get<Mbarrier>(body[1]).variant);
  const auto& cluster_result = std::get<Mbarrier::ArriveSharedCluster>(
      std::get<Mbarrier>(body[5]).variant);
  const auto& register_operands = std::get<Mbarrier::ArriveGenericOrShared::NoCountOperands>(
      register_result.operands);
  const auto& sink_operands = std::get<Mbarrier::ArriveGenericOrShared::NoCountOperands>(
      sink_result.operands);
  const auto& cluster_operands = std::get<Mbarrier::ArriveSharedCluster::WithCountOperands>(
      cluster_result.operands);
  EXPECT_TRUE(register_operands.state.value.register_ref.has_value());
  EXPECT_FALSE(sink_operands.state.value.register_ref.has_value());
  EXPECT_FALSE(cluster_operands.state.value.register_ref.has_value());

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {8, 6},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context sink_too_old{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  const auto old_sink = checker::check(std::get<Mbarrier>(body[1]), sink_too_old);
  ASSERT_FALSE(old_sink.has_value());
  EXPECT_EQ(old_sink.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context count_sm_too_old{
      .target = {.ptx_version = {8, 0}, .sm_version = 89},
      .instruction_range = ast.range,
  };
  const auto old_count = checker::check(std::get<Mbarrier>(body[2]), count_sm_too_old);
  ASSERT_FALSE(old_count.has_value());
  EXPECT_EQ(old_count.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %state; .reg .u64 %rd0;
  mbarrier.arrive.shared::cluster.b64 %state, [%rd0]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto cluster_register = resolveModule(*parsed_module_2);
  ASSERT_FALSE(cluster_register.has_value());
  EXPECT_NE(cluster_register.error().front().message.find("requires the '_' sink"),
            std::string::npos);

  for (const std::string_view source : {
           ".shared .align 8 .b64 shared_value; .entry kernel() { mbarrier.arrive.expect_tx.release.cta.shared.b64 _, [shared_value], 4294967296; }",
           ".shared .align 8 .b64 shared_value; .entry kernel() { mbarrier.arrive.expect_tx.release.cluster.shared.b64 _, [shared_value], 4294967296; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    const auto out_of_range = resolveModule(*parsed_module_3);
    ASSERT_FALSE(out_of_range.has_value());
    EXPECT_FALSE(out_of_range.error().front().checker_kind.has_value());
    EXPECT_EQ(out_of_range.error().front().message,
              "Integer literal '4294967296' is out of range for scalar type 'U32'.");
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierArriveDropForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u32 %count;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.arrive_drop.b64 %state, [%rd0];
  mbarrier.arrive_drop.b64 _, [shared_value];
  mbarrier.arrive_drop.b64 %state, [shared_value], %count;
  mbarrier.arrive_drop.shared::cta.b64 %state, [shared_value+8];
  mbarrier.arrive_drop.release.cta.b64 %state, [shared_value], %count;
  mbarrier.arrive_drop.shared::cluster.b64 _, [shared_value], 1;
  mbarrier.arrive_drop.expect_tx.release.cluster.shared.b64 _, [shared_value], 1;
  mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 _, [shared_value], 1;
  mbarrier.arrive_drop.relaxed.cluster.shared::cluster.b64 _, [shared_value], 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 9u);
  const auto& register_result = std::get<Mbarrier::ArriveDropGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& sink_result = std::get<Mbarrier::ArriveDropGenericOrShared>(
      std::get<Mbarrier>(body[1]).variant);
  const auto& cluster_result = std::get<Mbarrier::ArriveDropSharedCluster>(
      std::get<Mbarrier>(body[5]).variant);
  const auto& register_operands = std::get<Mbarrier::ArriveDropGenericOrShared::NoCountOperands>(
      register_result.operands);
  const auto& sink_operands = std::get<Mbarrier::ArriveDropGenericOrShared::NoCountOperands>(
      sink_result.operands);
  const auto& cluster_operands = std::get<Mbarrier::ArriveDropSharedCluster::WithCountOperands>(
      cluster_result.operands);
  EXPECT_TRUE(register_operands.state.value.register_ref.has_value());
  EXPECT_FALSE(sink_operands.state.value.register_ref.has_value());
  EXPECT_FALSE(cluster_operands.state.value.register_ref.has_value());

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {8, 6},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  const checker::Context sink_too_old{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  const auto old_sink = checker::check(std::get<Mbarrier>(body[1]), sink_too_old);
  ASSERT_FALSE(old_sink.has_value());
  EXPECT_EQ(old_sink.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const checker::Context count_sm_too_old{
      .target = {.ptx_version = {8, 0}, .sm_version = 89},
      .instruction_range = ast.range,
  };
  const auto old_count = checker::check(std::get<Mbarrier>(body[2]), count_sm_too_old);
  ASSERT_FALSE(old_count.has_value());
  EXPECT_EQ(old_count.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const checker::Context relaxed_too_old{
      .target = {.ptx_version = {8, 5},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  const auto old_relaxed = checker::check(std::get<Mbarrier>(body[8]), relaxed_too_old);
  ASSERT_FALSE(old_relaxed.has_value());
  EXPECT_EQ(old_relaxed.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %state; .reg .u64 %rd0;
  mbarrier.arrive_drop.shared::cluster.b64 %state, [%rd0]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto cluster_register = resolveModule(*parsed_module_2);
  ASSERT_FALSE(cluster_register.has_value());
  EXPECT_NE(cluster_register.error().front().message.find("requires the '_' sink"),
            std::string::npos);
}

TEST(ResolvedModule, ChecksMbarrierArrivalCountRangesInEveryCountForm) {
  constexpr std::array forms{
      "mbarrier.arrive.b64 _, [barrier], $count;",
      "mbarrier.arrive.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive.shared::cluster.b64 _, [barrier], $count;",
      "mbarrier.arrive.release.cta.b64 _, [barrier], $count;",
      "mbarrier.arrive.release.cta.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive.release.cluster.shared::cluster.b64 _, [barrier], $count;",
      "mbarrier.arrive.noComplete.b64 _, [barrier], $count;",
      "mbarrier.arrive.noComplete.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive.noComplete.release.cta.b64 _, [barrier], $count;",
      "mbarrier.arrive.noComplete.release.cta.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.shared::cluster.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.release.cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.release.cta.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.release.cluster.shared::cluster.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.noComplete.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.noComplete.shared::cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.noComplete.release.cta.b64 _, [barrier], $count;",
      "mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 _, [barrier], $count;",
  };
  constexpr std::array counts{"0", "1", "1048575", "%count", "1048576"};
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  for (const auto form : forms) {
    for (const auto count : counts) {
      SCOPED_TRACE(form);
      SCOPED_TRACE(count);
      auto instruction = std::string(form);
      instruction.replace(instruction.find("$count"), 6, count);
      const auto parsed_module_1 = parseModule(
          ".shared .align 8 .b64 barrier;\n.entry kernel() { .reg .u32 %count; " +
          instruction + " }");
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
      const auto& ast = *parsed_module_1;
      const auto resolved = resolveModule(ast);
      ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
      const auto& syntax_instruction = std::get<syntax_ast::AstInstruction>(
          std::get<syntax_ast::AstFunction>(ast.items.back()).body.back());
      const auto checked = checker::check(
          std::get<Mbarrier>(resolved->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {9, 3},
                                      .sm_version = 90,
                                      .capabilities = cluster_capabilities},
                           .instruction_range = syntax_instruction.range});
      if (count == "1048576") {
        ASSERT_FALSE(checked.has_value());
        ASSERT_FALSE(checked.error().empty());
        EXPECT_EQ(checked.error().front().kind,
                  checker::CheckDiagnosticKind::ImmediateValueMismatch);
        EXPECT_EQ(checked.error().front().range,
                  std::get<syntax_ast::AstImmediate>(
                      syntax_instruction.operands.back())
                      .syntax.range);
      } else {
        EXPECT_TRUE(checked.has_value());
      }
    }
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierTestWaitBasicForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .pred %p0;
  .reg .u32 %phase;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.test_wait.b64 %p0, [%rd0], %state;
  mbarrier.test_wait.shared.b64 %p0, [shared_value], %state;
  mbarrier.test_wait.shared::cta.b64 %p0, [shared_value+8], %state;
  mbarrier.test_wait.parity.b64 %p0, [shared_value], 0;
  mbarrier.test_wait.parity.shared::cta.b64 %p0, [shared_value], %phase;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto& token = std::get<Mbarrier::TestWaitTokenGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& parity = std::get<Mbarrier::TestWaitParityGenericOrShared>(
      std::get<Mbarrier>(body[3]).variant);
  EXPECT_TRUE(token.state.value.register_ref.has_value());
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(parity.phase_parity.value));

  const checker::Context supported{
      .target = {.ptx_version = {7, 8}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Mbarrier>(instruction), supported).has_value());
  const auto old_ptx = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_parity = checker::check(
      std::get<Mbarrier>(body[3]),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_parity.has_value());
  EXPECT_EQ(old_parity.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_cta = checker::check(
      std::get<Mbarrier>(body[2]),
      checker::Context{.target = {.ptx_version = {7, 7}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_cta.has_value());
  EXPECT_EQ(old_cta.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() { .reg .pred %p0; .reg .b64 %state;
  mbarrier.test_wait.b64 %p0, [global_value], %state; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  const auto wrong_space_checked = checker::check(
      std::get<Mbarrier>(wrong_space->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_space_checked.has_value());
  EXPECT_EQ(wrong_space_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() { .reg .pred %p0; .reg .b64 %state;
  mbarrier.test_wait.b64 %p0, [unaligned_value], %state;
  mbarrier.test_wait.parity.b64 %p0, [aligned_value+4], 1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %rd0;
  mbarrier.test_wait.b64 %p0, [%rd0], _; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto invalid_token = resolveModule(*parsed_module_4);
  ASSERT_FALSE(invalid_token.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %rd0;
  mbarrier.test_wait.b64 %p0, [%rd0], 1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto immediate_token = resolveModule(*parsed_module_5);
  ASSERT_FALSE(immediate_token.has_value());

  const auto parsed_module_6 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p0;
  mbarrier.test_wait.parity.b64 %p0, [shared_value], 2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto invalid_parity = resolveModule(*parsed_module_6);
  ASSERT_TRUE(invalid_parity.has_value()) << invalid_parity.error().front().message;
  EXPECT_FALSE(checker::check(
      std::get<Mbarrier>(invalid_parity->functions.front().body.front()), supported).has_value());

  const auto parsed_module_7 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p0; .reg .b32 %state; .reg .u64 %phase;
  mbarrier.test_wait.b64 %p0, [shared_value], %state;
  mbarrier.test_wait.parity.b64 %p0, [shared_value], %phase; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto wrong_width = resolveModule(*parsed_module_7);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  for (const auto& instruction : wrong_width->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierTryWaitBasicForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .pred %p0;
  .reg .u32 %phase, %hint;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.try_wait.b64 %p0, [%rd0], %state;
  mbarrier.try_wait.shared::cta.b64 %p0, [shared_value], %state, 1;
  mbarrier.try_wait.parity.b64 %p0, [shared_value], 0;
  mbarrier.try_wait.parity.shared::cta.b64 %p0, [shared_value+8], %phase, %hint;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto& token = std::get<Mbarrier::TryWaitTokenGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& parity = std::get<Mbarrier::TryWaitParitySharedCta>(
      std::get<Mbarrier>(body[3]).variant);
  const auto& token_operands = std::get<Mbarrier::TryWaitTokenGenericOrShared::NoHintOperands>(
      token.operands);
  const auto& parity_operands = std::get<Mbarrier::TryWaitParitySharedCta::WithHintOperands>(
      parity.operands);
  EXPECT_TRUE(token_operands.state.value.register_ref.has_value());
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(parity_operands.time_hint.value));

  const checker::Context supported{
      .target = {.ptx_version = {7, 8}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Mbarrier>(instruction), supported).has_value());
  const auto old_ptx = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {7, 7}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {7, 8}, .sm_version = 89},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() { .reg .pred %p0; .reg .b64 %state;
  mbarrier.try_wait.b64 %p0, [global_value], %state; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  const auto wrong_space_checked = checker::check(
      std::get<Mbarrier>(wrong_space->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_space_checked.has_value());
  EXPECT_EQ(wrong_space_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() { .reg .pred %p0; .reg .b64 %state;
  mbarrier.try_wait.b64 %p0, [unaligned_value], %state;
  mbarrier.try_wait.parity.b64 %p0, [aligned_value+4], 1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %rd0;
  mbarrier.try_wait.b64 %p0, [%rd0], _; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto invalid_token = resolveModule(*parsed_module_4);
  ASSERT_FALSE(invalid_token.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p0;
  mbarrier.try_wait.parity.b64 %p0, [shared_value], 2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto invalid_parity = resolveModule(*parsed_module_5);
  ASSERT_TRUE(invalid_parity.has_value()) << invalid_parity.error().front().message;
  EXPECT_FALSE(checker::check(
      std::get<Mbarrier>(invalid_parity->functions.front().body.front()), supported).has_value());

  const auto parsed_module_6 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p0; .reg .b32 %state; .reg .u64 %hint;
  mbarrier.try_wait.b64 %p0, [shared_value], %state;
  mbarrier.try_wait.parity.b64 %p0, [shared_value], 1, %hint; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto wrong_width = resolveModule(*parsed_module_6);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  for (const auto& instruction : wrong_width->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
  const auto parsed_module_7 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value;
.entry kernel() { .reg .pred %p0; .reg .b64 %state;
  mbarrier.try_wait.shared::cta.b64 %p0, [shared_value], %state, 4294967296; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto out_of_range_time_hint = resolveModule(*parsed_module_7);
  ASSERT_FALSE(out_of_range_time_hint.has_value());
  EXPECT_FALSE(out_of_range_time_hint.error().front().checker_kind.has_value());
  EXPECT_EQ(out_of_range_time_hint.error().front().message,
            "Integer literal '4294967296' is out of range for scalar type 'U32'.");
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierWaitPhaseAndReportForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .pred %p<3>;
  .reg .b8 %b0;
  .reg .u32 %phase, %hint;
  .reg .u64 %rd0;
  .reg .b64 %state;
  mbarrier.test_wait.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], %state;
  mbarrier.test_wait.parity.phase_type::primary.shared::cta.b64 %p0|%p1, %b0, [shared_value], 1;
  mbarrier.test_wait.parity.phase_type::conditional.b64 %p0, [%rd0], %phase;
  mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], %state, %hint;
  mbarrier.try_wait.parity.phase_type::primary.shared::cta.b64 %p0|%p1, %b0, [shared_value+8], 0, 1;
  mbarrier.try_wait.parity.phase_type::conditional.b64 %p0, [%rd0], 1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& token = std::get<Mbarrier::TestWaitTokenPrimaryGenericOrShared>(
      std::get<Mbarrier>(body[0]).variant);
  const auto& token_operands = std::get<
      Mbarrier::TestWaitTokenPrimaryGenericOrShared::ReportPredicateValueOperands>(
      token.operands);
  EXPECT_EQ(token_operands.wait_complete_report_predicate.value.first.register_ref.spelling,
            "%p0");
  EXPECT_EQ(token_operands.wait_complete_report_predicate.value.second.register_ref.spelling,
            "%p1");

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Mbarrier>(instruction), supported).has_value());
  for (const auto checked : {
           checker::check(std::get<Mbarrier>(body[0]),
                          checker::Context{.target = {.ptx_version = {9, 2}, .sm_version = 90},
                                           .instruction_range = ast.range}),
           checker::check(std::get<Mbarrier>(body[0]),
                          checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 89},
                                           .instruction_range = ast.range}),
       })
    ASSERT_FALSE(checked.has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p0;
  mbarrier.test_wait.parity.phase_type::primary.b64 %p0, [shared_value], 2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bad_parity = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bad_parity.has_value()) << bad_parity.error().front().message;
  EXPECT_FALSE(checker::check(
      std::get<Mbarrier>(bad_parity->functions.front().body.front()), supported).has_value());

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() { .reg .pred %p<2>; .reg .b32 %r0; .reg .b64 %state;
  mbarrier.test_wait.phase_type::primary.b64 %p0|%p1, %r0, [shared_value], %state; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_report_value = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_report_value.has_value())
      << wrong_report_value.error().front().message;
  const auto wrong_report_checked = checker::check(
      std::get<Mbarrier>(wrong_report_value->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_report_checked.has_value());
  EXPECT_EQ(wrong_report_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierPendingCount) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  .reg .b64 %state;
  mbarrier.pending_count.b64 %r0, %state;
  mbarrier.pending_count.layout::v0.b64 %r0, %state;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& pending = std::get<Mbarrier::PendingCount>(
      std::get<Mbarrier>(body[0]).variant);
  EXPECT_EQ(pending.layout.value, MbarrierLayout::V0);
  EXPECT_TRUE(pending.state.value.register_ref.has_value());

  const checker::Context baseline{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Mbarrier>(body[0]), baseline).has_value());
  const auto explicit_old_ptx = checker::check(
      std::get<Mbarrier>(body[1]),
      checker::Context{.target = {.ptx_version = {9, 2}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(explicit_old_ptx.has_value());
  EXPECT_EQ(explicit_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto explicit_old_sm = checker::check(
      std::get<Mbarrier>(body[1]),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 89},
                       .instruction_range = ast.range});
  ASSERT_FALSE(explicit_old_sm.has_value());
  EXPECT_EQ(explicit_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %r0; .reg .u64 %rd0; .reg .b32 %state32; .reg .b64 %state64;
  mbarrier.pending_count.b64 %rd0, %state64;
  mbarrier.pending_count.b64 %r0, %state32; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  for (const auto& instruction : wrong_width->functions.front().body) {
    const auto checked = checker::check(std::get<Mbarrier>(instruction), baseline);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksMbarrierCheckLayout) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .pred %p0;
  .reg .u64 %rd0;
  mbarrier.check_layout.layout::v0.b64 %p0, [%rd0];
  mbarrier.check_layout.layout::v1.b64 %p0, [shared_value];
  mbarrier.check_layout.layout::v0.shared::cta.b64 %p0, [shared_value];
  mbarrier.check_layout.layout::v1.shared::cta.b64 %p0, [shared_value+8];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto& generic_v0 = std::get<Mbarrier::CheckLayoutGenericV0>(
      std::get<Mbarrier>(body[0]).variant);
  EXPECT_EQ(generic_v0.layout, MbarrierLayout::V0);
  EXPECT_EQ(generic_v0.result.value.register_ref.spelling, "%p0");

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Mbarrier>(instruction), supported).has_value());
  const auto old_ptx = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {9, 2}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      std::get<Mbarrier>(body[0]),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 89},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() { .reg .pred %p0;
  mbarrier.check_layout.layout::v0.b64 %p0, [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  const auto wrong_space_checked = checker::check(
      std::get<Mbarrier>(wrong_space->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_space_checked.has_value());
  EXPECT_EQ(wrong_space_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.entry kernel() { .reg .pred %p0;
  mbarrier.check_layout.layout::v1.b64 %p0, [unaligned_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  const auto unaligned_checked = checker::check(
      std::get<Mbarrier>(unaligned->functions.front().body.front()), supported);
  ASSERT_FALSE(unaligned_checked.has_value());
  EXPECT_EQ(unaligned_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMapaClusterAddressSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 4 .u32 shared_value;
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<5>;
  .reg .u64 %rd<3>;
  mapa.shared::cluster.u32 %r0, %r1, 0;
  mapa.shared::cluster.u32 %r0, shared_value, %r2;
  mapa.shared::cluster.u32 %r0, shared_value+4, 0;
  mapa.shared::cluster.u64 %rd0, %rd1, %r2;
  mapa.u32 %r3, %r4, 0;
  mapa.u64 %rd0, %rd1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& shared_register =
      std::get<Mapa::SharedCluster>(std::get<Mapa>(body[0]).variant);
  const auto& shared_symbol =
      std::get<Mapa::SharedCluster>(std::get<Mapa>(body[1]).variant);
  const auto& shared_address =
      std::get<Mapa::SharedCluster>(std::get<Mapa>(body[2]).variant);
  const auto& generic = std::get<Mapa::Generic>(std::get<Mapa>(body[4]).variant);
  EXPECT_TRUE(shared_register.shared_cluster);
  EXPECT_EQ(shared_register.type.value, ScalarType::U32);
  EXPECT_EQ(shared_register.dst.value.declared_type, ScalarType::U32);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(shared_register.src.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedSymbolRef>(shared_symbol.src.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedAddress>(shared_address.src.value));
  EXPECT_EQ(generic.type.value, ScalarType::U32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(shared_register.rank.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(shared_symbol.rank.value));

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {7, 8},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(checker::check(std::get<Mapa>(instruction), supported).has_value());
  }
  for (const checker::Context unavailable : {
           checker::Context{.target = {.ptx_version = {7, 7},
                                      .sm_version = 90,
                                      .capabilities = cluster_capabilities},
                            .instruction_range = ast.range},
           checker::Context{.target = {.ptx_version = {7, 8},
                                      .sm_version = 89,
                                      .capabilities = cluster_capabilities},
                            .instruction_range = ast.range},
           checker::Context{.target = {.ptx_version = {7, 8}, .sm_version = 90},
                            .instruction_range = ast.range},
       }) {
    EXPECT_FALSE(
        checker::check(std::get<Mapa>(body.front()), unavailable).has_value());
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  mapa.shared::cluster.u32 %r0, global_value, 0;
  mapa.shared::cluster.u32 %r0, global_value+4, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked = checker::check(std::get<Mapa>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  for (const std::string_view source : {
           ".entry kernel() { .reg .u32 %r<2>; mapa.shared::cluster.u32 %r0, 0, 0; }",
           ".entry kernel() { .reg .u32 %r<2>; mapa.shared::cluster.u32 %r0, %tid.x, 0; }",
           ".global .u32 value; .entry kernel() { .reg .u32 %r<2>; mapa.u32 %r0, value, 0; }",
           ".entry kernel() { .reg .u32 %r<2>; mapa.u32 %r0, %r1+4, 0; }",
           ".func device() {} .entry kernel() { .reg .u32 %r<2>; mapa.shared::cluster.u32 %r0, device, 0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    EXPECT_FALSE(resolveModule(*parsed_module_3).has_value());
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  .reg .u64 %rd<3>;
  mapa.u32 %rd0, %r0, %rd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto bad_widths = resolveModule(*parsed_module_4);
  ASSERT_TRUE(bad_widths.has_value()) << bad_widths.error().front().message;
  const auto bad_check = checker::check(
      std::get<Mapa>(bad_widths->functions.front().body.front()), supported);
  ASSERT_FALSE(bad_check.has_value());
  EXPECT_EQ(bad_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  for (const std::string_view source : {
           ".entry kernel() { .reg .u32 %r<2>; mapa.u32 %r0, %r1, 4294967296; }",
           ".entry kernel() { .reg .u32 %r<2>; mapa.shared::cluster.u32 %r0, %r1, 4294967296; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_5 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    const auto out_of_range = resolveModule(*parsed_module_5);
    ASSERT_FALSE(out_of_range.has_value());
    EXPECT_FALSE(out_of_range.error().front().checker_kind.has_value());
    EXPECT_EQ(out_of_range.error().front().message,
              "Integer literal '4294967296' is out of range for scalar type 'U32'.");
  }
}

TEST(ResolvedModule, ResolvesAndChecksGetctarankClusterAddressSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 4 .u32 shared_value;
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<4>;
  .reg .b32 %b0;
  .reg .s32 %s0;
  .reg .u64 %rd<2>;
  getctarank.shared::cluster.u32 %r0, %r1;
  getctarank.shared::cluster.u32 %b0, shared_value;
  getctarank.shared::cluster.u32 %s0, shared_value+4;
  getctarank.shared::cluster.u64 %r0, %rd0;
  getctarank.u32 %r2, %r3;
  getctarank.u64 %r2, %rd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& shared_register =
      std::get<Getctarank::SharedCluster>(std::get<Getctarank>(body[0]).variant);
  const auto& shared_symbol =
      std::get<Getctarank::SharedCluster>(std::get<Getctarank>(body[1]).variant);
  const auto& shared_address =
      std::get<Getctarank::SharedCluster>(std::get<Getctarank>(body[2]).variant);
  const auto& generic =
      std::get<Getctarank::Generic>(std::get<Getctarank>(body[4]).variant);
  EXPECT_TRUE(shared_register.shared_cluster);
  EXPECT_EQ(shared_register.type.value, ScalarType::U32);
  EXPECT_EQ(shared_register.dst.value.declared_type, ScalarType::U32);
  EXPECT_EQ(shared_symbol.dst.value.declared_type, ScalarType::B32);
  EXPECT_EQ(shared_address.dst.value.declared_type, ScalarType::S32);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(shared_register.src.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedSymbolRef>(shared_symbol.src.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedAddress>(shared_address.src.value));
  EXPECT_EQ(generic.type.value, ScalarType::U32);

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {7, 8},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(
        checker::check(std::get<Getctarank>(instruction), supported).has_value());
  }
  for (const checker::Context unavailable : {
           checker::Context{.target = {.ptx_version = {7, 7},
                                      .sm_version = 90,
                                      .capabilities = cluster_capabilities},
                            .instruction_range = ast.range},
           checker::Context{.target = {.ptx_version = {7, 8},
                                      .sm_version = 89,
                                      .capabilities = cluster_capabilities},
                            .instruction_range = ast.range},
           checker::Context{.target = {.ptx_version = {7, 8}, .sm_version = 90},
                            .instruction_range = ast.range},
       }) {
    EXPECT_FALSE(checker::check(std::get<Getctarank>(body.front()), unavailable)
                     .has_value());
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  getctarank.shared::cluster.u32 %r0, global_value;
  getctarank.shared::cluster.u32 %r0, global_value+4;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  for (const auto& instruction : wrong_space->functions.front().body) {
    const auto checked =
        checker::check(std::get<Getctarank>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  for (const std::string_view source : {
           ".entry kernel() { .reg .u32 %r<2>; getctarank.shared::cluster.u32 %r0, 0; }",
           ".entry kernel() { .reg .u32 %r<2>; getctarank.shared::cluster.u32 %r0, %tid.x; }",
           ".global .u32 value; .entry kernel() { .reg .u32 %r<2>; getctarank.u32 %r0, value; }",
           ".entry kernel() { .reg .u32 %r<2>; getctarank.u32 %r0, %r1+4; }",
           ".func device() {} .entry kernel() { .reg .u32 %r<2>; getctarank.shared::cluster.u32 %r0, device; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    EXPECT_FALSE(resolveModule(*parsed_module_3).has_value());
  }

  for (const std::string_view source : {
           ".entry kernel() { .reg .u32 %r<2>; .reg .u64 %rd0; getctarank.u32 %rd0, %r0; }",
           ".entry kernel() { .reg .u32 %r<2>; getctarank.u64 %r0, %r1; }",
       }) {
    const auto parsed_module_4 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
    const auto bad_width = resolveModule(*parsed_module_4);
    ASSERT_TRUE(bad_width.has_value()) << bad_width.error().front().message;
    const auto checked = checker::check(
        std::get<Getctarank>(bad_width->functions.front().body.front()), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksElectSyncSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  .reg .b32 %b0;
  .reg .s32 %s0;
  .reg .u32 %mask;
  .reg .pred %p<4>;
  elect.sync %u0|%p0, 0xffffffff;
  elect.sync %b0|%p1, %mask;
  elect.sync %s0|%p2, 0xffffffff;
  elect.sync _|%p3, %mask;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto& u32 = std::get<Elect::Sync>(std::get<Elect>(body[0]).variant);
  const auto& b32 = std::get<Elect::Sync>(std::get<Elect>(body[1]).variant);
  const auto& s32 = std::get<Elect::Sync>(std::get<Elect>(body[2]).variant);
  const auto& sink = std::get<Elect::Sync>(std::get<Elect>(body[3]).variant);
  ASSERT_TRUE(u32.result.value.data.has_value());
  ASSERT_TRUE(u32.result.value.predicate.has_value());
  EXPECT_EQ(u32.result.value.data->value.declared_type, ScalarType::U32);
  EXPECT_EQ(u32.result.value.predicate->value.register_ref.declared_type,
            ScalarType::Pred);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(u32.membermask.value));
  ASSERT_TRUE(b32.result.value.data.has_value());
  EXPECT_EQ(b32.result.value.data->value.declared_type, ScalarType::B32);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(b32.membermask.value));
  ASSERT_TRUE(s32.result.value.data.has_value());
  EXPECT_EQ(s32.result.value.data->value.declared_type, ScalarType::S32);
  EXPECT_FALSE(sink.result.value.data.has_value());

  const checker::Context context{
      .target = {.ptx_version = {8, 0}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(checker::check(std::get<Elect>(instruction), context).has_value());
  }
  const auto too_old_ptx = checker::check(
      std::get<Elect>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 9}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Elect>(body.front()),
      checker::Context{.target = {.ptx_version = {8, 0}, .sm_version = 89},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %d0; .reg .pred %p0; elect.sync %d0|%p0, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bad_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bad_width.has_value()) << bad_width.error().front().message;
  const auto bad_check = checker::check(
      std::get<Elect>(bad_width->functions.front().body.front()), context);
  ASSERT_FALSE(bad_check.has_value());
  EXPECT_EQ(bad_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u0; .reg .pred %p0; .reg .b64 %d0; elect.sync %u0|%p0, %d0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bad_mask = resolveModule(*parsed_module_3);
  ASSERT_TRUE(bad_mask.has_value()) << bad_mask.error().front().message;
  const auto bad_mask_check = checker::check(
      std::get<Elect>(bad_mask->functions.front().body.front()), context);
  ASSERT_FALSE(bad_mask_check.has_value());
  EXPECT_EQ(bad_mask_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  for (const std::string_view source : {
           ".entry kernel() { .reg .u32 %u0; elect.sync %u0|%u0, 0; }",
           ".entry kernel() { .reg .u32 %u0; .reg .pred %p0; elect.sync %u0, 0; }",
           ".entry kernel() { .reg .u32 %u0; .reg .pred %p0; elect.sync %u0|%p0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_4 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
    EXPECT_FALSE(resolveModule(*parsed_module_4).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksShflSyncIdxB32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<3>;
  .reg .pred %p<2>;
  .reg .u32 %u<4>;
  shfl.sync.idx.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
  shfl.sync.idx.b32 %b1|%p1, %b2, %u0, %u1, %u2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& immediate =
      std::get<Shfl::SyncIdxB32>(std::get<Shfl>(body[0]).variant);
  const auto& register_operands =
      std::get<Shfl::SyncIdxB32>(std::get<Shfl>(body[1]).variant);
  EXPECT_TRUE(immediate.sync);
  EXPECT_TRUE(immediate.idx);
  EXPECT_EQ(immediate.type, ScalarType::B32);
  ASSERT_TRUE(immediate.dst.value.data.has_value());
  ASSERT_TRUE(immediate.dst.value.predicate.has_value());
  EXPECT_EQ(immediate.dst.value.data->value.declared_type, ScalarType::B32);
  EXPECT_EQ(immediate.dst.value.predicate->value.register_ref.declared_type,
            ScalarType::Pred);
  EXPECT_FALSE(immediate.dst.locs.empty());
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(immediate.lane.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(register_operands.lane.value));
  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 30},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Shfl>(body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Shfl>(body[1]), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Shfl>(body[0]),
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 30},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Shfl>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 29},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  .reg .b64 %wide0;
  shfl.sync.idx.b32 %b0|%p0, %wide0, 0, 31, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bad_data_and_lane = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bad_data_and_lane.has_value())
      << bad_data_and_lane.error().front().message;
  const auto bad_check = checker::check(
      std::get<Shfl>(bad_data_and_lane->functions.front().body.front()), context);
  ASSERT_FALSE(bad_check.has_value());
  EXPECT_EQ(bad_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .u32 %u0;
  shfl.sync.idx.b32 %b0|%u0, %b0, 0, 31, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bad_pair = resolveModule(*parsed_module_3);
  ASSERT_FALSE(bad_pair.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  shfl.sync.idx.b32 %b0|_, %b0, 0, 31, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto& bad_predicate_ast = *parsed_module_4;
  const auto bad_predicate = resolveModule(bad_predicate_ast);
  ASSERT_FALSE(bad_predicate.has_value());
  const auto& bad_predicate_instruction = std::get<syntax_ast::AstInstruction>(
      std::get<syntax_ast::AstFunction>(bad_predicate_ast.items.back()).body.back());
  EXPECT_EQ(
      bad_predicate.error().front().range,
      std::get<syntax_ast::AstRegisterPredicatePair>(
          bad_predicate_instruction.operands.front())
          .predicate.syntax.range);
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.sync.up.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto wrong_mode = resolveModule(*parsed_module_5);
  ASSERT_FALSE(wrong_mode.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.idx.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto legacy = resolveModule(*parsed_module_6);
  ASSERT_FALSE(legacy.has_value());
  const auto parsed_module_7 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.sync.idx.b32 %b0|%p0, %b1, 0, 31;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto missing_membermask = resolveModule(*parsed_module_7);
  ASSERT_FALSE(missing_membermask.has_value());
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir
