#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "ptx_parser.hpp"
#include "ptx_parser_core.hpp"

namespace ptx_frontend {
namespace {

using ParsedAdd = generated::InstrIntegerAdd<ParsedOp>;

class ValidParserSyntaxTest : public testing::TestWithParam<std::string_view> {
};

TEST_P(ValidParserSyntaxTest, Parses) {
  auto result = parseInstruction(GetParam());
  ASSERT_TRUE(result.has_value()) << result.error().message;
}

INSTANTIATE_TEST_SUITE_P(IntegerAddVariants, ValidParserSyntaxTest,
                         testing::Values("add.u32 %r1, %r2, %r3;",
                                         "add.sat.s32 %r1, %r2, %r3;",
                                         "add.u16x2 %r1, %r2, %r3;",
                                         "add.sat.u8x4 %r1, %r2, %r3;",
                                         "add.sat.u32 %r1, %r2, %r3;"));

class InvalidParserSyntaxTest
    : public testing::TestWithParam<std::string_view> {};

TEST_P(InvalidParserSyntaxTest, Rejects) {
  EXPECT_FALSE(parseInstruction(GetParam()).has_value());
}

INSTANTIATE_TEST_SUITE_P(OperandAndModifierConstraints, InvalidParserSyntaxTest,
                         testing::Values("add.u32 1, %r2, %r3;",
                                         "add.u32.sat %r1, %r2, %r3;",
                                         "add.sat.sat.s32 %r1, %r2, %r3;"));

const ParsedAdd& requireAdd(const ParsedInstruction& instruction) {
  return std::get<ParsedAdd>(instruction);
}

const ImmediateValue& requireImmediate(const WithLoc<ParsedOp>& operand) {
  return std::get<ImmediateValue>(operand.value.value);
}

TEST(PtxParserOperands, RejectsImmediateRegisterDestination) {
  auto result = parseInstruction("add.u32 1, %r2, %r3;");

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message, "expected identifier operand");
}

TEST(PtxParserOperands, ParsesNegativeIntegerImmediate) {
  auto result = parseInstruction("add.s32 %r1, %r2, -1;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& immediate = requireImmediate(requireAdd(*result).src2);
  EXPECT_EQ(std::get<int64_t>(immediate.value), -1);
}

TEST(PtxParserOperands, ParsesDecimalFloatingImmediate) {
  auto result = parseInstruction("add.s32 %r1, %r2, -1.5;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& immediate = requireImmediate(requireAdd(*result).src2);
  EXPECT_DOUBLE_EQ(std::get<double>(immediate.value), -1.5);
}

TEST(PtxParserOperands, ParsesFloatingBitPatternImmediate) {
  auto result = parseInstruction("add.s32 %r1, %r2, 0f3f800000;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& immediate = requireImmediate(requireAdd(*result).src2);
  EXPECT_FLOAT_EQ(std::get<float>(immediate.value), 1.0F);
}

TEST(PtxParserOperands, ParsesDoubleBitPatternImmediate) {
  auto result = parseInstruction("add.s32 %r1, %r2, 0d3ff0000000000000;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& immediate = requireImmediate(requireAdd(*result).src2);
  EXPECT_DOUBLE_EQ(std::get<double>(immediate.value), 1.0);
}

TEST(PtxParserOperands, ParsesNegativeHexIntegerImmediate) {
  auto result = parseInstruction("add.s32 %r1, %r2, -0x10;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& immediate = requireImmediate(requireAdd(*result).src2);
  EXPECT_EQ(std::get<int64_t>(immediate.value), -16);
}

TEST(PtxParserOperands, ParsesVectorMember) {
  auto result = parseInstruction("add.u32 %r1, %r2.x, %r3;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& member =
      std::get<ParsedOp::VecMemberIdx>(requireAdd(*result).src1.value.value);
  EXPECT_EQ(member.base, "%r2");
  EXPECT_EQ(member.member, 0);
}

TEST(ParserCoreOperands, ParsesVectorPack) {
  ParserCore parser("{%r1, -2, 0f3f800000}");
  auto parsed = parser.parseOperandWithLoc(OperandKind::Vector);
  const auto& elements = std::get<ParsedOp::VecPack>(parsed.value.value);

  ASSERT_EQ(elements.size(), 3);
  EXPECT_EQ(std::get<Ident>(elements[0].value), "%r1");
  EXPECT_EQ(
      std::get<int64_t>(std::get<ImmediateValue>(elements[1].value).value), -2);
  EXPECT_FLOAT_EQ(
      std::get<float>(std::get<ImmediateValue>(elements[2].value).value), 1.0F);
}

TEST(ParserCoreOperands, ParsesAddressOffset) {
  ParserCore parser("[%rd1-16]");
  auto parsed = parser.parseOperandWithLoc(OperandKind::Address);
  const auto& address = std::get<ParsedOp::RegOffset>(parsed.value.value);

  EXPECT_EQ(address.base, "%rd1");
  EXPECT_EQ(address.offset, -16);
}

TEST(PtxParserModifiers, RejectsModifiersInWrongOrder) {
  auto result = parseInstruction("add.u32.sat %r1, %r2, %r3;");

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("no supported syntax variant"),
            std::string::npos);
}

TEST(PtxParserAvailability, SyntaxOnlyModeAcceptsAllKnownVariants) {
  auto result = parseInstruction("add.u8x4 %r1, %r2, %r3;");

  ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST(PtxParserAvailability, RejectsVariantForOlderPtxTarget) {
  ParserOptions options{
      .target = {.ptx = PtxVersion{8, 0}, .sm = 120, .family = "sm_120f"},
  };
  auto result = parseInstruction("add.u8x4 %r1, %r2, %r3;", std::move(options));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message,
            "PTX instruction variant requires PTX 9.2 or newer");
}

TEST(PtxParserAvailability, RejectsVariantForOlderSmTarget) {
  ParserOptions options{
      .target = {.ptx = PtxVersion{9, 2}, .sm = 90, .family = "sm_120f"},
  };
  auto result = parseInstruction("add.u8x4 %r1, %r2, %r3;", std::move(options));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message,
            "PTX instruction variant requires sm_120 or newer");
}

TEST(PtxParserAvailability, AcceptsVariantForMatchingTarget) {
  ParserOptions options{
      .target = {.ptx = PtxVersion{9, 2}, .sm = 120, .family = "sm_120f"},
  };
  auto result = parseInstruction("add.u8x4 %r1, %r2, %r3;", std::move(options));

  ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST(PtxParserAvailability, RejectsVariantForDifferentTargetFamily) {
  ParserOptions options{
      .target = {.ptx = PtxVersion{9, 2}, .sm = 120, .family = "sm_120a"},
  };
  auto result = parseInstruction("add.u8x4 %r1, %r2, %r3;", std::move(options));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message,
            "PTX instruction variant requires target family 'sm_120f'");
}

}  // namespace
}  // namespace ptx_frontend
