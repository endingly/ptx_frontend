#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/abs/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/abs/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/abs/checker.gen.hpp>
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

TEST(ResolveAbs, SelectsFrozenSignedAndFloatVariants) {
  const auto s32 = resolve<Abs>(parse_instruction("abs.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Abs::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Abs::S32::type, ScalarType::S32);

  const auto f32 = resolve<Abs>(parse_instruction("abs.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Abs::F32>(&f32->variant), nullptr);
  EXPECT_EQ(Abs::F32::type, ScalarType::F32);
}

TEST(ResolveAbs, RejectsInvalidForms) {
  for (const auto source : {"abs.sat.s32 %r0, %r1;", "abs.ftz.f64 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Abs>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Abs>(parse_instruction("abs.s32 %r0;")).has_value());
  EXPECT_FALSE(
      resolve<Abs>(parse_instruction("abs.f32 %f0, %f1, %f2;")).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedAbsAvailability) {
  for (const auto source : {"abs.s32 %r0, %r1;", "abs.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto abs = resolve<Abs>(*ast);
    ASSERT_TRUE(abs.has_value()) << abs.error().message;
    const auto old_ptx =
        check(*abs, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*abs, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
