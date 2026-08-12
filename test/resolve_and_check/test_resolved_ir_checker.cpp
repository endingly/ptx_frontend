#include <gtest/gtest.h>

#include <array>

#include "ptx_ir/ptx_resolved_ir_checker.hpp"
#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend::resolved_ir::checker {
namespace {

const SourceRange kInstructionRange{{4, 3}, {4, 17}};

constexpr VariantDescriptor kVariants[] = {
    {
        .variant_name = "PackedOptionalSatSm120",
        .availability =
            {
                .minimum_ptx_version = {9, 2},
                .minimum_sm_version = 120,
                .required_family = "sm_120f",
            },
        .rule_id = "integer_arith.add_packed",
    },
};

constexpr InstructionDescriptor kInstruction{
    .opcode_name = "add",
    .variants = kVariants,
};

TEST(ResolvedIrChecker, AcceptsAvailableVariant) {
  constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const Context context{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .families = families},
      .instruction_range = kInstructionRange,
  };

  const auto result =
      check_common(kInstruction, "PackedOptionalSatSm120", context);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(is_available(kVariants[0].availability, context.target));
}

TEST(ResolvedIrChecker, AccumulatesTargetAvailabilityDiagnostics) {
  constexpr std::array<std::string_view, 1> families{"sm_100"};
  const Context context{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .families = families},
      .instruction_range = kInstructionRange,
  };

  const auto result =
      check_common(kInstruction, "PackedOptionalSatSm120", context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 3U);
  EXPECT_EQ(result.error()[0].kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(result.error()[1].kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error()[2].kind,
            CheckDiagnosticKind::UnsupportedTargetFamily);
  EXPECT_EQ(result.error()[0].range, kInstructionRange);
}

TEST(ResolvedIrChecker, DiagnosesMissingGeneratedVariantDescriptor) {
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_common(kInstruction, "Missing", context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::MissingVariantDescriptor);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, GeneratedAddWrapperUsesYamlAvailability) {
  PtxSyntaxParser parser("add.sat.u8x4 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

  constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const Context unsupported_context{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .families = families},
      .instruction_range = ast->range,
  };

  const auto unsupported = check(*resolved, unsupported_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_context{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .families = families},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, supported_context).has_value());
}

TEST(ResolvedIrChecker, GeneratedAddWrapperChecksImmediateTypeExpression) {
  PtxSyntaxParser parser("add.s32 %r0, %r1, 7;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  auto* immediate = std::get_if<ResolvedImmediate>(&add->src2.value);
  ASSERT_NE(immediate, nullptr);
  immediate->type = ScalarType::U32;

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().range,
            std::get<syntax_ast::AstImmediate>(ast->operands[2]).syntax.range);
}

TEST(ResolvedIrChecker, GeneratedAddWrapperChecksSelectedOperandLayoutTag) {
  PtxSyntaxParser parser("add.s32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  add->operand_layout = ResolvedOperandLayoutTag{1};

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::InvalidOperandLayoutTag);
  EXPECT_EQ(result.error().front().range, ast->range);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
