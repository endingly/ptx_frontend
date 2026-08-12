#include <gtest/gtest.h>

#include <array>

#include "ptx_ir/ptx_resolved_ir_checker.hpp"
#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend::resolved_ir::checker {
namespace {

const SourceRange kInstructionRange{{4, 3}, {4, 17}};

constexpr ModifierValueAvailabilityDescriptor kModifierValueAvailabilities[] = {
    {
        .kind_id = "type",
        .value_kind = ModifierValueKind::ScalarType,
        .scalar_type = ScalarType::U32,
        .availability =
            {
                .minimum_ptx_version = {2, 0},
                .minimum_sm_version = 20,
            },
    },
};

constexpr VariantDescriptor kVariants[] = {
    {
        .variant_name = "PackedOptionalSatSm120",
        .availability =
            {
                .minimum_ptx_version = {9, 2},
                .minimum_sm_version = 120,
                .required_family = "sm_120f",
            },
        .modifier_value_availabilities = kModifierValueAvailabilities,
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

TEST(ResolvedIrChecker, ChecksSelectedModifierValueAvailability) {
  constexpr std::array<ModifierValueView, 1> values{{
      {
          .kind_id = "type",
          .value_kind = ModifierValueKind::ScalarType,
          .scalar_type = ScalarType::U32,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = {{1, 1}, {1, 8}},
  };

  const auto result = check_modifier_value_availability(
      kModifierValueAvailabilities, values, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2U);
  EXPECT_EQ(result.error()[0].kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(result.error()[1].kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error()[0].range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksFixedScalarOperandTypeDescriptor) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "barrier",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::U32,
          },
      .role = OperandRole::Barrier,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Immediate,
  }};
  constexpr OperandView operands[] = {{
      .field_id = "barrier",
      .actual_shape = OperandShape::Immediate,
      .immediate_type = ScalarType::S32,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_operands(descriptors, {}, operands, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind, CheckDiagnosticKind::OperandTypeMismatch);
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

TEST(ResolvedIrChecker, GeneratedBarWrapperRejectsMismatchedLayoutPayload) {
  PtxSyntaxParser parser("bar.sync 1, 128;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.error().message;

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
  ASSERT_TRUE(immediate_ast.has_value()) << immediate_ast.error().message;
  auto immediate = resolve<Bar>(*immediate_ast);
  ASSERT_TRUE(immediate.has_value()) << immediate.error().message;

  const Context sm10_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = immediate_ast->range,
  };
  EXPECT_TRUE(check(*immediate, sm10_context).has_value());

  PtxSyntaxParser register_parser("bar.sync %r1;");
  const auto register_ast = register_parser.parseInstruction();
  ASSERT_TRUE(register_ast.has_value()) << register_ast.error().message;
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
