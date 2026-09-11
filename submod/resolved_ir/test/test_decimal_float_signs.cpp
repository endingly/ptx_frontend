#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse an instruction holding an immediate without bypassing source syntax. */
SyntaxInstructionParseResult parseImmediate(std::string_view spelling) {
  PtxSyntaxParser parser("mov.f32 %f0, " + std::string(spelling) + ";");
  return parser.parseInstruction();
}

/** Return the second operand after the caller has verified the parsed instruction. */
const syntax_ast::AstImmediate& immediateOperand(
    const syntax_ast::AstInstruction& instruction) {
  return std::get<syntax_ast::AstImmediate>(instruction.operands[1]);
}

/** Return the immediate source held by a scalar move instruction. */
const ResolvedImmediate& scalarMovImmediate(const ResolvedInstruction& instruction) {
  const auto& mov = std::get<Mov>(instruction);
  const auto& scalar = std::get<Mov::Scalar>(mov.variant);
  const auto& operands = std::get<Mov::Scalar::ScalarOperands>(scalar.operands);
  return std::get<ResolvedImmediate>(operands.src.value);
}

/** Accept equivalent signs while retaining exact decimal floating bit patterns. */
TEST(DecimalFloatSigns, ResolvesLeadingSignsZeroAndExponents) {
  /** One source spelling, selected use type, and expected IEEE payload. */
  struct Fixture {
    /** Exact decimal source spelling, including its leading or exponent sign. */
    std::string_view spelling;
    /** Scalar use type that determines the retained IEEE payload width. */
    ScalarType type;
    /** Expected use-width IEEE payload bits. */
    uint64_t bits;
  };
  constexpr std::array<Fixture, 12> fixtures{{
      {"1.0", ScalarType::F32, 0x3f800000u},
      {"+1.0", ScalarType::F32, 0x3f800000u},
      {"-1.0", ScalarType::F32, 0xbf800000u},
      {"+0.0", ScalarType::F32, 0u},
      {"-0.0", ScalarType::F32, 0x80000000u},
      {"+1e+1", ScalarType::F32, 0x41200000u},
      {"1.0", ScalarType::F64, 0x3ff0000000000000ULL},
      {"+1.0", ScalarType::F64, 0x3ff0000000000000ULL},
      {"-1.0", ScalarType::F64, 0xbff0000000000000ULL},
      {"+0.0", ScalarType::F64, 0u},
      {"-0.0", ScalarType::F64, 0x8000000000000000ULL},
      {"-1e-1", ScalarType::F64, 0xbfb999999999999aULL},
  }};

  for (const auto& fixture : fixtures) {
    SCOPED_TRACE(fixture.spelling);
    const auto parsed = parseImmediate(fixture.spelling);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto& immediate = immediateOperand(*parsed);
    EXPECT_EQ(immediate.kind, syntax_ast::AstImmediateKind::DecimalFloat);
    const auto resolved = resolve_immediate_literal(immediate, fixture.type);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_EQ(resolved->bits, fixture.bits);
    EXPECT_EQ(resolved->type, fixture.type);
    EXPECT_FALSE(resolved->integer_source_bits.has_value());
  }
}

/** Keep raw floating bit-pattern sign handling independent of decimal decoding. */
TEST(DecimalFloatSigns, PreservesRawBitPatternSignRules) {
  const auto unsigned_instruction = parseImmediate("0f3f800000");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(unsigned_instruction);
  const auto unsigned_bits = resolve_immediate_literal(
      immediateOperand(*unsigned_instruction), ScalarType::F32);
  ASSERT_TRUE(unsigned_bits.has_value()) << unsigned_bits.error().message;
  EXPECT_EQ(unsigned_bits->bits, 0x3f800000u);

  const auto positive_instruction = parseImmediate("+0f3f800000");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(positive_instruction);
  const auto positive_bits = resolve_immediate_literal(
      immediateOperand(*positive_instruction), ScalarType::F32);
  ASSERT_TRUE(positive_bits.has_value()) << positive_bits.error().message;
  EXPECT_EQ(positive_bits->bits, 0x3f800000u);

  const auto negative_instruction = parseImmediate("-0f3f800000");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(negative_instruction);
  const auto negative_bits = resolve_immediate_literal(
      immediateOperand(*negative_instruction), ScalarType::F32);
  ASSERT_FALSE(negative_bits.has_value());
  EXPECT_EQ(negative_bits.error().message,
            "Floating bit-pattern literal '-0f3f800000' cannot have a sign.");
}

/** Reject malformed leading signs without changing exponent or raw-bit syntax. */
TEST(DecimalFloatSigns, RejectsMalformedLeadingSigns) {
  for (const auto spelling : {"++1.0", "+-1.0", "-+1.0", "--1.0"}) {
    SCOPED_TRACE(spelling);
    const syntax_ast::AstImmediate immediate{
        .syntax = {.text = spelling, .range = {}},
        .kind = syntax_ast::AstImmediateKind::DecimalFloat,
    };
    const auto resolved = resolve_immediate_literal(immediate, ScalarType::F32);
    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().message,
              "Invalid decimal floating literal '" + std::string(spelling) +
                  "'.");
  }
}

/** Use function formal types for signed decimal literals in source call arguments. */
TEST(DecimalFloatSigns, ResolvesSourceCallLiteralsAgainstFloatingFormals) {
  const auto module = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.func callee(.param .f32 input);
.entry caller() {
  call callee, (+1.0);
  call callee, (-0.0);
  ret;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(module);

  const auto resolved = resolveModule(*module);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const declaration_semantics::FunctionParameterContract f32{
      .scalar_type = base::ScalarType::F32,
      .type_spelling = ".f32",
  };
  const auto negative_zero_instruction = parseImmediate("-0.0");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(negative_zero_instruction);
  const auto& negative_zero = immediateOperand(*negative_zero_instruction);
  const auto literal = resolve_call_literal(
      ResolvedCallLiteral{.spelling = negative_zero.syntax.text,
                          .kind = negative_zero.kind},
      negative_zero.syntax.range, f32);
  ASSERT_TRUE(literal.has_value()) << literal.error().message;
  EXPECT_EQ(literal->value.bits, 0x80000000u);
  EXPECT_EQ(literal->value.type, ScalarType::F32);
}

/** Resolve leading-sign decimal literals through source module move instructions. */
TEST(DecimalFloatSigns, ResolvesSignedDecimalMovesInSourceModule) {
  const auto module = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.entry kernel() {
  .reg .f32 %f32;
  .reg .f64 %f64;
  mov.f32 %f32, +1.0;
  mov.f32 %f32, -0.0;
  mov.f64 %f64, +1.0;
  mov.f64 %f64, -0.0;
  ret;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(module);

  const auto resolved = resolveModule(*module);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  EXPECT_EQ(scalarMovImmediate(body[0]).bits, 0x3f800000u);
  EXPECT_EQ(scalarMovImmediate(body[1]).bits, 0x80000000u);
  EXPECT_EQ(scalarMovImmediate(body[2]).bits, 0x3ff0000000000000ULL);
  EXPECT_EQ(scalarMovImmediate(body[3]).bits, 0x8000000000000000ULL);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
