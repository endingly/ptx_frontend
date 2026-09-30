#include <gtest/gtest.h>
#include "test_instruction_access.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/mbarrier.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(SelectVariantMbarrier, SelectsBasicTestWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.test_wait.b64 %p0, [%rd0], %state;",
                 Mbarrier::VariantType::TestWaitTokenGenericOrShared);
  expect_variant("mbarrier.test_wait.shared.b64 %p0, [shared_value], %state;",
                 Mbarrier::VariantType::TestWaitTokenGenericOrShared);
  expect_variant(
      "mbarrier.test_wait.shared::cta.b64 %p0, [shared_value], %state;",
      Mbarrier::VariantType::TestWaitTokenSharedCta);
  expect_variant("mbarrier.test_wait.parity.b64 %p0, [%rd0], 1;",
                 Mbarrier::VariantType::TestWaitParityGenericOrShared);
  expect_variant(
      "mbarrier.test_wait.parity.shared::cta.b64 %p0, [shared_value], %r0;",
      Mbarrier::VariantType::TestWaitParitySharedCta);

  for (const std::string_view source : {
           "mbarrier.test_wait.shared::cluster.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.b32 %p0, [%rd0], %state;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
                   parse_instruction("mbarrier.test_wait.b64 %p0, [%rd0];"))
                   .has_value());
  EXPECT_FALSE(
      resolve<Mbarrier>(
          parse_instruction("mbarrier.test_wait.b64 %p0, [%rd0], %state, 1;"))
          .has_value());
}

TEST(SelectVariantMbarrier, SelectsBasicTryWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.try_wait.b64 %p0, [%rd0], %state;",
                 Mbarrier::VariantType::TryWaitTokenGenericOrShared);
  expect_variant(
      "mbarrier.try_wait.shared::cta.b64 %p0, [shared_value], %state, 1;",
      Mbarrier::VariantType::TryWaitTokenSharedCta);
  expect_variant("mbarrier.try_wait.parity.b64 %p0, [%rd0], 1;",
                 Mbarrier::VariantType::TryWaitParityGenericOrShared);
  expect_variant(
      "mbarrier.try_wait.parity.shared::cta.b64 %p0, [shared_value], %r0, %r1;",
      Mbarrier::VariantType::TryWaitParitySharedCta);

  EXPECT_FALSE(
      selectVariant<Mbarrier>(
          parse_instruction("mbarrier.try_wait.b32 %p0, [%rd0], %state;"))
          .has_value());
  EXPECT_FALSE(
      resolve<Mbarrier>(parse_instruction("mbarrier.try_wait.b64 %p0, [%rd0];"))
          .has_value());
  EXPECT_FALSE(
      resolve<Mbarrier>(
          parse_instruction("mbarrier.try_wait.b64 %p0, [%rd0], %state, 1, 2;"))
          .has_value());
}

TEST(SelectVariantMbarrier, SelectsPhaseAndReportWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant(
      "mbarrier.test_wait.phase_type::primary.b64 %p0, [%rd0], %state;",
      Mbarrier::VariantType::TestWaitTokenPrimaryGenericOrShared);
  expect_variant(
      "mbarrier.test_wait.phase_type::primary.shared::cta.b64 %p0|%p1, %b0, "
      "[shared_value], %state;",
      Mbarrier::VariantType::TestWaitTokenPrimarySharedCta);
  expect_variant(
      "mbarrier.test_wait.parity.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], "
      "1;",
      Mbarrier::VariantType::TestWaitParityPrimaryGenericOrShared);
  expect_variant(
      "mbarrier.test_wait.parity.phase_type::primary.shared::cta.b64 %p0, "
      "[shared_value], 1;",
      Mbarrier::VariantType::TestWaitParityPrimarySharedCta);
  expect_variant(
      "mbarrier.test_wait.parity.phase_type::conditional.b64 %p0, [%rd0], 1;",
      Mbarrier::VariantType::TestWaitParityConditionalGenericOrShared);
  expect_variant(
      "mbarrier.test_wait.parity.phase_type::conditional.shared::cta.b64 %p0, "
      "[shared_value], 1;",
      Mbarrier::VariantType::TestWaitParityConditionalSharedCta);
  expect_variant(
      "mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], %state, "
      "1;",
      Mbarrier::VariantType::TryWaitTokenPrimaryGenericOrShared);
  expect_variant(
      "mbarrier.try_wait.phase_type::primary.shared::cta.b64 %p0, "
      "[shared_value], %state;",
      Mbarrier::VariantType::TryWaitTokenPrimarySharedCta);
  expect_variant(
      "mbarrier.try_wait.parity.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], "
      "1, 2;",
      Mbarrier::VariantType::TryWaitParityPrimaryGenericOrShared);
  expect_variant(
      "mbarrier.try_wait.parity.phase_type::primary.shared::cta.b64 %p0, "
      "[shared_value], 1;",
      Mbarrier::VariantType::TryWaitParityPrimarySharedCta);
  expect_variant(
      "mbarrier.try_wait.parity.phase_type::conditional.b64 %p0, [%rd0], 1, 2;",
      Mbarrier::VariantType::TryWaitParityConditionalGenericOrShared);
  expect_variant(
      "mbarrier.try_wait.parity.phase_type::conditional.shared::cta.b64 %p0, "
      "[shared_value], 1;",
      Mbarrier::VariantType::TryWaitParityConditionalSharedCta);

  for (
      const std::string_view source : {
          "mbarrier.test_wait.phase_type::conditional.b64 %p0, [%rd0], %state;",
          "mbarrier.test_wait.parity.phase_type::conditional.b64 %p0|%p1, "
          "[%rd0], 1;",
          "mbarrier.test_wait.parity.phase_type::conditional.acquire.cta."
          "b64 %p0|%p1, [shared_value], 1;",
          "mbarrier.try_wait.phase_type::primary.b64 %p0, %b0, [%rd0], %state;",
          "mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, _, [%rd0], "
          "%state;",
          "mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, 1, [%rd0], "
          "%state;",
          "mbarrier.try_wait.parity.phase_type::conditional.acquire.cta.b64 "
          "%p0|%p1, [shared_value], 1;",
      }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsPendingCount) {
  const auto expect_variant = [](std::string_view source) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, Mbarrier::VariantType::PendingCount);
  };
  expect_variant("mbarrier.pending_count.b64 %r0, %state;");
  expect_variant("mbarrier.pending_count.layout::v0.b64 %r0, %state;");

  for (const std::string_view source : {
           "mbarrier.pending_count.layout::v1.b64 %r0, %state;",
           "mbarrier.pending_count.b32 %r0, %state;",
           "mbarrier.pending_count.b64 %r0, %state, 1;",
           "mbarrier.pending_count.shared.b64 %r0, %state;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
  for (const std::string_view source : {
           "mbarrier.pending_count.b64 _, %state;",
           "mbarrier.pending_count.b64 1, %state;",
           "mbarrier.pending_count.b64 %r0, _;",
           "mbarrier.pending_count.b64 %r0, 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsCheckLayout) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_variant("mbarrier.check_layout.layout::v0.b64 %p0, [%rd0];",
                 Mbarrier::VariantType::CheckLayoutGenericV0);
  expect_variant("mbarrier.check_layout.layout::v1.b64 %p0, [%rd0];",
                 Mbarrier::VariantType::CheckLayoutGenericV1);
  expect_variant(
      "mbarrier.check_layout.layout::v0.shared::cta.b64 %p0, [shared_value];",
      Mbarrier::VariantType::CheckLayoutSharedCtaV0);
  expect_variant(
      "mbarrier.check_layout.layout::v1.shared::cta.b64 %p0, [shared_value];",
      Mbarrier::VariantType::CheckLayoutSharedCtaV1);

  for (const std::string_view source : {
           "mbarrier.check_layout.b64 %p0, [%rd0];",
           "mbarrier.check_layout.layout::v2.b64 %p0, [%rd0];",
           "mbarrier.check_layout.shared.b64 %p0, [%rd0];",
           "mbarrier.check_layout.layout::v0.b32 %p0, [%rd0];",
           "mbarrier.check_layout.layout::v0.relaxed.b64 %p0, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  for (const std::string_view source : {
           "mbarrier.check_layout.layout::v0.b64 _, [%rd0];",
           "mbarrier.check_layout.layout::v0.b64 %p0, [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsInitLayoutsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.init.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitGenericV0);
  expect_variant("mbarrier.init.shared.b64 [%rd0], %r0;",
                 Mbarrier::VariantType::InitSharedV0);
  expect_variant("mbarrier.init.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedCtaV0);
  expect_variant("mbarrier.init.layout::v1.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitGenericV1);
  expect_variant("mbarrier.init.layout::v1.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedV1);
  expect_variant("mbarrier.init.layout::v1.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedCtaV1);

  for (const std::string_view source : {
           "mbarrier.init.b64.shared [%rd0], 1;",
           "mbarrier.init.shared.layout::v1.b64 [%rd0], 1;",
           "mbarrier.init.shared.shared::cta.b64 [%rd0], 1;",
           "mbarrier.init.layout::v0.layout::v1.b64 [%rd0], 1;",
           "mbarrier.shared.b64 [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(parse_instruction("mbarrier.init.b64 [%rd0];"))
                   .has_value());
}

TEST(SelectVariantMbarrier, SelectsInvalSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.inval.b64 [%rd0];",
                 Mbarrier::VariantType::InvalGeneric);
  expect_variant("mbarrier.inval.shared.b64 [%rd0];",
                 Mbarrier::VariantType::InvalShared);
  expect_variant("mbarrier.inval.shared::cta.b64 [%rd0];",
                 Mbarrier::VariantType::InvalSharedCta);

  for (const std::string_view source : {
           "mbarrier.inval [%rd0];",
           "mbarrier.inval.b64.shared [%rd0];",
           "mbarrier.inval.shared.shared::cta.b64 [%rd0];",
           "mbarrier.inval.shared::cta.shared.b64 [%rd0];",
           "mbarrier.inval.layout::v0.b64 [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Mbarrier>(parse_instruction("mbarrier.inval.b64;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsExpectTxSemanticsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.expect_tx.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxGenericOrShared);
  expect_variant("mbarrier.expect_tx.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxGenericOrShared);
  expect_variant("mbarrier.expect_tx.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxSharedCta);
  expect_variant("mbarrier.expect_tx.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxSharedCluster);
  expect_variant("mbarrier.expect_tx.relaxed.cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cta.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cta.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaSharedCta);
  expect_variant(
      "mbarrier.expect_tx.relaxed.cta.shared::cluster.b64 [%rd0], 1;",
      Mbarrier::VariantType::ExpectTxRelaxedCtaSharedCluster);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterGenericOrShared);
  expect_variant(
      "mbarrier.expect_tx.relaxed.cluster.shared::cta.b64 [%rd0], 1;",
      Mbarrier::VariantType::ExpectTxRelaxedClusterSharedCta);
  expect_variant(
      "mbarrier.expect_tx.relaxed.cluster.shared::cluster.b64 [%rd0], 1;",
      Mbarrier::VariantType::ExpectTxRelaxedClusterSharedCluster);

  for (const std::string_view source : {
           "mbarrier.expect_tx.relaxed.b64 [%rd0], 1;",
           "mbarrier.expect_tx.cta.b64 [%rd0], 1;",
           "mbarrier.expect_tx.relaxed.gpu.b64 [%rd0], 1;",
           "mbarrier.expect_tx.relaxed.cta.global.b64 [%rd0], 1;",
           "mbarrier.expect_tx.shared.relaxed.cta.b64 [%rd0], 1;",
           "mbarrier.expect_tx.shared [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Mbarrier>(parse_instruction("mbarrier.expect_tx.b64 [%rd0];"))
          .has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
                   parse_instruction("mbarrier.expect_tx.b64 [%rd0], %tid.x;"))
                   .has_value());
}

TEST(SelectVariantMbarrier, SelectsCompleteTxSemanticsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.complete_tx.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxGenericOrShared);
  expect_variant("mbarrier.complete_tx.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxGenericOrShared);
  expect_variant("mbarrier.complete_tx.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxSharedCta);
  expect_variant("mbarrier.complete_tx.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxSharedCluster);
  expect_variant("mbarrier.complete_tx.relaxed.cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cta.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cta.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaSharedCta);
  expect_variant(
      "mbarrier.complete_tx.relaxed.cta.shared::cluster.b64 [%rd0], 1;",
      Mbarrier::VariantType::CompleteTxRelaxedCtaSharedCluster);
  expect_variant(
      "mbarrier.complete_tx.relaxed.cluster.b64 [%rd0], 1;",
      Mbarrier::VariantType::CompleteTxRelaxedClusterGenericOrShared);
  expect_variant(
      "mbarrier.complete_tx.relaxed.cluster.shared.b64 [%rd0], 1;",
      Mbarrier::VariantType::CompleteTxRelaxedClusterGenericOrShared);
  expect_variant(
      "mbarrier.complete_tx.relaxed.cluster.shared::cta.b64 [%rd0], 1;",
      Mbarrier::VariantType::CompleteTxRelaxedClusterSharedCta);
  expect_variant(
      "mbarrier.complete_tx.relaxed.cluster.shared::cluster.b64 [%rd0], 1;",
      Mbarrier::VariantType::CompleteTxRelaxedClusterSharedCluster);

  for (const std::string_view source : {
           "mbarrier.complete_tx.relaxed.b64 [%rd0], 1;",
           "mbarrier.complete_tx.cta.b64 [%rd0], 1;",
           "mbarrier.complete_tx.relaxed.gpu.b64 [%rd0], 1;",
           "mbarrier.complete_tx.relaxed.cta.global.b64 [%rd0], 1;",
           "mbarrier.complete_tx.shared.relaxed.cta.b64 [%rd0], 1;",
           "mbarrier.complete_tx.shared [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Mbarrier>(parse_instruction("mbarrier.complete_tx.b64 [%rd0];"))
          .has_value());
  EXPECT_FALSE(
      resolve<Mbarrier>(
          parse_instruction("mbarrier.complete_tx.b64 [%rd0], %tid.x;"))
          .has_value());
}

TEST(SelectVariantMbarrier, SelectsArriveFormsAndLayouts) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  const auto sink_ast = parse_instruction("mbarrier.arrive.b64 _, [%rd0];");
  const auto* sink = test_ir_access::get_if<syntax_ast::AstIdentifierRef>(
      &sink_ast.operands[0]);
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(sink->syntax.text, "_");

  expect_variant("mbarrier.arrive.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveGenericOrShared);
  expect_variant("mbarrier.arrive.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveSharedCta);
  expect_variant("mbarrier.arrive.shared::cluster.b64 _, [%rd0];",
                 Mbarrier::VariantType::ArriveSharedCluster);
  expect_variant("mbarrier.arrive.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveSemanticsGenericOrShared);
  expect_variant(
      "mbarrier.arrive.release.cluster.shared::cta.b64 %state, [%rd0];",
      Mbarrier::VariantType::ArriveSemanticsSharedCta);
  expect_variant(
      "mbarrier.arrive.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveSemanticsSharedCluster);
  expect_variant("mbarrier.arrive.expect_tx.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxGenericOrShared);
  expect_variant("mbarrier.arrive.expect_tx.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSharedCta);
  expect_variant("mbarrier.arrive.expect_tx.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSharedCluster);
  expect_variant("mbarrier.arrive.expect_tx.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSemanticsGenericOrShared);
  expect_variant(
      "mbarrier.arrive.expect_tx.release.cluster.shared::cta.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveExpectTxSemanticsSharedCta);
  expect_variant(
      "mbarrier.arrive.expect_tx.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveExpectTxSemanticsSharedCluster);
  expect_variant("mbarrier.arrive.noComplete.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteGenericOrShared);
  expect_variant("mbarrier.arrive.noComplete.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteSharedCta);
  expect_variant(
      "mbarrier.arrive.noComplete.release.cta.b64 %state, [%rd0], 1;",
      Mbarrier::VariantType::ArriveNoCompleteReleaseCtaGenericOrShared);
  expect_variant(
      "mbarrier.arrive.noComplete.release.cta.shared::cta.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveNoCompleteReleaseCtaSharedCta);

  for (const std::string_view source : {
           "mbarrier.arrive.release.b64 %state, [%rd0];",
           "mbarrier.arrive.cta.b64 %state, [%rd0];",
           "mbarrier.arrive.noComplete.relaxed.cta.b64 %state, [%rd0], 1;",
           "mbarrier.arrive.expect_tx.noComplete.b64 %state, [%rd0], 1;",
           "mbarrier.arrive.b64.shared %state, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsArriveDropFormsAndLayouts) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.arrive_drop.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveDropGenericOrShared);
  expect_variant("mbarrier.arrive_drop.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropSharedCta);
  expect_variant("mbarrier.arrive_drop.shared::cluster.b64 _, [%rd0];",
                 Mbarrier::VariantType::ArriveDropSharedCluster);
  expect_variant("mbarrier.arrive_drop.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropSemanticsGenericOrShared);
  expect_variant(
      "mbarrier.arrive_drop.release.cluster.shared::cta.b64 %state, [%rd0];",
      Mbarrier::VariantType::ArriveDropSemanticsSharedCta);
  expect_variant(
      "mbarrier.arrive_drop.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveDropSemanticsSharedCluster);
  expect_variant("mbarrier.arrive_drop.expect_tx.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxGenericOrShared);
  expect_variant("mbarrier.arrive_drop.expect_tx.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSharedCta);
  expect_variant(
      "mbarrier.arrive_drop.expect_tx.shared::cluster.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveDropExpectTxSharedCluster);
  expect_variant(
      "mbarrier.arrive_drop.expect_tx.release.cta.b64 %state, [%rd0], 1;",
      Mbarrier::VariantType::ArriveDropExpectTxSemanticsGenericOrShared);
  expect_variant(
      "mbarrier.arrive_drop.expect_tx.release.cluster.shared::cta.b64 %state, "
      "[%rd0], 1;",
      Mbarrier::VariantType::ArriveDropExpectTxSemanticsSharedCta);
  expect_variant(
      "mbarrier.arrive_drop.expect_tx.relaxed.cta.shared::cluster.b64 _, "
      "[%rd0], 1;",
      Mbarrier::VariantType::ArriveDropExpectTxSemanticsSharedCluster);
  expect_variant("mbarrier.arrive_drop.noComplete.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropNoCompleteGenericOrShared);
  expect_variant(
      "mbarrier.arrive_drop.noComplete.shared::cta.b64 _, [%rd0], 1;",
      Mbarrier::VariantType::ArriveDropNoCompleteSharedCta);
  expect_variant(
      "mbarrier.arrive_drop.noComplete.release.cta.b64 %state, [%rd0], 1;",
      Mbarrier::VariantType::ArriveDropNoCompleteReleaseCtaGenericOrShared);
  expect_variant(
      "mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 _, [%rd0], "
      "1;",
      Mbarrier::VariantType::ArriveDropNoCompleteReleaseCtaSharedCta);

  for (const std::string_view source : {
           "mbarrier.arrive_drop.release.b64 %state, [%rd0];",
           "mbarrier.arrive_drop.cta.b64 %state, [%rd0];",
           "mbarrier.arrive_drop.noComplete.relaxed.cta.b64 %state, [%rd0], 1;",
           "mbarrier.arrive_drop.expect_tx.noComplete.b64 %state, [%rd0], 1;",
           "mbarrier.arrive_drop.b64.shared %state, [%rd0];",
           "mbarrier.arrive_drop.noComplete.shared::cluster.b64 _, [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
}

/** Keep explicit wait qualifier pairs separate from unqualified wait forms. */
TEST(SelectVariantMbarrier, SelectsPairedTestWaitForms) {
  const std::array<std::pair<std::string_view, Mbarrier::VariantType>, 10>
      forms{{
          {"mbarrier.test_wait.acquire.cta.b64 %p0, [%rd0], %state;",
           Mbarrier::VariantType::TestWaitTokenSemanticsGenericOrShared},
          {"mbarrier.test_wait.relaxed.cluster.shared::cta.b64 %p0, "
           "[shared_value], %state;",
           Mbarrier::VariantType::TestWaitTokenSemanticsSharedCta},
          {"mbarrier.test_wait.parity.acquire.cluster.shared.b64 %p0, "
           "[shared_value], 1;",
           Mbarrier::VariantType::TestWaitParitySemanticsGenericOrShared},
          {"mbarrier.test_wait.parity.relaxed.cta.shared::cta.b64 %p0, "
           "[shared_value], %phase;",
           Mbarrier::VariantType::TestWaitParitySemanticsSharedCta},
          {"mbarrier.test_wait.phase_type::primary.acquire.cta.b64 %p0|%p1, "
           "%b0, [%rd0], %state;",
           Mbarrier::VariantType::TestWaitTokenPrimarySemanticsGenericOrShared},
          {"mbarrier.test_wait.phase_type::primary.relaxed.cluster.shared::cta."
           "b64 %p0, [shared_value], %state;",
           Mbarrier::VariantType::TestWaitTokenPrimarySemanticsSharedCta},
          {"mbarrier.test_wait.parity.phase_type::primary.acquire.cluster.b64 "
           "%p0|%p1, %b0, [%rd0], 1;",
           Mbarrier::VariantType::
               TestWaitParityPrimarySemanticsGenericOrShared},
          {"mbarrier.test_wait.parity.phase_type::primary.relaxed.cta.shared::"
           "cta.b64 %p0, [shared_value], 0;",
           Mbarrier::VariantType::TestWaitParityPrimarySemanticsSharedCta},
          {"mbarrier.test_wait.parity.phase_type::conditional.acquire.cta.b64 "
           "%p0, [%rd0], 1;",
           Mbarrier::VariantType::
               TestWaitParityConditionalSemanticsGenericOrShared},
          {"mbarrier.test_wait.parity.phase_type::conditional.relaxed.cluster."
           "shared::cta.b64 %p0, [shared_value], %phase;",
           Mbarrier::VariantType::TestWaitParityConditionalSemanticsSharedCta},
      }};
  for (const auto& [source, expected] : forms) {
    SCOPED_TRACE(source);
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  }
  for (const std::string_view source : {
           "mbarrier.test_wait.acquire.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.cta.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.relaxed.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.cluster.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.cta.acquire.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.acquire.cta.shared::cluster.b64 %p0, "
           "[shared_value], %state;",
           "mbarrier.test_wait.phase_type::conditional.acquire.cta.b64 %p0, "
           "[%rd0], %state;",
       }) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto parsed = parser.parseInstruction();
    if (parsed)
      EXPECT_FALSE(selectVariant<Mbarrier>(*parsed).has_value());
  }
}

/** Select explicit try-wait qualifier pairs across all structural layouts. */
TEST(SelectVariantMbarrier, SelectsPairedTryWaitForms) {
  const std::array<std::pair<std::string_view, Mbarrier::VariantType>, 10>
      forms{{
          {"mbarrier.try_wait.acquire.cta.b64 %p0, [%rd0], %state;",
           Mbarrier::VariantType::TryWaitTokenSemanticsGenericOrShared},
          {"mbarrier.try_wait.relaxed.cluster.shared::cta.b64 %p0, "
           "[shared_value], %state, 12;",
           Mbarrier::VariantType::TryWaitTokenSemanticsSharedCta},
          {"mbarrier.try_wait.parity.acquire.cluster.shared.b64 %p0, "
           "[shared_value], 1;",
           Mbarrier::VariantType::TryWaitParitySemanticsGenericOrShared},
          {"mbarrier.try_wait.parity.relaxed.cta.shared::cta.b64 %p0, "
           "[shared_value], %phase, %hint;",
           Mbarrier::VariantType::TryWaitParitySemanticsSharedCta},
          {"mbarrier.try_wait.phase_type::primary.acquire.cta.b64 %p0|%p1, "
           "%b0, [%rd0], %state, 20;",
           Mbarrier::VariantType::TryWaitTokenPrimarySemanticsGenericOrShared},
          {"mbarrier.try_wait.phase_type::primary.relaxed.cluster.shared::cta."
           "b64 %p0, [shared_value], %state;",
           Mbarrier::VariantType::TryWaitTokenPrimarySemanticsSharedCta},
          {"mbarrier.try_wait.parity.phase_type::primary.acquire.cluster.b64 "
           "%p0|%p1, %b0, [%rd0], 1, %hint;",
           Mbarrier::VariantType::TryWaitParityPrimarySemanticsGenericOrShared},
          {"mbarrier.try_wait.parity.phase_type::primary.relaxed.cta.shared::"
           "cta.b64 %p0, [shared_value], 0;",
           Mbarrier::VariantType::TryWaitParityPrimarySemanticsSharedCta},
          {"mbarrier.try_wait.parity.phase_type::conditional.acquire.cta.b64 "
           "%p0, [%rd0], 1, 8;",
           Mbarrier::VariantType::
               TryWaitParityConditionalSemanticsGenericOrShared},
          {"mbarrier.try_wait.parity.phase_type::conditional.relaxed.cluster."
           "shared::cta.b64 %p0, [shared_value], %phase;",
           Mbarrier::VariantType::TryWaitParityConditionalSemanticsSharedCta},
      }};
  for (const auto& [source, expected] : forms) {
    SCOPED_TRACE(source);
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  }
  for (const std::string_view source : {
           "mbarrier.try_wait.acquire.b64 %p0, [%rd0], %state;",
           "mbarrier.try_wait.cta.b64 %p0, [%rd0], %state;",
           "mbarrier.try_wait.relaxed.b64 %p0, [%rd0], %state;",
           "mbarrier.try_wait.cluster.b64 %p0, [%rd0], %state;",
           "mbarrier.try_wait.cta.acquire.b64 %p0, [%rd0], %state;",
           "mbarrier.try_wait.acquire.cta.shared::cluster.b64 %p0, "
           "[shared_value], %state;",
           "mbarrier.try_wait.phase_type::conditional.acquire.cta.b64 %p0, "
           "[%rd0], %state;",
           "mbarrier.try_wait.acquire.cta.acquire.cta.b64 %p0, [%rd0], %state;",
       }) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto parsed = parser.parseInstruction();
    if (parsed)
      EXPECT_FALSE(selectVariant<Mbarrier>(*parsed).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
