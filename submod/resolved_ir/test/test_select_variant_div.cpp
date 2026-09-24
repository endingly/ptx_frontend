#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/div/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div/checker.gen.hpp>
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

TEST(ResolveDiv, SelectsFrozenU32VariantAndAcceptsZeroImmediate) {
  const auto resolved = resolve<Div>(parse_instruction("div.u32 %r0, %r1, 0;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* div = std::get_if<Div::U32>(&resolved->variant);
  ASSERT_NE(div, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(div->src2.value));
}

TEST(ResolveDiv, SelectsM12S32AndRnFloatingVariants) {
  const auto s32 = resolve<Div>(parse_instruction("div.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Div::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Div::S32::type, ScalarType::S32);

  const auto f32 = resolve<Div>(parse_instruction("div.rn.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Div::RnF32>(&f32->variant), nullptr);
  EXPECT_EQ(Div::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Div::RnF32::type, ScalarType::F32);

  const auto f64 = resolve<Div>(parse_instruction("div.rn.f64 %d0, %d1, %d2;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  ASSERT_NE(std::get_if<Div::RnF64>(&f64->variant), nullptr);
  EXPECT_EQ(Div::RnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(Div::RnF64::type, ScalarType::F64);
}

TEST(ResolveDiv, RejectsInvalidFloatingModeCombinations) {
  for (const auto source :
       {"div.f32 %f0, %f1, %f2;", "div.f64 %d0, %d1, %d2;",
        "div.approx.rn.f32 %f0, %f1, %f2;",
        "div.approx.full.f32 %f0, %f1, %f2;", "div.rn.ftz.f64 %d0, %d1, %d2;",
        "div.full.f64 %d0, %d1, %d2;", "div.rn.f16 %h0, %h1, %h2;",
        "div.sat.u32 %r0, %r1, %r2;"}) {
    const auto selected = selectVariant<Div>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedDivU32Availability) {
  PtxSyntaxParser parser("div.u32 %r0, %r1, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto rejected =
      check(*div, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*div, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF32Availability) {
  PtxSyntaxParser parser("div.rn.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx =
      check(*div, Context{.target = {.ptx_version = {1, 3}, .sm_version = 20},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 19},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 20},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF64Availability) {
  PtxSyntaxParser parser("div.rn.f64 %d0, %d1, %d2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx =
      check(*div, Context{.target = {.ptx_version = {1, 3}, .sm_version = 13},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 12},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 13},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
