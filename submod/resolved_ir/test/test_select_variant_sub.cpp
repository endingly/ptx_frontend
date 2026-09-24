#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/sub/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sub/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sub/resolution.gen.hpp>
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

TEST(SelectVariantSub, SelectsEveryGeneratedVariant) {
  const auto expect_variant = [](std::string_view source,
                                 Sub::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Sub>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("sub.u32 %r0, %r1, %r2;", Sub::VariantType::IntegerNoSat);
  expect_variant("sub.s32 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.sat.s32 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.u8x4 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.sat.s8x4 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.rz.ftz.sat.f32 %f0, %f1, %f2;",
                 Sub::VariantType::FloatF32);
  expect_variant("sub.rp.f32x2 %r0, %r1, %r2;", Sub::VariantType::FloatF32x2);
  expect_variant("sub.rm.f64 %fd0, %fd1, %fd2;", Sub::VariantType::FloatF64);
  expect_variant("sub.rn.ftz.sat.f16x2 %r0, %r1, %r2;", Sub::VariantType::Half);
  expect_variant("sub.bf16 %r0, %r1, %r2;", Sub::VariantType::Bfloat);
  expect_variant("sub.f32.f16 %f0, %h1, %f2;", Sub::VariantType::MixedF32);
  expect_variant("sub.rz.f32.bf16.sat %f0, %h1, %f2;",
                 Sub::VariantType::MixedF32);
}

TEST(ResolveSub, BuildsIntegerAndMixedPrecisionVariants) {
  const auto integer_ast = parse_instruction("sub.sat.s32 %r4, %r5, -1;");
  const auto integer_resolved = resolve<Sub>(integer_ast);
  ASSERT_TRUE(integer_resolved.has_value()) << integer_resolved.error().message;
  const auto* integer =
      std::get_if<Sub::OptionalSat>(&integer_resolved->variant);
  ASSERT_NE(integer, nullptr);
  EXPECT_TRUE(integer->saturate.value);
  ASSERT_EQ(integer->saturate.locs.size(), 1U);
  EXPECT_EQ(integer->type.value, ScalarType::S32);
  const auto* immediate = std::get_if<ResolvedImmediate>(&integer->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::S32);

  const auto mixed_ast =
      parse_instruction("sub.rz.f32.bf16.sat %f0, %h1, %f2;");
  const auto mixed_resolved = resolve<Sub>(mixed_ast);
  ASSERT_TRUE(mixed_resolved.has_value()) << mixed_resolved.error().message;
  const auto* mixed = std::get_if<Sub::MixedF32>(&mixed_resolved->variant);
  ASSERT_NE(mixed, nullptr);
  EXPECT_EQ(mixed->rounding.value, RoundingMode::Rz);
  EXPECT_EQ(Sub::MixedF32::result_type, ScalarType::F32);
  EXPECT_EQ(mixed->input_type.value, ScalarType::BF16);
  EXPECT_TRUE(mixed->saturate.value);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(mixed->subtrahend.value).spelling,
            "%f2");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, GeneratedSubWrapperUsesValueAvailability) {
  PtxSyntaxParser parser("sub.sat.u8x4 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolve<Sub>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Sub::OptionalSat>(&resolved->variant), nullptr);

  constexpr std::array<std::string_view, 1> family{"sm_120f"};
  const Context unsupported_context{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .enabled_family_features = family},
      .instruction_range = ast->range,
  };
  const auto unsupported = check(*resolved, unsupported_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_context{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .enabled_family_features = family},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, supported_context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
