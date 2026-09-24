#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct/resolution.gen.hpp>
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

/** SLCT selector type determines the generated public alternative. */
TEST(ResolveSlct, SelectsTypedNumericSelectorVariants) {
  const auto integer =
      resolve<Slct>(parse_instruction("slct.u32.s32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(integer.has_value()) << integer.error().message;
  const auto* u32_s32 = std::get_if<Slct::S32>(&integer->variant);
  ASSERT_NE(u32_s32, nullptr);
  EXPECT_EQ(
      std::get<ResolvedRegisterRef>(u32_s32->selector.value).register_class,
      ResolvedRegisterClass::General);
  EXPECT_EQ(u32_s32->dtype.value, ScalarType::U32);

  const auto floating = resolve<Slct>(
      parse_instruction("slct.ftz.u64.f32 %rd0, %rd1, %rd2, %f0;"));
  ASSERT_TRUE(floating.has_value()) << floating.error().message;
  const auto* f32 = std::get_if<Slct::F32>(&floating->variant);
  ASSERT_NE(f32, nullptr);
  EXPECT_TRUE(f32->ftz.value);
  EXPECT_EQ(f32->dtype.value, ScalarType::U64);
}

/** SLCT excludes FTZ on integer selectors and unsupported type suffixes. */
TEST(ResolveSlct, RejectsIllegalModifierForms) {
  for (const auto source : {
           "slct.ftz.u32.s32 %r0, %r1, %r2, %r3;",
           "slct.ftz.u64.s32 %rd0, %rd1, %rd2, %r0;",
           "slct.u32.f64 %r0, %r1, %r2, %fd0;",
           "slct.f16.s32 %h0, %h1, %h2, %r0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Slct>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedSlctAvailability) {
  for (const auto source : {
           "slct.u32.s32 %r0, %r1, %r2, %r3;",
           "slct.ftz.u64.f32 %rd0, %rd1, %rd2, %f0;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto slct = resolve<Slct>(*ast);
    ASSERT_TRUE(slct.has_value()) << slct.error().message;
    const auto rejected =
        check(*slct, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*slct, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
