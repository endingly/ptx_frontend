#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/and.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/xor.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "ptx_resolved_ir_private.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a complete source and release text/parser/AST before returning owned IR. */
std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveBitConstantSource(std::string body, bool check_source = true) {
  const std::string source =
      ".version 9.3\n.target sm_90\n.address_size 64\n" + body;
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return check_source ? resolveAndValidateModule(*ast)
                      : resolveModuleOnly(*ast);
}

/** One lexical floating precision and its independent binary payload expectation. */
struct FloatingBitFixture {
  /** Written literal or expression; operators evaluate in binary64. */
  std::string_view source;
  /** Bit carrier suffix matching the lexical or evaluated source precision. */
  std::string_view matching_type;
  /** Independently expected payload, without integer-source provenance. */
  uint64_t bits;
};

/** Fundamental bit sizes all participate in the equal-size source precision test. */
constexpr std::array<std::string_view, 5> kBitTypes{"b8", "b16", "b32", "b64",
                                                    "b128"};

/** Storage accepts matching source precision and preserves payload after AST release. */
TEST(FloatingBitConstants, StoragePrecisionMatrixAndOwnedBits) {
  constexpr std::array fixtures{
      FloatingBitFixture{"0f3f800000", "b32", 0x3f800000},
      FloatingBitFixture{"0f80000000", "b32", 0x80000000},
      FloatingBitFixture{"0f7fc01234", "b32", 0x7fc01234},
      FloatingBitFixture{"1.0", "b64", 0x3ff0000000000000},
      FloatingBitFixture{"-0.0", "b64", 0x8000000000000000},
      FloatingBitFixture{"0d7ff8000000001234", "b64", 0x7ff8000000001234},
      FloatingBitFixture{"(1.0 + 2.0) * 3.0", "b64", 0x4022000000000000},
      FloatingBitFixture{"(0 ? 2 : -0.0)", "b64", 0x8000000000000000}};
  for (const auto& fixture : fixtures) {
    for (const auto type : kBitTypes) {
      for (const auto source_check : {true, false}) {
        SCOPED_TRACE(std::string(type) + " " + std::string(fixture.source));
        const auto module = resolveBitConstantSource(
            ".global ." + std::string(type) +
                " object = " + std::string(fixture.source) + ";",
            source_check);
        ASSERT_EQ(module.has_value(), type == fixture.matching_type);
        if (!module)
          continue;
        const auto& values = module->storage_declarations.front().initializer;
        ASSERT_EQ(values.size(), 1u);
        const auto& value = std::get<StorageConstant>(values.front().value);
        EXPECT_EQ(value.bits, fixture.bits);
        EXPECT_EQ(value.high_bits, 0u);
        EXPECT_TRUE(validateModule(*module));
      }
    }
  }
}

/** Literal precision selects the bit width while ordinary float conversion remains valid. */
TEST(FloatingBitConstants,
     LiteralConversionPrecisionMatrixAndFloatingControls) {
  for (const auto& [source, expected_type] :
       {std::pair{"0f3f800000", ScalarType::B32},
        std::pair{"0d3ff0000000000000", ScalarType::B64},
        std::pair{"1.0", ScalarType::B64}}) {
    PtxSyntaxParser parser("mov.f64 %d, " + std::string(source) + ";");
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto& literal = std::get<syntax_ast::AstImmediate>(ast->operands[1]);
    for (const auto destination :
         {ScalarType::B8, ScalarType::B16, ScalarType::B32, ScalarType::B64,
          ScalarType::B128}) {
      const auto value = resolve_immediate_literal(literal, destination);
      ASSERT_EQ(value.has_value(), destination == expected_type);
      if (value) {
        EXPECT_EQ(value->type, destination);
        EXPECT_EQ(value->bits, destination == ScalarType::B32
                                   ? 0x3f800000
                                   : 0x3ff0000000000000);
        EXPECT_FALSE(value->integer_source_bits);
      }
    }
    for (const auto destination : {ScalarType::F32, ScalarType::F64}) {
      const auto value = resolve_immediate_literal(literal, destination);
      ASSERT_TRUE(value.has_value()) << value.error().message;
      EXPECT_EQ(value->bits, destination == ScalarType::F32
                                 ? 0x3f800000
                                 : 0x3ff0000000000000);
    }
  }
}

/** MOV and ordinary bit-data operators share conversion without changing written types. */
TEST(FloatingBitConstants,
     ScalarInstructionSourcesSharePrecisionAndPayloadRules) {
  constexpr std::array fixtures{
      FloatingBitFixture{"0f7fc01234", "b32", 0x7fc01234},
      FloatingBitFixture{"0f80000000", "b32", 0x80000000},
      FloatingBitFixture{"1.0", "b64", 0x3ff0000000000000},
      FloatingBitFixture{"0d7ff8000000001234", "b64", 0x7ff8000000001234},
      FloatingBitFixture{"(-0.0)", "b64", 0x8000000000000000},
      FloatingBitFixture{"((1.0 + 2.0) * 3.0)", "b64", 0x4022000000000000}};
  for (const auto& fixture : fixtures) {
    for (const auto type : {std::string_view("b32"), std::string_view("b64")}) {
      SCOPED_TRACE(std::string(type) + " " + std::string(fixture.source));
      const auto module = resolveBitConstantSource(
          ".entry kernel() { .reg ." + std::string(type) + " %q; mov." +
          std::string(type) + " %q, " + std::string(fixture.source) + "; " +
          (type == "b32" ? "and.b32" : "xor.b64") + " %q, %q, " +
          std::string(fixture.source) + "; }");
      ASSERT_EQ(module.has_value(), type == fixture.matching_type);
      if (!module)
        continue;
      const auto& body = module->functions.front().body;
      ASSERT_EQ(body.size(), 2u);
      const auto& mov = dynamic_cast<const MovScalar&>(*body.front());
      const auto& immediate =
          std::get<ResolvedImmediate>(mov.src_mov_source->value);
      EXPECT_EQ(immediate.bits, fixture.bits);
      EXPECT_EQ(immediate.type,
                type == "b32" ? ScalarType::B32 : ScalarType::B64);
      EXPECT_FALSE(immediate.integer_source_bits);
      EXPECT_FALSE(immediate.is_negative);
      const auto& second =
          type == "b32" ? std::get<ResolvedImmediate>(
                              dynamic_cast<const AndB32&>(*body[1]).src2.value)
                        : std::get<ResolvedImmediate>(
                              dynamic_cast<const XorB64&>(*body[1]).src2.value);
      EXPECT_EQ(second.bits, fixture.bits);
      EXPECT_EQ(second.type, immediate.type);
      EXPECT_FALSE(second.integer_source_bits);
      EXPECT_TRUE(validateModule(*module));
    }
  }
}

/** Formal-driven call literals retain their lexical kind and revalidate after AST release. */
TEST(FloatingBitConstants, CallsRetainDirectF32SourceProvenance) {
  for (const auto source : {"0f7fc01234"}) {
    auto module = resolveBitConstantSource(
        ".extern .func take(.reg .b32 x); .entry kernel() { call take, (" +
        std::string(source) + "); }");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    auto& call =
        dynamic_cast<CallDirect&>(*module->functions.back().body.front());
    auto& literal =
        std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value);
    EXPECT_EQ(literal.kind, syntax_ast::AstImmediateKind::F32Hex);
    EXPECT_EQ(literal.spelling, "0f7fc01234");
    ASSERT_TRUE(literal.value);
    EXPECT_EQ(literal.value->bits, 0x7fc01234u);
    EXPECT_EQ(literal.value->type, ScalarType::B32);
    EXPECT_FALSE(literal.value->integer_source_bits);
    EXPECT_TRUE(validateModule(*module));
    literal.spelling = "0f3f800000";
    EXPECT_FALSE(validateModule(*module));
  }
  for (const auto source : {"1.0", "0d3ff0000000000000", "(0.5 + 0.5)"}) {
    const auto module = resolveBitConstantSource(
        ".extern .func take(.reg .b64 x); .entry kernel() { call take, (" +
        std::string(source) + "); }");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    const auto& call =
        dynamic_cast<const CallDirect&>(*module->functions.back().body.front());
    const auto& literal =
        std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value);
    ASSERT_TRUE(literal.value);
    EXPECT_EQ(literal.value->bits, 0x3ff0000000000000u);
    EXPECT_EQ(literal.value->type, ScalarType::B64);
    EXPECT_FALSE(literal.value->integer_source_bits);
    EXPECT_TRUE(validateModule(*module));
  }
  for (const auto& [type, source] :
       {std::pair{"b32", "(0f3f800000)"}, std::pair{"b32", "((0f3f800000))"},
        std::pair{"b64", "0f3f800000"}, std::pair{"b64", "((0f3f800000))"},
        std::pair{"b32", "1.0"}, std::pair{"b32", "0d3ff0000000000000"},
        std::pair{"b32", "(+0f3f800000)"},
        std::pair{"b64", "(1 ? 1.0 : 0f3f800000)"}}) {
    EXPECT_FALSE(resolveBitConstantSource(
        ".extern .func take(.reg ." + std::string(type) +
        " x); .entry kernel() { call take, (" + std::string(source) + "); }"));
  }
}

/** Metadata-backed indirect calls share formal-driven floating source precision rules. */
TEST(FloatingBitConstants, IndirectCallsRetainTypedFloatingSourcesAfterCopy) {
  constexpr std::array fixtures{
      FloatingBitFixture{"0f3f800000", "b32", 0x3f800000},
      FloatingBitFixture{"-0d3ff0000000000000", "b64", 0xbff0000000000000},
      FloatingBitFixture{"(-0d3ff0000000000000)", "f32", 0xbf800000},
      FloatingBitFixture{"(+0d8000000000000000)", "f64", 0x8000000000000000}};
  for (const auto& fixture : fixtures) {
    const auto module = resolveBitConstantSource(
        ".entry kernel() { .reg .u64 %fptr; prototype: .callprototype _ (.reg "
        "." +
        std::string(fixture.matching_type) + " argument); call %fptr, (" +
        std::string(fixture.source) + "), prototype; }");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    const auto& call = dynamic_cast<const CallDirect&>(
        *module->functions.front().body.front());
    const auto& literal =
        std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value);
    ASSERT_TRUE(literal.value);
    EXPECT_EQ(literal.value->bits, fixture.bits);
    EXPECT_FALSE(literal.value->integer_source_bits);
    EXPECT_TRUE(validateModule(*module));
    const auto copy = *module;
    EXPECT_TRUE(validateModule(copy));
  }
  for (const auto& [type, source] :
       {std::pair{"b32", "(0f3f800000)"}, std::pair{"f32", "+0f3f800000"},
        std::pair{"f64", "-0f3f800000"}, std::pair{"b64", "0f3f800000"},
        std::pair{"b32", "0d3ff0000000000000"}}) {
    EXPECT_FALSE(resolveBitConstantSource(
        ".entry kernel() { .reg .u64 %fptr; prototype: .callprototype _ (.reg "
        "." +
        std::string(type) + " argument); call %fptr, (" + std::string(source) +
        "), prototype; }"));
  }
}

/** Signed lexical aliases obey the same source-format rules in storage, MOV, and calls. */
TEST(FloatingBitConstants,
     SignedAndWrappedSourcesAgreeAcrossStorageScalarAndCallRoutes) {
  for (const auto source : {"(0f3f800000)", "((0f3f800000))", "+0f3f800000",
                            "-0f3f800000", "(+0f3f800000)", "(-0f3f800000)",
                            "+0d3ff0000000000000", "-0d3ff0000000000000",
                            "(+0d3ff0000000000000)", "(-0d3ff0000000000000)"}) {
    const std::string_view spelling = source;
    const bool exact_f32 = spelling.find("0f") != std::string_view::npos;
    const bool negative = spelling.find('-') != std::string_view::npos;
    for (const auto type : {"f32", "f64", "b32", "b64"}) {
      SCOPED_TRACE(std::string(type) + " " + source);
      const bool accepted = !exact_f32 && std::string_view(type) != "b32";
      const uint64_t bits =
          std::string_view(type) == "f32"
              ? (negative ? uint64_t{0xbf800000} : uint64_t{0x3f800000})
              : (negative ? uint64_t{0xbff0000000000000}
                          : uint64_t{0x3ff0000000000000});
      for (const auto check_source : {true, false}) {
        const auto storage = resolveBitConstantSource(
            ".global ." + std::string(type) + " object = " + source + ";",
            check_source);
        ASSERT_EQ(storage.has_value(), accepted);
        if (storage) {
          EXPECT_EQ(
              std::get<StorageConstant>(storage->storage_declarations.front()
                                            .initializer.front()
                                            .value)
                  .bits,
              bits);
          EXPECT_TRUE(validateModule(*storage));
        }
      }
      const auto scalar = resolveBitConstantSource(
          ".entry kernel() { .reg ." + std::string(type) + " %q; mov." + type +
          " %q, " + source + "; }");
      ASSERT_EQ(scalar.has_value(), accepted);
      if (scalar) {
        const auto& mov = dynamic_cast<const MovScalar&>(
            *scalar->functions.front().body.front());
        EXPECT_EQ(std::get<ResolvedImmediate>(mov.src_mov_source->value).bits,
                  bits);
        EXPECT_TRUE(validateModule(*scalar));
      }
      const auto call_module = resolveBitConstantSource(
          ".extern .func take(.reg ." + std::string(type) +
          " x); .entry kernel() { call take, (" + source + "); }");
      ASSERT_EQ(call_module.has_value(), accepted);
      if (call_module) {
        const auto& call = dynamic_cast<const CallDirect&>(
            *call_module->functions.back().body.front());
        const auto& literal = std::get<ResolvedCallLiteral>(
            call.arguments->value.values[0].value);
        ASSERT_TRUE(literal.value);
        EXPECT_EQ(literal.value->bits, bits);
        EXPECT_TRUE(validateModule(*call_module));
      }
    }
  }
}

/** Bit data without integer provenance is legal only for the two floating carrier widths. */
TEST(FloatingBitConstants,
     ValueVectorsPreserveFloatingOriginAndIntegerRoleControls) {
  for (const auto type : {ScalarType::B8, ScalarType::B16, ScalarType::B32,
                          ScalarType::B64, ScalarType::B128}) {
    ResolvedImmediate value{.bits = 0, .type = type};
    EXPECT_EQ(valid_value_vector_immediate(value),
              type == ScalarType::B32 || type == ScalarType::B64);
    value.is_negative = true;
    EXPECT_FALSE(valid_value_vector_immediate(value));
  }
  const auto vector = resolveBitConstantSource(
      ".global .u32 A; .entry kernel() { .reg .v2 .b32 V; "
      "mov.v2.b32 V, {0f3f800000,0f80000000}; st.global.b32 [A], 0f3f800000; "
      "}");
  ASSERT_TRUE(vector.has_value()) << vector.error().front().message;
  EXPECT_TRUE(validateModule(*vector));
  for (const auto body :
       {"mov.b32 0f3f800000, %q;", "shl.b64 %d, %d, 1.0;",
        "st.global.v2.b32 [%d], {0f3f800000, %q};",
        "mov.b32 %q, (+0f3f800000);", "mov.b32 %q, +0f3f800000;",
        "mov.b64 %d, (1 ? 2.0 : 0f3f800000);"}) {
    SCOPED_TRACE(body);
    EXPECT_FALSE(resolveBitConstantSource(
        ".global .u32 A; .entry kernel() { .reg .b32 %q; .reg .b64 %d; " +
        std::string(body) + " }"));
  }
}

/** Resolve a written use expression without extending any instruction permissions. */
std::expected<ResolvedImmediate, ResolveDiagnostic> packedWordExpression(
    std::string_view source, ScalarType type) {
  PtxSyntaxParser parser("mov.b32 %r, " + std::string(source) + ";");
  const auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ResolveDiagnostic{});
  return detail::resolve_numeric_operand(ast->operands[1], type);
}

/** Rebuild a synchronous generic view from the actual current word on every check. */
checker::CheckResult checkPackedCurrentWord(ScalarType selected,
                                            const ResolvedImmediate& word) {
  const checker::OperandDescriptor descriptor{
      .target_field_id = "src",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = selected},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Immediate};
  const SourceRange range{{4, 3}, {4, 13}};
  const checker::OperandView view{
      .field_id = "src",
      .actual_shape = checker::OperandShape::Immediate,
      .immediate_type = word.type,
      .immediate_bits = word.bits,
      .immediate_is_negative = word.is_negative,
      .locations = {&range, 1},
      .integer_source_bits = word.integer_source_bits};
  const auto profile = base::find_target_profile("sm_100a");
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
  return checker::check_operands({&descriptor, 1}, {}, {&view, 1}, {}, context);
}

/** All packed32 formats preserve complete words, including encoded signs and NaNs. */
TEST(FloatingBitConstants,
     PackedWordsPreserveExactBitsAndRejectNumericPacking) {
  constexpr std::array words{std::pair{"0f00000000", uint64_t{0}},
                             std::pair{"0f00000001", uint64_t{1}},
                             std::pair{"0f80000000", uint64_t{0x80000000}},
                             std::pair{"0f7f800001", uint64_t{0x7f800001}},
                             std::pair{"0f7fc12345", uint64_t{0x7fc12345}},
                             std::pair{"0fffffffff", uint64_t{0xffffffff}},
                             std::pair{"0f3f800000", uint64_t{0x3f800000}},
                             std::pair{"0f3e003e00", uint64_t{0x3e003e00}}};
  for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2,
                          ScalarType::E4m3x4, ScalarType::E5m2x4}) {
    for (const auto& [source, bits] : words) {
      SCOPED_TRACE(std::string(to_string(type)) + " " + source);
      const auto value = packedWordExpression(source, type);
      ASSERT_TRUE(value) << value.error().message;
      EXPECT_EQ(value->type, type);
      EXPECT_EQ(value->bits, bits);
      EXPECT_FALSE(value->integer_source_bits);
      EXPECT_FALSE(value->is_negative);
      EXPECT_TRUE(checkPackedCurrentWord(type, *value));
    }
    for (const auto source :
         {"0", "0x3e003e00", "0x3e003e00U", "1+2", "1.5==1.5", "-0f3e003e00",
          "+0f3e003e00", "(0f3e003e00)", "1.5", "0d3ff8000000000000"}) {
      SCOPED_TRACE(std::string(to_string(type)) + " " + source);
      EXPECT_FALSE(packedWordExpression(source, type));
    }
  }
  for (const auto type :
       {ScalarType::B8, ScalarType::B16, ScalarType::B32, ScalarType::B64}) {
    const auto integer = packedWordExpression("1", type);
    ASSERT_TRUE(integer);
    EXPECT_EQ(integer->bits, 1u);
    EXPECT_EQ(integer->integer_source_bits, 1u);
  }
  for (const auto type : {ScalarType::E4m3x2, ScalarType::E5m2x2})
    EXPECT_FALSE(packedWordExpression("0f00003c3c", type));
}

/** Current data accepts coherent word edits, with flags and selected type rechecked. */
TEST(FloatingBitConstants, PackedCurrentWordsRejectFlagsWithoutLiteralHistory) {
  for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2,
                          ScalarType::E4m3x4, ScalarType::E5m2x4}) {
    SCOPED_TRACE(std::string(to_string(type)));
    ResolvedImmediate word{.bits = 1, .type = type};
    ASSERT_TRUE(checkPackedCurrentWord(type, word));
    word.bits = 2;
    EXPECT_TRUE(checkPackedCurrentWord(type, word));
    for (int mutation = 0; mutation < 5; ++mutation) {
      auto bad = word;
      if (mutation == 0)
        bad.bits = uint64_t{1} << 32;
      if (mutation == 1)
        bad.integer_source_bits = 0;
      if (mutation == 2)
        bad.integer_source_bits = bad.bits;
      if (mutation == 3)
        bad.is_negative = true;
      if (mutation == 4)
        bad.type = ScalarType::F32;
      const auto result = checkPackedCurrentWord(type, bad);
      ASSERT_FALSE(result) << mutation;
      EXPECT_EQ(result.error().front().range, (SourceRange{{4, 3}, {4, 13}}));
      if (mutation < 4)
        EXPECT_EQ(result.error().front().kind,
                  checker::CheckDiagnosticKind::ImmediateValueMismatch);
      EXPECT_TRUE(checkPackedCurrentWord(type, word));
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
