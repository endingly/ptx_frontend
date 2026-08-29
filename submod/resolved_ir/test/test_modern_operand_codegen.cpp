#include <gtest/gtest.h>

#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "resolved_ir.gen.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

TEST(ModernOperandCodegen, ResolvesAndChecksSyntheticPrimitive) {
  PtxSyntaxParser parser("synthetic_modern {%r0, 1}, {%r1};");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto instruction = resolve<SyntheticModern>(*ast);
  ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
  const auto& coordinate_syntax =
      std::get<syntax_ast::AstVectorPack>(ast->operands[0]);
  const auto& fragment_syntax =
      std::get<syntax_ast::AstVectorPack>(ast->operands[1]);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(checker::check(*instruction, context).has_value());

  auto& primitive = std::get<SyntheticModern::Primitive>(instruction->variant);
  ASSERT_EQ(primitive.coordinate.locs.size(), 2u);
  EXPECT_EQ(primitive.coordinate.locs[0],
            std::get<syntax_ast::AstIdentifierRef>(coordinate_syntax.elements[0])
                .syntax.range);
  EXPECT_EQ(primitive.coordinate.locs[1],
            std::get<syntax_ast::AstImmediate>(coordinate_syntax.elements[1])
                .syntax.range);
  ASSERT_EQ(primitive.fragment.locs.size(), 1u);
  EXPECT_EQ(primitive.fragment.locs[0],
            std::get<syntax_ast::AstIdentifierRef>(fragment_syntax.elements[0])
                .syntax.range);
  primitive.coordinate.value.elements.clear();
  const auto rejected = checker::check(*instruction, context);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
