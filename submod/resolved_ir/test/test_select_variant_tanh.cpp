#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/tanh/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/tanh/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/tanh/resolution.gen.hpp>
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

TEST(ResolveTanh, SelectsFrozenFloatAndLowPrecisionVariants) {
  const auto f32 =
      resolve<Tanh>(parse_instruction("tanh.approx.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Tanh::ApproxF32>(&f32->variant), nullptr);
  EXPECT_EQ(Tanh::ApproxF32::type, ScalarType::F32);

  const auto f16 =
      resolve<Tanh>(parse_instruction("tanh.approx.f16 %h0, %h1;"));
  ASSERT_TRUE(f16.has_value()) << f16.error().message;
  ASSERT_NE(std::get_if<Tanh::ApproxF16>(&f16->variant), nullptr);
  EXPECT_EQ(Tanh::ApproxF16::type, ScalarType::F16);

  const auto f16x2 =
      resolve<Tanh>(parse_instruction("tanh.approx.f16x2 %r0, %r1;"));
  ASSERT_TRUE(f16x2.has_value()) << f16x2.error().message;
  ASSERT_NE(std::get_if<Tanh::ApproxF16x2>(&f16x2->variant), nullptr);
  EXPECT_EQ(Tanh::ApproxF16x2::type, ScalarType::F16x2);

  const auto bf16 =
      resolve<Tanh>(parse_instruction("tanh.approx.bf16 %b0, %b1;"));
  ASSERT_TRUE(bf16.has_value()) << bf16.error().message;
  ASSERT_NE(std::get_if<Tanh::ApproxBf16>(&bf16->variant), nullptr);
  EXPECT_EQ(Tanh::ApproxBf16::type, ScalarType::BF16);

  const auto bf16x2 =
      resolve<Tanh>(parse_instruction("tanh.approx.bf16x2 %r0, %r1;"));
  ASSERT_TRUE(bf16x2.has_value()) << bf16x2.error().message;
  ASSERT_NE(std::get_if<Tanh::ApproxBf16x2>(&bf16x2->variant), nullptr);
  EXPECT_EQ(Tanh::ApproxBf16x2::type, ScalarType::BF16x2);
}

TEST(ResolveTanh, RejectsInvalidForms) {
  for (const auto source :
       {"tanh.f32 %f0, %f1;", "tanh.approx.ftz.f32 %f0, %f1;",
        "tanh.approx.ftz.f16 %h0, %h1;", "tanh.approx.ftz.bf16 %b0, %b1;",
        "tanh.approx.f64 %d0, %d1;", "tanh.rz.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Tanh>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
