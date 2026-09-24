#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/sin/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sin/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sin/checker.gen.hpp>
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

TEST(ResolveSin, SelectsFrozenApproxVariant) {
  const auto resolved =
      resolve<Sin>(parse_instruction("sin.approx.ftz.f32 %f0, %f1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Sin::ApproxF32>(&resolved->variant), nullptr);
  EXPECT_EQ(Sin::ApproxF32::type, ScalarType::F32);
  EXPECT_TRUE(Sin::ApproxF32::approx);
  EXPECT_TRUE(std::get<Sin::ApproxF32>(resolved->variant).ftz.value);
}

TEST(ResolveSin, RejectsInvalidForms) {
  for (const auto source :
       {"sin.f32 %f0, %f1;", "sin.approx.f64 %d0, %d1;",
        "sin.approx.f32x2 %f0, %f1;", "sin.approx.sat.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Sin>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
