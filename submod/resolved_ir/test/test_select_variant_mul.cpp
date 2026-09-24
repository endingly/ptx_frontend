#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/mul/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mul/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mul/checker.gen.hpp>
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

TEST(ResolveMul, SelectsFrozenLoU32VariantAndImmediateSource) {
  const auto resolved =
      resolve<Mul>(parse_instruction("mul.lo.u32 %r0, %r1, 7;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mul = std::get_if<Mul::LoU32>(&resolved->variant);
  ASSERT_NE(mul, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mul->src2.value));
}

TEST(ResolveMul, SelectsM12HiAndWideU32Variants) {
  const auto hi = resolve<Mul>(parse_instruction("mul.hi.u32 %r0, %r1, %r2;"));
  ASSERT_TRUE(hi.has_value()) << hi.error().message;
  const auto* hi_variant = std::get_if<Mul::HiU32>(&hi->variant);
  ASSERT_NE(hi_variant, nullptr);
  EXPECT_TRUE(Mul::HiU32::hi);
  EXPECT_EQ(Mul::HiU32::type, ScalarType::U32);

  const auto wide =
      resolve<Mul>(parse_instruction("mul.wide.u32 %rd0, %r1, %r2;"));
  ASSERT_TRUE(wide.has_value()) << wide.error().message;
  const auto* wide_variant = std::get_if<Mul::WideU32>(&wide->variant);
  ASSERT_NE(wide_variant, nullptr);
  EXPECT_TRUE(Mul::WideU32::wide);
  EXPECT_EQ(Mul::WideU32::type, ScalarType::U32);
}

TEST(ResolveMul, SelectsM12WideS32Variant) {
  const auto resolved =
      resolve<Mul>(parse_instruction("mul.wide.s32 %rd0, %r1, -7;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* wide = std::get_if<Mul::WideS32>(&resolved->variant);
  ASSERT_NE(wide, nullptr);
  EXPECT_TRUE(Mul::WideS32::wide);
  EXPECT_EQ(Mul::WideS32::type, ScalarType::S32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(wide->src2.value));
}

TEST(ResolveMul, RejectsIllegalWide64Variants) {
  for (const auto source :
       {"mul.u32 %r0, %r1, %r2;", "mul.wide.s64 %rd0, %r1, %r2;"}) {
    const auto selected = selectVariant<Mul>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveMul, SelectsFrozenRnF32Variant) {
  const auto resolved =
      resolve<Mul>(parse_instruction("mul.rn.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mul = std::get_if<Mul::RnF32>(&resolved->variant);
  ASSERT_NE(mul, nullptr);
  EXPECT_EQ(mul->rounding.value, RoundingMode::Rn);
  EXPECT_EQ(Mul::RnF32::type, ScalarType::F32);
}

TEST(ResolveMul, RejectsIllegalFloatingModifierCombinations) {
  for (const auto source :
       {"mul.sat.f64 %fd0, %fd1, %fd2;", "mul.sat.f32x2 %rd0, %rd1, %rd2;"}) {
    const auto selected = selectVariant<Mul>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveMul, SelectsImmediateFloatingOperand) {
  const auto resolved =
      resolve<Mul>(parse_instruction("mul.rn.f32 %f0, 1.0, %f2;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mul = std::get_if<Mul::RnF32>(&resolved->variant);
  ASSERT_NE(mul, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mul->src1.value));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedMulLoU32Availability) {
  PtxSyntaxParser parser("mul.lo.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected =
      check(*mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mul, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulHiU32Availability) {
  PtxSyntaxParser parser("mul.hi.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected =
      check(*mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mul, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulWideU32Availability) {
  PtxSyntaxParser parser("mul.wide.u32 %rd0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected =
      check(*mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mul, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulWideS32Availability) {
  PtxSyntaxParser parser("mul.wide.s32 %rd0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected =
      check(*mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mul, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulRnF32Availability) {
  PtxSyntaxParser parser("mul.rn.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected =
      check(*mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mul, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
