#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/min.gen.hpp>
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

TEST(ResolveMin, SelectsSignedBinaryAndTernaryVariants) {
  const auto s32 = resolveMin(parse_instruction("min.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(dynamic_cast<MinS32*>(s32->get()), nullptr);
  EXPECT_EQ(MinS32::type, ScalarType::S32);

  const auto nan = resolveMin(parse_instruction("min.NaN.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(nan.has_value()) << nan.error().message;
  ASSERT_NE(dynamic_cast<MinF32*>(nan->get()), nullptr);
  EXPECT_EQ(MinF32::type, ScalarType::F32);
  EXPECT_TRUE(dynamic_cast<MinF32&>(**nan).nan.value);
  EXPECT_EQ(dynamic_cast<MinF32&>(**nan).operand_layout,
            (ResolvedOperandLayoutTag{0}));

  const auto ternary =
      resolveMin(parse_instruction("min.abs.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(ternary.has_value()) << ternary.error().message;
  ASSERT_NE(dynamic_cast<MinF32*>(ternary->get()), nullptr);
  EXPECT_TRUE(dynamic_cast<MinF32&>(**ternary).abs.value);
  EXPECT_EQ(dynamic_cast<MinF32&>(**ternary).operand_layout,
            (ResolvedOperandLayoutTag{1}));
}

TEST(ResolveMin, RejectsIllegalModifiers) {
  for (const auto source :
       {"min.nan.f32 %f0, %f1, %f2;", "min.xorsign.f32 %f0, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        select_variant_name(parse_instruction(source), min_syntax_descriptor())
            .has_value());
  }
  // These spellings select a variant and an arity, then fail because the
  // selected layout forbids the modifier.
  for (const auto source : {"min.abs.f32 %f0, %f1, %f2;",
                            "min.xorsign.abs.f32 %f0, %f1, %f2, %f3;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolveMin(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_FALSE((*resolved)
                     ->check(checker::Context{
                         .target = {.ptx_version = {8, 8}, .sm_version = 100}})
                     .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedMinAvailability) {
  PtxSyntaxParser integer_parser("min.s32 %r0, %r1, %r2;");
  const auto integer_ast = integer_parser.parseInstruction();
  ASSERT_TRUE(integer_ast.has_value())
      << integer_ast.diagnostics.front().message;
  const auto integer_min = resolveMin(*integer_ast);
  ASSERT_TRUE(integer_min.has_value()) << integer_min.error().message;
  const auto old_integer =
      (*integer_min)
          ->check(Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = integer_ast->range});
  ASSERT_FALSE(old_integer.has_value());
  EXPECT_EQ(old_integer.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      (*integer_min)
          ->check(Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = integer_ast->range})
          .has_value());

  PtxSyntaxParser nan_parser("min.NaN.f32 %f0, %f1, %f2;");
  const auto nan_ast = nan_parser.parseInstruction();
  ASSERT_TRUE(nan_ast.has_value()) << nan_ast.diagnostics.front().message;
  const auto nan_min = resolveMin(*nan_ast);
  ASSERT_TRUE(nan_min.has_value()) << nan_min.error().message;
  const auto old_ptx = (*nan_min)->check(
      Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
              .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = (*nan_min)->check(
      Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
              .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*nan_min)
          ->check(Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                          .instruction_range = nan_ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
