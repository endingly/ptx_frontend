#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/lg2.gen.hpp>
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

TEST(ResolveLg2, SelectsFrozenApproxVariant) {
  const auto resolved =
      resolveLg2(parse_instruction("lg2.approx.ftz.f32 %f0, %f1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(dynamic_cast<Lg2ApproxF32*>(resolved->get()),
            nullptr);
  EXPECT_EQ(Lg2ApproxF32::type, ScalarType::F32);
  EXPECT_TRUE(Lg2ApproxF32::approx);
}

TEST(ResolveLg2, RejectsInvalidForms) {
  for (const auto source :
       {"lg2.f32 %f0, %f1;", "lg2.approx.f64 %d0, %d1;",
        "lg2.approx.f32x2 %f0, %f1;", "lg2.approx.sat.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source), lg2_syntax_descriptor()).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
