#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/neg/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/neg/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/neg/checker.gen.hpp>
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

TEST(ResolveNeg, SelectsFrozenScalarAndPackedVariants) {
  const auto s32 = resolve<Neg>(parse_instruction("neg.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Neg::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Neg::S32::type, ScalarType::S32);

  const auto f32 = resolve<Neg>(parse_instruction("neg.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Neg::F32>(&f32->variant), nullptr);
  EXPECT_EQ(Neg::F32::type, ScalarType::F32);

  const auto f16x2 = resolve<Neg>(parse_instruction("neg.f16x2 %r0, %r1;"));
  ASSERT_TRUE(f16x2.has_value()) << f16x2.error().message;
  ASSERT_NE(std::get_if<Neg::F16x2>(&f16x2->variant), nullptr);
  EXPECT_EQ(Neg::F16x2::type, ScalarType::F16x2);
}

TEST(ResolveNeg, RejectsInvalidForms) {
  for (const auto source : {"neg.ftz.f64 %f0, %f1;", "neg.ftz.bf16x2 %r0, %r1;",
                            "neg.sat.s32 %r0, %r1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Neg>(parse_instruction(source)).has_value());
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
    const auto neg = resolve<Neg>(*ast);
    ASSERT_TRUE(neg.has_value()) << neg.error().message;
    const auto old_ptx =
        check(*neg, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(
        check(*neg, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
            .has_value());
  }

  PtxSyntaxParser packed_parser("neg.f16x2 %r0, %r1;");
  const auto packed_ast = packed_parser.parseInstruction();
  ASSERT_TRUE(packed_ast.has_value()) << packed_ast.diagnostics.front().message;
  const auto packed = resolve<Neg>(*packed_ast);
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  const auto old_ptx = check(
      *packed, Context{.target = {.ptx_version = {5, 9}, .sm_version = 53},
                       .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *packed, Context{.target = {.ptx_version = {6, 0}, .sm_version = 52},
                       .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*packed,
                    Context{.target = {.ptx_version = {6, 0}, .sm_version = 53},
                            .instruction_range = packed_ast->range})
                  .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
