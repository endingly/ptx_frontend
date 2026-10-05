#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/slct.gen.hpp>
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
      resolveSlct(parse_instruction("slct.u32.s32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(integer.has_value()) << integer.error().message;
  const auto* u32_s32 = dynamic_cast<SlctS32*>(integer->get());
  ASSERT_NE(u32_s32, nullptr);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(u32_s32->selector.value)
                .register_class,
            ResolvedRegisterClass::General);
  EXPECT_EQ(u32_s32->dtype.value, ScalarType::U32);

  const auto floating = resolveSlct(
      parse_instruction("slct.ftz.u64.f32 %rd0, %rd1, %rd2, %f0;"));
  ASSERT_TRUE(floating.has_value()) << floating.error().message;
  const auto* f32 = dynamic_cast<SlctF32*>(floating->get());
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
    EXPECT_FALSE(select_variant_name(parse_instruction(source), slct_syntax_descriptor()).has_value());
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
    const auto slct = resolveSlct(*ast);
    ASSERT_TRUE(slct.has_value()) << slct.error().message;
    const auto rejected =
        (*slct)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        (*slct)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
