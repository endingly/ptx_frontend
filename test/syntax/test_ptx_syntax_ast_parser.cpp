#include <gtest/gtest.h>

#include <string_view>
#include <variant>

#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend {
namespace {

using syntax_ast::AstAddress;
using syntax_ast::AstAddressOffset;
using syntax_ast::AstIdentifierRef;
using syntax_ast::AstImmediate;
using syntax_ast::AstPredicateOperand;
using syntax_ast::AstVectorMember;
using syntax_ast::AstVectorPack;

TEST(PtxSyntaxParser, ParsesInstructionWithoutCstTrivia) {
  PtxSyntaxParser parser("  // leading\nadd.sat.s32 %r1, %r2, -1;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  const auto& instruction = *result;
  EXPECT_EQ(instruction.opcode.syntax.text, "add");

  ASSERT_EQ(instruction.modifiers.size(), 2u);
  EXPECT_EQ(instruction.modifiers[0].syntax.text, ".sat");
  EXPECT_EQ(instruction.modifiers[1].syntax.text, ".s32");

  ASSERT_EQ(instruction.operands.size(), 3u);
  EXPECT_TRUE(
      std::holds_alternative<AstIdentifierRef>(instruction.operands[0]));
  EXPECT_TRUE(
      std::holds_alternative<AstIdentifierRef>(instruction.operands[1]));
  ASSERT_TRUE(std::holds_alternative<AstImmediate>(instruction.operands[2]));
  EXPECT_EQ(std::get<AstImmediate>(instruction.operands[2]).syntax.text, "-1");
}

TEST(PtxSyntaxParser, ParsesPredicateAddressAndVectorMember) {
  PtxSyntaxParser parser("@!%p add.u32 [%rd1+16], %r2.x, %r3;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  const auto& instruction = *result;
  ASSERT_TRUE(instruction.predicate.has_value());
  EXPECT_TRUE(instruction.predicate->negated);
  EXPECT_EQ(instruction.predicate->name.syntax.text, "%p");

  ASSERT_EQ(instruction.operands.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<AstAddress>(instruction.operands[0]));
  const auto& address = std::get<AstAddress>(instruction.operands[0]);
  EXPECT_TRUE(address.bracketed);
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(address.offset->operation, AstAddressOffset::Operator::Add);
  EXPECT_EQ(address.offset->magnitude.syntax.text, "16");

  ASSERT_TRUE(std::holds_alternative<AstVectorMember>(instruction.operands[1]));
  const auto& member = std::get<AstVectorMember>(instruction.operands[1]);
  EXPECT_EQ(member.base.syntax.text, "%r2");
  EXPECT_EQ(member.selector.text, ".x");
}

TEST(PtxSyntaxParser, ParsesVectorPack) {
  PtxSyntaxParser parser("mov.b32 {%r1, -2, 0x10}, %r2;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  ASSERT_EQ(result->operands.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<AstVectorPack>(result->operands[0]));
  const auto& pack = std::get<AstVectorPack>(result->operands[0]);
  ASSERT_EQ(pack.elements.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<AstIdentifierRef>(pack.elements[0]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[1]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[2]));
}

TEST(PtxSyntaxParser, LowersUnbracketedAddressOffsetOperation) {
  PtxSyntaxParser parser("ld.u32 %r1, %rd1-4;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  ASSERT_EQ(result->operands.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<AstAddress>(result->operands[1]));
  const auto& address = std::get<AstAddress>(result->operands[1]);
  EXPECT_FALSE(address.bracketed);
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(address.offset->operation, AstAddressOffset::Operator::Subtract);
  EXPECT_EQ(address.offset->magnitude.syntax.text, "4");
}

TEST(PtxSyntaxParser, ParsesNegatedPredicateOperand) {
  PtxSyntaxParser parser("bar.red.popc.u32 %r0, 1, !%p1;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  ASSERT_EQ(result->operands.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<AstPredicateOperand>(result->operands[2]));
  const auto& predicate = std::get<AstPredicateOperand>(result->operands[2]);
  EXPECT_TRUE(predicate.negated);
  EXPECT_EQ(predicate.name.syntax.text, "%p1");
}

TEST(PtxSyntaxParser, RejectsMissingInstructionTerminator) {
  PtxSyntaxParser parser("add.u32 %r1, %r2, %r3");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message, "expected ';'");
}

TEST(PtxSyntaxParser, RejectsEmptyVectorPack) {
  PtxSyntaxParser parser("mov.b32 {}, %r1;");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message, "vector operand cannot be empty");
}

TEST(AstFile, DistinguishesInstructionFragmentAndModuleRoots) {
  syntax_ast::AstFile module_file{syntax_ast::AstModule{}};

  EXPECT_EQ(module_file.instruction(), nullptr);
  ASSERT_NE(module_file.module(), nullptr);
  EXPECT_TRUE(module_file.module()->items.empty());
}

TEST(SyntaxLower, RejectsModuleRootAsInstructionFragment) {
  syntax_cst::CstFile module_file{
      .tokens = {},
      .root = syntax_cst::CstModule{.items = {}, .token_range = {}},
  };

  const auto result = lowerSyntaxInstruction(module_file);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message,
            "expected an instruction-fragment CST root");
}

TEST(PtxSyntaxParser, LowersMinimalModuleToTypedAst) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80, debug
.address_size 64
.visible .entry kernel() {
  add.u32 %r0, %r1, %r2;
}
)ptx";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->items.size(), 4u);
  const auto& version =
      std::get<syntax_ast::AstVersionDirective>(result->items[0]);
  EXPECT_EQ(version.version.text, "8.0");

  const auto& target =
      std::get<syntax_ast::AstTargetDirective>(result->items[1]);
  ASSERT_EQ(target.targets.size(), 2u);
  EXPECT_EQ(target.targets[0].text, "sm_80");
  EXPECT_EQ(target.targets[1].text, "debug");

  const auto& address_size =
      std::get<syntax_ast::AstAddressSizeDirective>(result->items[2]);
  EXPECT_EQ(address_size.bit_width.text, "64");

  const auto& function = std::get<syntax_ast::AstFunction>(result->items[3]);
  EXPECT_TRUE(function.is_entry);
  EXPECT_EQ(function.name.syntax.text, "kernel");
  ASSERT_EQ(function.qualifiers.size(), 1u);
  EXPECT_EQ(function.qualifiers[0].text, ".visible");
  ASSERT_EQ(function.body.size(), 1u);
  EXPECT_EQ(
      std::get<syntax_ast::AstInstruction>(function.body[0]).opcode.syntax.text,
      "add");
}

TEST(PtxSyntaxParser, LowersFuncDefinition) {
  PtxSyntaxParser parser(
      ".func (.param .b32 result) helper(.param .b32 input) { ret; }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->items.size(), 1u);
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  EXPECT_FALSE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(function.name.syntax.text, "helper");
  ASSERT_EQ(function.return_parameters.size(), 1u);
  EXPECT_EQ(function.return_parameters[0].state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(function.return_parameters[0].name.syntax.text, "result");
  ASSERT_EQ(function.parameters.size(), 1u);
  EXPECT_EQ(function.parameters[0].name.syntax.text, "input");
  ASSERT_EQ(function.body.size(), 1u);
  EXPECT_EQ(
      std::get<syntax_ast::AstInstruction>(function.body[0]).opcode.syntax.text,
      "ret");
}

TEST(PtxSyntaxParser, LowersParameterAttributesArraysAndPrototype) {
  PtxSyntaxParser parser(
      ".extern .func sink(.param .align 8 .b8 blob[12], "
      ".param .u64 .ptr .global .align 16 ptr) .noreturn;");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  EXPECT_TRUE(function.is_prototype);
  EXPECT_TRUE(function.is_noreturn);
  ASSERT_EQ(function.qualifiers.size(), 1u);
  EXPECT_EQ(function.qualifiers[0].text, ".extern");
  ASSERT_EQ(function.parameters.size(), 2u);

  const auto& blob = function.parameters[0];
  EXPECT_EQ(blob.state_space, syntax_ast::AstStateSpace::Parameter);
  ASSERT_TRUE(blob.alignment.has_value());
  EXPECT_EQ(blob.alignment->text, "8");
  EXPECT_TRUE(blob.is_array);
  ASSERT_TRUE(blob.array_size.has_value());
  EXPECT_EQ(blob.array_size->text, "12");

  const auto& pointer = function.parameters[1];
  EXPECT_TRUE(pointer.is_pointer);
  ASSERT_TRUE(pointer.pointer_space.has_value());
  EXPECT_EQ(pointer.pointer_space->text, ".global");
  ASSERT_TRUE(pointer.pointer_alignment.has_value());
  EXPECT_EQ(pointer.pointer_alignment->text, "16");
  EXPECT_TRUE(function.body.empty());
}

TEST(PtxSyntaxParser, LowersRegisterDeclarationsAndLabels) {
  PtxSyntaxParser parser(
      ".entry kernel() { .reg .align 16 .u32 %r<3>, %tmp; loop: ret; }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& function = std::get<syntax_ast::AstFunction>(result->items[0]);
  ASSERT_EQ(function.body.size(), 3u);

  const auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(function.body[0]);
  ASSERT_TRUE(declaration.alignment.has_value());
  EXPECT_EQ(declaration.alignment->text, "16");
  EXPECT_EQ(declaration.type.text, ".u32");
  ASSERT_EQ(declaration.declarators.size(), 2u);
  EXPECT_EQ(declaration.declarators[0].name.syntax.text, "%r");
  ASSERT_TRUE(declaration.declarators[0].register_count.has_value());
  EXPECT_EQ(declaration.declarators[0].register_count->text, "3");
  EXPECT_EQ(declaration.declarators[1].name.syntax.text, "%tmp");

  const auto& label = std::get<syntax_ast::AstLabel>(function.body[1]);
  EXPECT_EQ(label.name.syntax.text, "loop");
  EXPECT_TRUE(
      std::holds_alternative<syntax_ast::AstInstruction>(function.body[2]));
}

TEST(PtxSyntaxParser, LowersModuleAndFunctionVariableDeclarations) {
  constexpr std::string_view source =
      ".visible .global .align 16 .v4 .f32 values[2][3];\n"
      ".entry kernel() {\n"
      "  .shared .v2 .u16 tile[8];\n"
      "  .local .u32 scratch[19][19];\n"
      "  .param .align 8 .b8 argument[12];\n"
      "  ret;\n"
      "}";
  PtxSyntaxParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->items.size(), 2u);
  const auto& global =
      std::get<syntax_ast::AstVariableDeclaration>(result->items[0]);
  EXPECT_EQ(global.state_space, syntax_ast::AstStateSpace::Global);
  ASSERT_EQ(global.qualifiers.size(), 1u);
  EXPECT_EQ(global.qualifiers[0].text, ".visible");
  ASSERT_TRUE(global.alignment.has_value());
  EXPECT_EQ(global.alignment->text, "16");
  ASSERT_TRUE(global.vector_type.has_value());
  EXPECT_EQ(global.vector_type->text, ".v4");
  ASSERT_EQ(global.declarators[0].array_dimensions.size(), 2u);
  EXPECT_EQ(global.declarators[0].array_dimensions[1].size_tokens[0].text, "3");

  const auto& function = std::get<syntax_ast::AstFunction>(result->items[1]);
  ASSERT_EQ(function.body.size(), 4u);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[0])
                .state_space,
            syntax_ast::AstStateSpace::Shared);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[1])
                .state_space,
            syntax_ast::AstStateSpace::Local);
  EXPECT_EQ(std::get<syntax_ast::AstVariableDeclaration>(function.body[2])
                .state_space,
            syntax_ast::AstStateSpace::Parameter);
}

}  // namespace
}  // namespace ptx_frontend
