#include <gtest/gtest.h>

#include <string_view>
#include <variant>

#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend {
namespace {

using syntax_ast::AstAddress;
using syntax_ast::AstIdentifierRef;
using syntax_ast::AstImmediate;
using syntax_ast::AstVectorMember;
using syntax_ast::AstVectorPack;

TEST(PtxSyntaxParser, ParsesInstructionAndPreservesLeadingTrivia) {
  PtxSyntaxParser parser("  // leading\nadd.sat.s32 %r1, %r2, -1;");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

  const auto& instruction = *result;
  EXPECT_EQ(instruction.opcode.syntax.text, "add");
  ASSERT_EQ(instruction.opcode.syntax.leading_trivia.size(), 3u);
  EXPECT_EQ(instruction.opcode.syntax.leading_trivia[0].kind,
            TriviaKind::Whitespace);
  EXPECT_EQ(instruction.opcode.syntax.leading_trivia[1].kind,
            TriviaKind::LineComment);
  EXPECT_EQ(instruction.opcode.syntax.leading_trivia[2].kind,
            TriviaKind::Whitespace);

  ASSERT_EQ(instruction.modifiers.size(), 2u);
  EXPECT_EQ(instruction.modifiers[0].syntax.text, ".sat");
  EXPECT_EQ(instruction.modifiers[1].syntax.text, ".s32");

  ASSERT_EQ(instruction.operands.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<AstIdentifierRef>(instruction.operands[0]));
  EXPECT_TRUE(std::holds_alternative<AstIdentifierRef>(instruction.operands[1]));
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
  EXPECT_EQ(address.syntax.text, "[%rd1+16]");
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(address.offset->operator_token.text, "+");
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
  EXPECT_EQ(pack.syntax.text, "{%r1,-2,0x10}");
  ASSERT_EQ(pack.elements.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<AstIdentifierRef>(pack.elements[0]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[1]));
  EXPECT_TRUE(std::holds_alternative<AstImmediate>(pack.elements[2]));
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

}  // namespace
}  // namespace ptx_frontend
