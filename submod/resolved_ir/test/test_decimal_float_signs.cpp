#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_module_snapshot.hpp"
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

/** Return the resolved source literal from a scalar move layout. */
const ResolvedImmediate& scalarMovImmediate(const Instruction& instruction) {
  const auto& mov = dynamic_cast<const MovScalar&>(instruction);
  return std::get<ResolvedImmediate>(mov.src_mov_source.value().value);
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

/** Trailing decimal points share f64 decoding, destination narrowing, and ownership. */
TEST(DecimalFloatSigns, ResolvesTrailingDecimalPointsInOwnedModule) {
  auto resolved = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .f32 g = 1.;
.global .f64 h = (-1.E+2);
.entry kernel() {
  .reg .f32 %f;
  .reg .f64 %d;
  mov.f32 %f, +1.;
  mov.f32 %f, -0.;
  mov.f32 %f, 1.e2;
  mov.f64 %d, -1.E-2;
  mov.f64 %d, (1. + 2.e1);
  ret;
}
)ptx";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    EXPECT_TRUE(ast.has_value());
    EXPECT_TRUE(ast.diagnostics.empty());
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->storage_declarations.size(), 2u);
  EXPECT_EQ(std::get<StorageConstant>(
                resolved->storage_declarations[0].initializer[0].value)
                .bits,
            0x3f800000u);
  EXPECT_EQ(std::get<StorageConstant>(
                resolved->storage_declarations[1].initializer[0].value)
                .bits,
            0xc059000000000000ULL);
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const std::array<uint64_t, 5> expected{0x3f800000u, 0x80000000u, 0x42c80000u,
                                         0xbf847ae147ae147bULL,
                                         0x4035000000000000ULL};
  for (size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(scalarMovImmediate(*body[i]).bits, expected[i]);
  EXPECT_TRUE(validateModule(*resolved));
}

/** Floating spellings cannot acquire integer destination or declaration semantics. */
TEST(DecimalFloatSigns, TrailingDecimalPointsRejectIntegerUses) {
  for (const std::string_view source :
       {".global .u32 g = 1.;", ".global .u64 g = 1.e2;",
        ".entry k() { .reg .u32 %r; mov.u32 %r, 1.; ret; }"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveModule(*ast).has_value());
  }
  for (const std::string_view spelling : {"1.", "1.e2", "+1.E+2", "-0."}) {
    SCOPED_TRACE(spelling);
    const auto parsed = parseImmediate(spelling);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto& immediate = immediateOperand(*parsed);
    EXPECT_EQ(immediate.kind, syntax_ast::AstImmediateKind::DecimalFloat);
    EXPECT_EQ(immediate.syntax.text, spelling);
    EXPECT_FALSE(resolve_immediate_literal(immediate, ScalarType::U32));
    EXPECT_TRUE(resolve_immediate_literal(immediate, ScalarType::F32));
    EXPECT_TRUE(resolve_immediate_literal(immediate, ScalarType::F64));
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
    EXPECT_EQ(resolved.error().message, "Invalid decimal floating literal '" +
                                            std::string(spelling) + "'.");
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

  const auto resolved = test_support::resolveModuleSnapshot(*module);

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
  EXPECT_EQ(scalarMovImmediate(*body[0]).bits, 0x3f800000u);
  EXPECT_EQ(scalarMovImmediate(*body[1]).bits, 0x80000000u);
  EXPECT_EQ(scalarMovImmediate(*body[2]).bits, 0x3ff0000000000000ULL);
  EXPECT_EQ(scalarMovImmediate(*body[3]).bits, 0x8000000000000000ULL);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
