#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/mad/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mad/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mad/resolution.gen.hpp>
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

TEST(ResolveMad, SelectsFrozenLoU32VariantAndImmediateSource) {
  const auto resolved =
      resolve<Mad>(parse_instruction("mad.lo.u32 %r0, %r1, 7, %r2;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mad = std::get_if<Mad::LoU32>(&resolved->variant);
  ASSERT_NE(mad, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mad->src2.value));
}

TEST(ResolveMad, SelectsIntegerAndExplicitFloatingVariants) {
  const auto lo =
      resolve<Mad>(parse_instruction("mad.lo.s32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(lo.has_value()) << lo.error().message;
  ASSERT_NE(std::get_if<Mad::LoS32>(&lo->variant), nullptr);
  EXPECT_TRUE(Mad::LoS32::lo);
  EXPECT_EQ(Mad::LoS32::type, ScalarType::S32);

  const auto wide =
      resolve<Mad>(parse_instruction("mad.wide.u32 %rd0, %r1, %r2, %rd3;"));
  ASSERT_TRUE(wide.has_value()) << wide.error().message;
  ASSERT_NE(std::get_if<Mad::WideU32>(&wide->variant), nullptr);
  EXPECT_TRUE(Mad::WideU32::wide);
  EXPECT_EQ(Mad::WideU32::type, ScalarType::U32);

  const auto rn =
      resolve<Mad>(parse_instruction("mad.rn.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(rn.has_value()) << rn.error().message;
  ASSERT_NE(std::get_if<Mad::RnF32>(&rn->variant), nullptr);
  EXPECT_EQ(Mad::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Mad::RnF32::type, ScalarType::F32);

  const auto directed =
      resolve<Mad>(parse_instruction("mad.rz.ftz.sat.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(directed.has_value()) << directed.error().message;
  const auto* directed_variant =
      std::get_if<Mad::DirectedF32>(&directed->variant);
  ASSERT_NE(directed_variant, nullptr);
  EXPECT_EQ(directed_variant->rounding.value, RoundingMode::Rz);

  const auto f64 =
      resolve<Mad>(parse_instruction("mad.rp.f64 %d0, %d1, %d2, %d3;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  const auto* f64_variant = std::get_if<Mad::DirectedF64>(&f64->variant);
  ASSERT_NE(f64_variant, nullptr);
  EXPECT_EQ(f64_variant->rounding.value, RoundingMode::Rp);
}

TEST(ResolveMad, RejectsIllegalModifiers) {
  for (const auto source :
       {"mad.u32 %r0, %r1, %r2, %r3;", "mad.lo.sat.s32 %r0, %r1, %r2, %r3;",
        "mad.f32 %f0, %f1, %f2, %f3;", "mad.rn.ftz.f64 %d0, %d1, %d2, %d3;",
        "mad.rn.sat.f64 %d0, %d1, %d2, %d3;",
        "mad.lo.cc.s16 %r0, %r1, %r2, %r3;"}) {
    const auto selected = selectVariant<Mad>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedMadLoU32Availability) {
  PtxSyntaxParser parser("mad.lo.u32 %r0, %r1, %r2, %r3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mad = resolve<Mad>(*ast);
  ASSERT_TRUE(mad.has_value()) << mad.error().message;
  const auto rejected =
      check(*mad, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*mad, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMadLoS32AndWideU32Availability) {
  for (const auto source : {"mad.lo.s32 %r0, %r1, %r2, %r3;",
                            "mad.wide.u32 %rd0, %r1, %r2, %rd3;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto mad = resolve<Mad>(*ast);
    ASSERT_TRUE(mad.has_value()) << mad.error().message;
    const auto rejected =
        check(*mad, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*mad, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedMadRnF32Availability) {
  PtxSyntaxParser parser("mad.rn.f32 %f0, %f1, %f2, %f3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mad = resolve<Mad>(*ast);
  ASSERT_TRUE(mad.has_value()) << mad.error().message;
  const auto old_ptx =
      check(*mad, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*mad, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                          .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*mad, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
