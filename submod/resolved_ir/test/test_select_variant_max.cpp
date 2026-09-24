#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/max/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/max/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/max/checker.gen.hpp>
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

TEST(ResolveMax, SelectsSignedBinaryAndTernaryVariants) {
  const auto s32 = resolve<Max>(parse_instruction("max.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Max::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Max::S32::type, ScalarType::S32);

  const auto nan =
      resolve<Max>(parse_instruction("max.NaN.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(nan.has_value()) << nan.error().message;
  ASSERT_NE(std::get_if<Max::F32>(&nan->variant), nullptr);
  EXPECT_EQ(Max::F32::type, ScalarType::F32);
  EXPECT_TRUE(std::get<Max::F32>(nan->variant).nan.value);
  EXPECT_EQ(std::get<Max::F32>(nan->variant).operand_layout,
            (ResolvedOperandLayoutTag{0}));

  const auto ternary =
      resolve<Max>(parse_instruction("max.abs.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(ternary.has_value()) << ternary.error().message;
  ASSERT_NE(std::get_if<Max::F32>(&ternary->variant), nullptr);
  EXPECT_TRUE(std::get<Max::F32>(ternary->variant).abs.value);
  EXPECT_EQ(std::get<Max::F32>(ternary->variant).operand_layout,
            (ResolvedOperandLayoutTag{1}));
}

TEST(ResolveMax, RejectsIllegalModifiers) {
  for (const auto source :
       {"max.nan.f32 %f0, %f1, %f2;", "max.xorsign.f32 %f0, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Max>(parse_instruction(source)).has_value());
  }
  for (const auto source : {"max.abs.f32 %f0, %f1, %f2;",
                            "max.xorsign.abs.f32 %f0, %f1, %f2, %f3;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Max>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_FALSE(
        checker::check(*resolved,
                       checker::Context{.target = {.ptx_version = {8, 8},
                                                   .sm_version = 100}})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedMaxAvailability) {
  PtxSyntaxParser integer_parser("max.s32 %r0, %r1, %r2;");
  const auto integer_ast = integer_parser.parseInstruction();
  ASSERT_TRUE(integer_ast.has_value())
      << integer_ast.diagnostics.front().message;
  const auto integer_max = resolve<Max>(*integer_ast);
  ASSERT_TRUE(integer_max.has_value()) << integer_max.error().message;
  const auto old_integer = check(
      *integer_max, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = integer_ast->range});
  ASSERT_FALSE(old_integer.has_value());
  EXPECT_EQ(old_integer.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*integer_max,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = integer_ast->range})
                  .has_value());

  PtxSyntaxParser nan_parser("max.NaN.f32 %f0, %f1, %f2;");
  const auto nan_ast = nan_parser.parseInstruction();
  ASSERT_TRUE(nan_ast.has_value()) << nan_ast.diagnostics.front().message;
  const auto nan_max = resolve<Max>(*nan_ast);
  ASSERT_TRUE(nan_max.has_value()) << nan_max.error().message;
  const auto old_ptx = check(
      *nan_max, Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *nan_max, Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*nan_max,
                    Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                            .instruction_range = nan_ast->range})
                  .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
