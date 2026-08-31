#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
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

std::string syntheticModule(std::string_view coordinate,
                            std::string_view fragment) {
  std::string source = R"ptx(
.version 9.3
.entry kernel() {
  .reg .u32 %r<64>;
  .reg .f32 %f<64>;
  synthetic_modern {)ptx";
  source.append(coordinate);
  source.append("}, {");
  source.append(fragment);
  source.append("};\n}\n");
  return source;
}

std::string fragmentPack(size_t wrong_index = 64) {
  std::string pack;
  for (size_t index = 0; index < 64; ++index) {
    if (!pack.empty())
      pack.append(", ");
    pack.append(index == wrong_index ? "%f" : "%r");
    pack.append(std::to_string(index));
  }
  return pack;
}

const SyntheticModern& syntheticInstruction(const ResolvedModule& module) {
  return std::get<SyntheticModern>(module.functions.front().body.front());
}

const SyntheticModern::Primitive& syntheticPrimitive(
    const SyntheticModern& instruction) {
  return std::get<SyntheticModern::Primitive>(instruction.variant);
}

checker::Context syntheticContext(const SyntheticModern::Primitive& primitive) {
  return {
      .target = {.ptx_version = {9, 3}, .sm_version = 0},
      .instruction_range = primitive.coordinate.locs.front(),
  };
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

TEST(ModernOperandCodegen, ChecksModuleBoundCoordinateElementTypes) {
  const auto wrong = resolveModule(
      parseModule(syntheticModule("%f0", "%r0")));
  ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
  const SyntheticModern& wrong_instruction = syntheticInstruction(*wrong);
  const auto& wrong_primitive = syntheticPrimitive(wrong_instruction);
  const auto rejected =
      checker::check(wrong_instruction, syntheticContext(wrong_primitive));
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(rejected.error().front().range, wrong_primitive.coordinate.locs[0]);

  const auto correct = resolveModule(
      parseModule(syntheticModule("%r0, 1", "%r0")));
  ASSERT_TRUE(correct.has_value()) << correct.error().front().message;
  const SyntheticModern& correct_instruction = syntheticInstruction(*correct);
  const auto& correct_primitive = syntheticPrimitive(correct_instruction);
  EXPECT_TRUE(checker::check(correct_instruction,
                             syntheticContext(correct_primitive))
                  .has_value());
}

TEST(ModernOperandCodegen, ChecksAllModernFragmentElementTypes) {
  for (const size_t wrong_index : {size_t{0}, size_t{8}, size_t{63}}) {
    const auto wrong = resolveModule(parseModule(
        syntheticModule("%r0", fragmentPack(wrong_index))));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const SyntheticModern& instruction = syntheticInstruction(*wrong);
    const auto& primitive = syntheticPrimitive(instruction);
    const auto rejected = checker::check(instruction, syntheticContext(primitive));
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(rejected.error().size(), 1u);
    EXPECT_EQ(rejected.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
    EXPECT_EQ(rejected.error().front().range,
              primitive.fragment.locs[wrong_index]);
  }

  const auto correct = resolveModule(
      parseModule(syntheticModule("%r0", fragmentPack())));
  ASSERT_TRUE(correct.has_value()) << correct.error().front().message;
  const SyntheticModern& instruction = syntheticInstruction(*correct);
  const auto& primitive = syntheticPrimitive(instruction);
  EXPECT_TRUE(checker::check(instruction, syntheticContext(primitive)).has_value());
}

TEST(ModernOperandCodegen, ChecksImmediateRangeInEachOperandLayout) {
  for (const std::string_view source : {
           "synthetic_immediate_layout 7;",
           "synthetic_immediate_layout %r0, 7;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto instruction = resolve<SyntheticImmediateLayout>(*ast);
    ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 0},
        .instruction_range = ast->range,
    };
    EXPECT_TRUE(checker::check(*instruction, context).has_value()) << source;
  }

  for (const std::string_view source : {
           "synthetic_immediate_layout 8;",
           "synthetic_immediate_layout %r0, 8;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto instruction = resolve<SyntheticImmediateLayout>(*ast);
    ASSERT_TRUE(instruction.has_value()) << instruction.error().message;
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 0},
        .instruction_range = ast->range,
    };
    const auto rejected = checker::check(*instruction, context);
    ASSERT_FALSE(rejected.has_value()) << source;
    ASSERT_EQ(rejected.error().size(), 1u);
    EXPECT_EQ(rejected.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

TEST(ModernOperandCodegen, AppliesExactTargetsThroughModuleAvailability) {
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
            checker::CheckDiagnosticKind::UnsupportedAvailability);

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

  const auto sm100f = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_100f
.entry kernel() { synthetic_sm100a; }
)ptx"), *sm100a_module);
  ASSERT_FALSE(sm100f.has_value());
  ASSERT_EQ(sm100f.error().size(), 1u);
  EXPECT_EQ(sm100f.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
}

TEST(ModernOperandCodegen, AppliesEnabledFamilyFeaturesThroughModuleAvailability) {
  const auto module = resolveModule(parseModule(R"ptx(
.version 9.3
.entry kernel() { synthetic_sm100f_feature; }
)ptx"));
  ASSERT_TRUE(module.has_value()) << module.error().front().message;

  const auto check_for_target = [&module](std::string_view target) {
    std::string source = ".version 9.3\n.target ";
    source.append(target);
    source.append("\n.entry kernel() { synthetic_sm100f_feature; }\n");
    return checkModuleAvailability(parseModule(source), *module);
  };

  EXPECT_TRUE(check_for_target("sm_100a").has_value());
  EXPECT_TRUE(check_for_target("sm_100f").has_value());
  EXPECT_TRUE(check_for_target("sm_103a").has_value());
  EXPECT_TRUE(check_for_target("sm_103f").has_value());

  for (const std::string_view target : {"sm_100", "sm_103", "sm_90a", "sm_120f"}) {
    const auto rejected = check_for_target(target);
    ASSERT_FALSE(rejected.has_value()) << target;
    EXPECT_TRUE(std::ranges::any_of(
        rejected.error(), [](const checker::CheckDiagnostic& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::UnsupportedTargetFamily;
        })) << target;
  }
}

TEST(ModernOperandCodegen,
     AppliesSm103fFamilyFeaturesThroughModuleAvailability) {
  const auto module = resolveModule(parseModule(R"ptx(
.version 9.3
.entry kernel() { synthetic_sm103f_feature; }
)ptx"));
  ASSERT_TRUE(module.has_value()) << module.error().front().message;

  const auto check_for_target = [&module](std::string_view target) {
    std::string source = ".version 9.3\n.target ";
    source.append(target);
    source.append("\n.entry kernel() { synthetic_sm103f_feature; }\n");
    return checkModuleAvailability(parseModule(source), *module);
  };

  for (const std::string_view target : {"sm_103a", "sm_103f"})
    EXPECT_TRUE(check_for_target(target).has_value()) << target;

  for (const std::string_view target :
       {"sm_100a", "sm_100f", "sm_103", "sm_120f"}) {
    const auto rejected = check_for_target(target);
    ASSERT_FALSE(rejected.has_value()) << target;
    EXPECT_TRUE(std::ranges::any_of(
        rejected.error(), [](const checker::CheckDiagnostic& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::UnsupportedTargetFamily;
        })) << target;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
