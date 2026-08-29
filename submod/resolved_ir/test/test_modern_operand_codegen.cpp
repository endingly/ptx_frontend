#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "resolved_ir.gen.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.diagnostics.front().message;
  return std::move(*module);
}

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

TEST(ModernOperandCodegen, AppliesLegacyFamiliesThroughModuleAvailability) {
  const auto sm90a_module = resolveModule(parseModule(R"ptx(
.version 8.0
.entry kernel() { synthetic_sm90a; }
)ptx"));
  ASSERT_TRUE(sm90a_module.has_value())
      << sm90a_module.error().front().message;

  const auto sm90a = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_90a
.entry kernel() { synthetic_sm90a; }
)ptx"), *sm90a_module);
  EXPECT_TRUE(sm90a.has_value());

  const auto sm90 = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_90
.entry kernel() { synthetic_sm90a; }
)ptx"), *sm90a_module);
  ASSERT_FALSE(sm90.has_value());
  ASSERT_EQ(sm90.error().size(), 1u);
  EXPECT_EQ(sm90.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedTargetFamily);

  const auto sm100a_module = resolveModule(parseModule(R"ptx(
.version 8.0
.entry kernel() { synthetic_sm100a; }
)ptx"));
  ASSERT_TRUE(sm100a_module.has_value())
      << sm100a_module.error().front().message;

  const auto sm100a = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_100a
.entry kernel() { synthetic_sm100a; }
)ptx"), *sm100a_module);
  EXPECT_TRUE(sm100a.has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
