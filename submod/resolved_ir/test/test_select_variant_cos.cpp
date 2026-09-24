#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/cos/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/cos/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/cos/checker.gen.hpp>
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

TEST(ResolveCos, SelectsFrozenApproxVariant) {
  const auto resolved =
      resolve<Cos>(parse_instruction("cos.approx.f32 %f0, %f1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Cos::ApproxF32>(&resolved->variant), nullptr);
  EXPECT_EQ(Cos::ApproxF32::type, ScalarType::F32);
  EXPECT_TRUE(Cos::ApproxF32::approx);
}

TEST(ResolveCos, RejectsInvalidForms) {
  for (const auto source :
       {"cos.f32 %f0, %f1;", "cos.approx.f64 %d0, %d1;",
        "cos.approx.f32x2 %f0, %f1;", "cos.rz.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Cos>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
