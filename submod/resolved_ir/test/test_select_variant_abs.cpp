#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/abs.gen.hpp>
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
  const auto s32 = resolveAbs(parse_instruction("abs.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(dynamic_cast<AbsS32*>(s32->get()), nullptr);
  EXPECT_EQ(AbsS32::type, ScalarType::S32);

  const auto f32 = resolveAbs(parse_instruction("abs.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(dynamic_cast<AbsF32*>(f32->get()), nullptr);
  EXPECT_EQ(AbsF32::type, ScalarType::F32);
}

TEST(ResolveAbs, RejectsInvalidForms) {
  for (const auto source : {"abs.sat.s32 %r0, %r1;", "abs.ftz.f64 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source), abs_syntax_descriptor()).has_value());
  }
  EXPECT_FALSE(resolveAbs(parse_instruction("abs.s32 %r0;")).has_value());
  EXPECT_FALSE(
      resolveAbs(parse_instruction("abs.f32 %f0, %f1, %f2;")).has_value());
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
    const auto abs = resolveAbs(*ast);
    ASSERT_TRUE(abs.has_value()) << abs.error().message;
    const auto old_ptx =
        (*abs)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        (*abs)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
