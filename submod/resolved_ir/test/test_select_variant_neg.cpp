#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/neg.gen.hpp>
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

TEST(ResolveNeg, SelectsFrozenScalarAndPackedVariants) {
  const auto s32 = resolveNeg(parse_instruction("neg.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(dynamic_cast<NegS32*>(s32->get()), nullptr);
  EXPECT_EQ(NegS32::type, ScalarType::S32);

  const auto f32 = resolveNeg(parse_instruction("neg.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(dynamic_cast<NegF32*>(f32->get()), nullptr);
  EXPECT_EQ(NegF32::type, ScalarType::F32);

  const auto f16x2 = resolveNeg(parse_instruction("neg.f16x2 %r0, %r1;"));
  ASSERT_TRUE(f16x2.has_value()) << f16x2.error().message;
  ASSERT_NE(dynamic_cast<NegF16x2*>(f16x2->get()), nullptr);
  EXPECT_EQ(NegF16x2::type, ScalarType::F16x2);
}

TEST(ResolveNeg, RejectsInvalidForms) {
  for (const auto source : {"neg.ftz.f64 %f0, %f1;", "neg.ftz.bf16x2 %r0, %r1;",
                            "neg.sat.s32 %r0, %r1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        select_variant_name(parse_instruction(source), neg_syntax_descriptor())
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedNegAvailability) {
  for (const auto source : {"neg.s32 %r0, %r1;", "neg.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto neg = resolveNeg(*ast);
    ASSERT_TRUE(neg.has_value()) << neg.error().message;
    const auto old_ptx = (*neg)->check(
        Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        (*neg)
            ->check(Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }

  PtxSyntaxParser packed_parser("neg.f16x2 %r0, %r1;");
  const auto packed_ast = packed_parser.parseInstruction();
  ASSERT_TRUE(packed_ast.has_value()) << packed_ast.diagnostics.front().message;
  const auto packed = resolveNeg(*packed_ast);
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  const auto old_ptx = (*packed)->check(
      Context{.target = {.ptx_version = {5, 9}, .sm_version = 53},
              .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = (*packed)->check(
      Context{.target = {.ptx_version = {6, 0}, .sm_version = 52},
              .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*packed)
          ->check(Context{.target = {.ptx_version = {6, 0}, .sm_version = 53},
                          .instruction_range = packed_ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
