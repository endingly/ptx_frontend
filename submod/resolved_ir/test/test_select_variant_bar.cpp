#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/bar/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/bar/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/bar/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
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

TEST(SelectVariantBar, SelectsEveryGeneratedVariant) {
  const auto expect_variant = [](std::string_view source,
                                 Bar::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Bar>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("bar.sync 0;", Bar::VariantType::Sync);
  expect_variant("bar.cta.sync 0;", Bar::VariantType::CtaSync);
  expect_variant("bar.arrive 0, 32;", Bar::VariantType::Arrive);
  expect_variant("bar.cta.arrive 0, 32;", Bar::VariantType::CtaArrive);
  expect_variant("bar.red.popc.u32 %r0, 1, %p1;", Bar::VariantType::RedPopcU32);
  expect_variant("bar.cta.red.popc.u32 %r0, 1, !%p1;",
                 Bar::VariantType::CtaRedPopcU32);
  expect_variant("bar.red.and.pred %p0, 1, %p1;", Bar::VariantType::RedAndPred);
  expect_variant("bar.cta.red.and.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedAndPred);
  expect_variant("bar.red.or.pred %p0, 1, %p1;", Bar::VariantType::RedOrPred);
  expect_variant("bar.cta.red.or.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedOrPred);
  expect_variant("bar.warp.sync 0xffffffff;", Bar::VariantType::WarpSync);

  for (const std::string_view source :
       {"bar.warp 0xffffffff;", "bar.warp.arrive 0xffffffff;"}) {
    const auto selected = selectVariant<Bar>(parse_instruction(source));
    EXPECT_FALSE(selected.has_value());
  }
}

/** @brief Keeps fixed scalar control operands out of ordinary narrowing. */
TEST(ResolveBar, RejectsOutOfRangeFixedScalarImmediates) {
  for (const auto literal : {"4294967296", "-1U"}) {
    SCOPED_TRACE(literal);
    const auto resolved = resolve<Bar>(
        parse_instruction(std::string("bar.sync ") + literal + ";"));
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().message,
              std::string("Integer literal '") + literal +
                  "' is out of range for scalar type 'U32'.");
  }
}

TEST(ResolveBar, BuildsPredicateReductionWithThreadCount) {
  const auto ast = parse_instruction("bar.cta.red.and.pred %p0, 1, 64, !%p1;");

  const auto resolved = resolve<Bar>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* bar = std::get_if<Bar::CtaRedAndPred>(&resolved->variant);
  ASSERT_NE(bar, nullptr);
  EXPECT_EQ(bar->operand_layout, (ResolvedOperandLayoutTag{1}));
  ASSERT_TRUE(
      std::holds_alternative<Bar::CtaRedAndPred::WithThreadCountOperands>(
          bar->operands));
  const auto& operands =
      std::get<Bar::CtaRedAndPred::WithThreadCountOperands>(bar->operands);
  EXPECT_EQ(operands.dst.value.register_ref.spelling, "%p0");
  EXPECT_EQ(operands.dst.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.dst.value.register_ref.index, 0U);
  EXPECT_FALSE(operands.dst.value.negated);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.barrier.value).bits, 1U);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.thread_count.value).bits, 64U);
  EXPECT_EQ(operands.predicate.value.register_ref.spelling, "%p1");
  EXPECT_EQ(operands.predicate.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.predicate.value.register_ref.index, 1U);
  EXPECT_TRUE(operands.predicate.value.negated);
  ASSERT_EQ(operands.predicate.locs.size(), 1U);
  EXPECT_EQ(operands.predicate.locs.front(),
            std::get<syntax_ast::AstPredicateOperand>(ast.operands[3]).range);

  const checker::Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(*resolved, context).has_value());
}

TEST(ResolveBar, RejectsGeneralRegisterInPredicateSlot) {
  const auto ast = parse_instruction("bar.red.and.pred %p0, 1, %r1;");

  const auto resolved = resolve<Bar>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().range,
      std::get<syntax_ast::AstIdentifierRef>(ast.operands[2]).syntax.range);
  EXPECT_EQ(resolved.error().message,
            "Expected a predicate register, got '%r1'.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, GeneratedBarWrapperRejectsMismatchedLayoutPayload) {
  PtxSyntaxParser parser("bar.sync 1, 128;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto resolved = resolve<Bar>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* bar = std::get_if<Bar::Sync>(&resolved->variant);
  ASSERT_NE(bar, nullptr);
  ASSERT_TRUE(std::holds_alternative<Bar::Sync::BarrierAndThreadCountOperands>(
      bar->operands));
  EXPECT_EQ(bar->operand_layout, (ResolvedOperandLayoutTag{2}));

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, context).has_value());

  bar->operand_layout = ResolvedOperandLayoutTag{0};
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::OperandLayoutPayloadMismatch);
  EXPECT_EQ(result.error().front().range, ast->range);
}

TEST(ResolvedIrChecker, GeneratedBarWrapperChecksLayoutAvailability) {
  PtxSyntaxParser immediate_parser("bar.sync 1;");
  const auto immediate_ast = immediate_parser.parseInstruction();
  ASSERT_TRUE(immediate_ast.has_value())
      << immediate_ast.diagnostics.front().message;
  auto immediate = resolve<Bar>(*immediate_ast);
  ASSERT_TRUE(immediate.has_value()) << immediate.error().message;

  const Context sm10_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = immediate_ast->range,
  };
  EXPECT_TRUE(check(*immediate, sm10_context).has_value());

  PtxSyntaxParser register_parser("bar.sync %r1;");
  const auto register_ast = register_parser.parseInstruction();
  ASSERT_TRUE(register_ast.has_value())
      << register_ast.diagnostics.front().message;
  auto register_barrier = resolve<Bar>(*register_ast);
  ASSERT_TRUE(register_barrier.has_value()) << register_barrier.error().message;

  const auto unsupported = check(*register_barrier, sm10_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context sm20_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = register_ast->range,
  };
  EXPECT_TRUE(check(*register_barrier, sm20_context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
