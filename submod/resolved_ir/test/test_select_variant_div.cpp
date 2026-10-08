#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/div.gen.hpp>
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
  const auto resolved = resolveDiv(parse_instruction("div.u32 %r0, %r1, 0;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* div = dynamic_cast<DivU32*>(resolved->get());
  ASSERT_NE(div, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(div->src2.value));
}

TEST(ResolveDiv, SelectsM12S32AndRnFloatingVariants) {
  const auto s32 = resolveDiv(parse_instruction("div.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(dynamic_cast<DivS32*>(s32->get()), nullptr);
  EXPECT_EQ(DivS32::type, ScalarType::S32);

  const auto f32 = resolveDiv(parse_instruction("div.rn.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(dynamic_cast<DivRnF32*>(f32->get()), nullptr);
  EXPECT_EQ(DivRnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(DivRnF32::type, ScalarType::F32);

  const auto f64 = resolveDiv(parse_instruction("div.rn.f64 %d0, %d1, %d2;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  ASSERT_NE(dynamic_cast<DivRnF64*>(f64->get()), nullptr);
  EXPECT_EQ(DivRnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(DivRnF64::type, ScalarType::F64);
}

TEST(ResolveDiv, RejectsInvalidFloatingModeCombinations) {
  for (const auto source :
       {"div.f32 %f0, %f1, %f2;", "div.f64 %d0, %d1, %d2;",
        "div.approx.rn.f32 %f0, %f1, %f2;",
        "div.approx.full.f32 %f0, %f1, %f2;", "div.rn.ftz.f64 %d0, %d1, %d2;",
        "div.full.f64 %d0, %d1, %d2;", "div.rn.f16 %h0, %h1, %h2;",
        "div.sat.u32 %r0, %r1, %r2;"}) {
    const auto selected =
        select_variant_name(parse_instruction(source), div_syntax_descriptor());
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
  const auto div = resolveDiv(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto rejected =
      (*div)->check(Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      (*div)
          ->check(Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF32Availability) {
  PtxSyntaxParser parser("div.rn.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolveDiv(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx =
      (*div)->check(Context{.target = {.ptx_version = {1, 3}, .sm_version = 20},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      (*div)->check(Context{.target = {.ptx_version = {1, 4}, .sm_version = 19},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*div)
          ->check(Context{.target = {.ptx_version = {1, 4}, .sm_version = 20},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF64Availability) {
  PtxSyntaxParser parser("div.rn.f64 %d0, %d1, %d2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolveDiv(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx =
      (*div)->check(Context{.target = {.ptx_version = {1, 3}, .sm_version = 13},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      (*div)->check(Context{.target = {.ptx_version = {1, 4}, .sm_version = 12},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*div)
          ->check(Context{.target = {.ptx_version = {1, 4}, .sm_version = 13},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
