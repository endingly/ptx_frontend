#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** One independently specified register spelling accepted at a fundamental width. */
struct FundamentalRegisterType {
  /** PTX declaration suffix, without its leading period. */
  std::string_view spelling;
};

/** One explicitly expected scalar-compatibility result, independent of YAML. */
struct ScalarCompatibilityCase {
  /** Declared type presented by the register. */
  ScalarType actual;
  /** Type required by the instruction operand. */
  ScalarType required;
  /** Whether same-width compatibility must accept this pair. */
  bool same_width_accepted;
};

/** Fundamental 32-bit integer and bit containers for ordinary PTX operands. */
constexpr std::array kFundamental32 = {
    FundamentalRegisterType{"u32"},
    FundamentalRegisterType{"s32"},
    FundamentalRegisterType{"b32"},
};

/** Fundamental 64-bit integer and bit containers for ordinary PTX operands. */
constexpr std::array kFundamental64 = {
    FundamentalRegisterType{"u64"},
    FundamentalRegisterType{"s64"},
    FundamentalRegisterType{"b64"},
};

/** Complete 32-bit bit, unsigned, signed, and floating compatibility matrix. */
constexpr std::array kScalarCompatibility32 = {
    ScalarCompatibilityCase{ScalarType::B32, ScalarType::B32, true},
    ScalarCompatibilityCase{ScalarType::B32, ScalarType::U32, true},
    ScalarCompatibilityCase{ScalarType::B32, ScalarType::S32, true},
    ScalarCompatibilityCase{ScalarType::B32, ScalarType::F32, true},
    ScalarCompatibilityCase{ScalarType::U32, ScalarType::B32, true},
    ScalarCompatibilityCase{ScalarType::U32, ScalarType::U32, true},
    ScalarCompatibilityCase{ScalarType::U32, ScalarType::S32, true},
    ScalarCompatibilityCase{ScalarType::U32, ScalarType::F32, false},
    ScalarCompatibilityCase{ScalarType::S32, ScalarType::B32, true},
    ScalarCompatibilityCase{ScalarType::S32, ScalarType::U32, true},
    ScalarCompatibilityCase{ScalarType::S32, ScalarType::S32, true},
    ScalarCompatibilityCase{ScalarType::S32, ScalarType::F32, false},
    ScalarCompatibilityCase{ScalarType::F32, ScalarType::B32, true},
    ScalarCompatibilityCase{ScalarType::F32, ScalarType::U32, false},
    ScalarCompatibilityCase{ScalarType::F32, ScalarType::S32, false},
    ScalarCompatibilityCase{ScalarType::F32, ScalarType::F32, true},
};

/** Complete 64-bit bit, unsigned, signed, and floating compatibility matrix. */
constexpr std::array kScalarCompatibility64 = {
    ScalarCompatibilityCase{ScalarType::B64, ScalarType::B64, true},
    ScalarCompatibilityCase{ScalarType::B64, ScalarType::U64, true},
    ScalarCompatibilityCase{ScalarType::B64, ScalarType::S64, true},
    ScalarCompatibilityCase{ScalarType::B64, ScalarType::F64, true},
    ScalarCompatibilityCase{ScalarType::U64, ScalarType::B64, true},
    ScalarCompatibilityCase{ScalarType::U64, ScalarType::U64, true},
    ScalarCompatibilityCase{ScalarType::U64, ScalarType::S64, true},
    ScalarCompatibilityCase{ScalarType::U64, ScalarType::F64, false},
    ScalarCompatibilityCase{ScalarType::S64, ScalarType::B64, true},
    ScalarCompatibilityCase{ScalarType::S64, ScalarType::U64, true},
    ScalarCompatibilityCase{ScalarType::S64, ScalarType::S64, true},
    ScalarCompatibilityCase{ScalarType::S64, ScalarType::F64, false},
    ScalarCompatibilityCase{ScalarType::F64, ScalarType::B64, true},
    ScalarCompatibilityCase{ScalarType::F64, ScalarType::U64, false},
    ScalarCompatibilityCase{ScalarType::F64, ScalarType::S64, false},
    ScalarCompatibilityCase{ScalarType::F64, ScalarType::F64, true},
};

/** Checker context that enables every regression instruction in this file. */
const checker::Context kContext{
    .target = {.ptx_version = {9, 3}, .sm_version = 100}};

/** Parse and resolve exactly one instruction, keeping parser failures test-visible. */
std::optional<ResolvedInstruction> resolveSingleInstruction(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << (ast.diagnostics.empty()
                          ? "PTX source did not parse."
                          : ast.diagnostics.front().message);
    return std::nullopt;
  }

  auto module = resolveModule(*ast);
  if (!module) {
    ADD_FAILURE() << module.error().front().message;
    return std::nullopt;
  }
  if (module->functions.size() != 1 ||
      module->functions.front().body.size() != 1) {
    ADD_FAILURE() << "Expected one resolved instruction.";
    return std::nullopt;
  }
  return std::move(module->functions.front().body.front());
}

/** Return the source range of one selected occurrence in a single-line fixture. */
SourceRange occurrenceRange(std::string_view source, std::string_view needle,
                            size_t occurrence) {
  if (occurrence == 0) {
    ADD_FAILURE() << "Source-range occurrence is one-based.";
    return {};
  }
  size_t position = 0;
  for (size_t index = 0; index != occurrence; ++index) {
    position = source.find(needle, position);
    if (position == std::string_view::npos) {
      ADD_FAILURE() << "Missing occurrence " << occurrence << " of '" << needle
                    << "'.";
      return {};
    }
    ++position;
  }
  --position;
  const size_t end = position + needle.size() + 1;
  if (end > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    ADD_FAILURE() << "Fixture is too large for SourceRange.";
    return {};
  }
  return SourceRange{{1, static_cast<int32_t>(position + 1)},
                     {1, static_cast<int32_t>(end)}};
}

/** Require the generated checker to accept the resolved instruction. */
void expectAccepted(const checker::CheckResult& checked) {
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
}

/** Require one type diagnostic at the independently computed operand range. */
void expectTypeMismatch(const checker::CheckResult& checked,
                        SourceRange expected_range) {
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, expected_range);
}

/** Find a generated resolved variant by its stable public name. */
const check_end::ResolvedVariantDescriptor* findResolvedVariant(
    const check_end::ResolvedInstructionDescriptor& descriptor,
    std::string_view name) {
  const auto found = std::ranges::find_if(
      descriptor.variants,
      [name](const check_end::ResolvedVariantDescriptor& variant) {
        return variant.variant_name == name;
      });
  if (found == descriptor.variants.end()) {
    ADD_FAILURE() << "Missing generated resolved variant '" << name << "'.";
    return nullptr;
  }
  return &*found;
}

/** Assert the policy carried by every binding in one generated operand layout. */
void expectBindingPolicy(
    const check_end::ResolvedInstructionDescriptor& descriptor,
    std::string_view variant_name, base::ScalarTypeSizePolicy policy) {
  const auto* variant = findResolvedVariant(descriptor, variant_name);
  ASSERT_NE(variant, nullptr);
  ASSERT_EQ(variant->operand_layouts.size(), 1u) << variant_name;
  ASSERT_FALSE(variant->operand_layouts.front().bindings.empty())
      << variant_name;
  for (const auto& binding : variant->operand_layouts.front().bindings)
    EXPECT_EQ(binding.register_width_policy, policy)
        << variant_name << "." << binding.target_field_id;
}

/** Base type semantics preserve fundamental compatibility while Exact remains identity. */
TEST(RegisterTypePolicy, BaseFundamentalCompatibilityMatrixIsIndependent) {
  const auto expect_matrix = [](const auto& matrix) {
    for (const ScalarCompatibilityCase& entry : matrix) {
      SCOPED_TRACE(static_cast<int>(entry.actual));
      SCOPED_TRACE(static_cast<int>(entry.required));
      EXPECT_EQ(
          base::scalar_types_compatible(entry.actual, entry.required,
                                        base::ScalarTypeSizePolicy::SameWidth),
          entry.same_width_accepted);
      EXPECT_EQ(
          base::scalar_types_compatible(entry.actual, entry.required,
                                        base::ScalarTypeSizePolicy::Exact),
          entry.actual == entry.required);
    }
  };
  expect_matrix(kScalarCompatibility32);
  expect_matrix(kScalarCompatibility64);
}

/** Construct a wide multiply fixture with independently chosen declaration types. */
std::string wideMulSource(std::string_view destination_type,
                          std::string_view first_multiplicand_type,
                          std::string_view second_multiplicand_type = "") {
  if (second_multiplicand_type.empty())
    second_multiplicand_type = first_multiplicand_type;
  return ".entry kernel() { .reg ." + std::string(destination_type) +
         " %dst; .reg ." + std::string(first_multiplicand_type) +
         " %src1; .reg ." + std::string(second_multiplicand_type) +
         " %src2; mul.wide.u32 %dst, %src1, %src2; }";
}

/** Construct a wide multiply-add fixture with independently chosen declaration types. */
std::string wideMadSource(std::string_view destination_type,
                          std::string_view first_multiplicand_type,
                          std::string_view second_multiplicand_type = "",
                          std::string_view addend_type = "") {
  if (second_multiplicand_type.empty())
    second_multiplicand_type = first_multiplicand_type;
  if (addend_type.empty())
    addend_type = destination_type;
  return ".entry kernel() { .reg ." + std::string(destination_type) +
         " %dst; .reg ." + std::string(addend_type) + " %src3; .reg ." +
         std::string(first_multiplicand_type) + " %src1; .reg ." +
         std::string(second_multiplicand_type) +
         " %src2; mad.wide.u32 %dst, %src1, %src2, %src3; }";
}

/** Ordinary wide arithmetic accepts every fundamental container of each width. */
TEST(RegisterTypePolicy, WideIntegerFundamentalMatrixIsMetamorphic) {
  for (const auto destination : kFundamental64) {
    for (const auto multiplicand : kFundamental32) {
      const std::string mul_source =
          wideMulSource(destination.spelling, multiplicand.spelling);
      SCOPED_TRACE(mul_source);
      const auto mul = resolveSingleInstruction(mul_source);
      ASSERT_TRUE(mul);
      expectAccepted(checker::check(std::get<Mul>(*mul), kContext));

      const std::string mad_source =
          wideMadSource(destination.spelling, multiplicand.spelling);
      SCOPED_TRACE(mad_source);
      const auto mad = resolveSingleInstruction(mad_source);
      ASSERT_TRUE(mad);
      expectAccepted(checker::check(std::get<Mad>(*mad), kContext));
    }
  }
}

/** Wide arithmetic rejects wrong widths and non-bit cross-category register types. */
TEST(RegisterTypePolicy, WideArithmeticRejectsWidthsAndCategoriesAtOperands) {
  const std::string wide_multiplicand = wideMulSource("u64", "b64", "u32");
  const auto wide_multiplicand_instruction =
      resolveSingleInstruction(wide_multiplicand);
  ASSERT_TRUE(wide_multiplicand_instruction);
  expectTypeMismatch(
      checker::check(std::get<Mul>(*wide_multiplicand_instruction), kContext),
      occurrenceRange(wide_multiplicand, "%src1", 2));

  const std::string float_multiplicand = wideMadSource("u64", "f32", "u32");
  const auto float_multiplicand_instruction =
      resolveSingleInstruction(float_multiplicand);
  ASSERT_TRUE(float_multiplicand_instruction);
  expectTypeMismatch(
      checker::check(std::get<Mad>(*float_multiplicand_instruction), kContext),
      occurrenceRange(float_multiplicand, "%src1", 2));

  const std::string narrow_result = wideMadSource("u32", "u32", "u32", "u64");
  const auto narrow_result_instruction =
      resolveSingleInstruction(narrow_result);
  ASSERT_TRUE(narrow_result_instruction);
  expectTypeMismatch(
      checker::check(std::get<Mad>(*narrow_result_instruction), kContext),
      occurrenceRange(narrow_result, "%dst", 2));

  const std::string float_result = wideMulSource("f64", "u32");
  const auto float_result_instruction = resolveSingleInstruction(float_result);
  ASSERT_TRUE(float_result_instruction);
  expectTypeMismatch(
      checker::check(std::get<Mul>(*float_result_instruction), kContext),
      occurrenceRange(float_result, "%dst", 2));
}

/** Bit containers are valid ordinary operands, including scalar floating MAD. */
TEST(RegisterTypePolicy, BitContainersWorkForOrdinaryArithmeticAndBitCounts) {
  constexpr std::string_view source =
      ".entry kernel() { .reg .b32 %f0, %f1, %f2, %f3; mad.rn.f32 %f0, %f1, "
      "%f2, %f3; }";
  const auto mad = resolveSingleInstruction(source);
  ASSERT_TRUE(mad);
  expectAccepted(checker::check(std::get<Mad>(*mad), kContext));

  constexpr std::string_view popc_source =
      ".entry kernel() { .reg .b32 %dst; .reg .s32 %src; popc.b32 %dst, %src; "
      "}";
  const auto popc = resolveSingleInstruction(popc_source);
  ASSERT_TRUE(popc);
  expectAccepted(checker::check(std::get<Popc>(*popc), kContext));

  constexpr std::string_view clz32_source =
      ".entry kernel() { .reg .b32 %dst; .reg .u32 %src; clz.b32 %dst, %src; }";
  const auto clz32 = resolveSingleInstruction(clz32_source);
  ASSERT_TRUE(clz32);
  expectAccepted(checker::check(std::get<Clz>(*clz32), kContext));

  constexpr std::string_view clz64_source =
      ".entry kernel() { .reg .b32 %dst; .reg .s64 %src; clz.b64 %dst, %src; }";
  const auto clz64 = resolveSingleInstruction(clz64_source);
  ASSERT_TRUE(clz64);
  expectAccepted(checker::check(std::get<Clz>(*clz64), kContext));

  constexpr std::string_view wrong_result =
      ".entry kernel() { .reg .u64 %dst; .reg .b32 %src; popc.b32 %dst, %src; "
      "}";
  const auto rejected = resolveSingleInstruction(wrong_result);
  ASSERT_TRUE(rejected);
  expectTypeMismatch(checker::check(std::get<Popc>(*rejected), kContext),
                     occurrenceRange(wrong_result, "%dst", 2));
}

/** A target-bearing multi-instruction kernel retains the same wide-multiply contract. */
TEST(RegisterTypePolicy, TargetedKernelUsesSameWideMultiplyPolicy) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .b32 %src1, %src2;
  .reg .b64 %dst;
  mov.b32 %src1, 1;
  mov.b32 %src2, 2;
  mul.wide.u32 %dst, %src1, %src2;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto module = resolveModule(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  ASSERT_EQ(module->functions.size(), 1u);
  const auto& body = module->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const checker::Context target_context{
      .target = {.ptx_version = {8, 0}, .sm_version = 80}};
  for (const ResolvedInstruction& instruction : body) {
    const auto checked = std::visit(
        [&target_context](const auto& concrete) {
          return checker::check(concrete, target_context);
        },
        instruction);
    expectAccepted(checked);
  }
}

/** Representative ordinary exact patterns remain compatible with bit/fundamental storage. */
TEST(RegisterTypePolicy, OtherOrdinaryPatternsUseFundamentalCompatibility) {
  constexpr std::string_view bfind_source =
      ".entry kernel() { .reg .s32 %dst, %src; bfind.shiftamt.u32 %dst, %src; "
      "}";
  const auto bfind = resolveSingleInstruction(bfind_source);
  ASSERT_TRUE(bfind);
  expectAccepted(checker::check(std::get<Bfind>(*bfind), kContext));

  constexpr std::string_view bfe_source =
      ".entry kernel() { .reg .b32 %dst; .reg .s32 %src; bfe.u32 %dst, %src, "
      "0, 8; }";
  const auto bfe = resolveSingleInstruction(bfe_source);
  ASSERT_TRUE(bfe);
  expectAccepted(checker::check(std::get<Bfe>(*bfe), kContext));

  constexpr std::string_view div_source =
      ".entry kernel() { .reg .b64 %dst, %src1, %src2; div.rn.f64 %dst, %src1, "
      "%src2; }";
  const auto div = resolveSingleInstruction(div_source);
  ASSERT_TRUE(div);
  expectAccepted(checker::check(std::get<Div>(*div), kContext));

  constexpr std::string_view min_source =
      ".entry kernel() { .reg .b32 %dst, %src1, %src2; min.NaN.f32 %dst, "
      "%src1, %src2; }";
  const auto min = resolveSingleInstruction(min_source);
  ASSERT_TRUE(min);
  expectAccepted(checker::check(std::get<Min>(*min), kContext));

  constexpr std::string_view max_source =
      ".entry kernel() { .reg .b32 %dst, %src1, %src2; max.NaN.f32 %dst, "
      "%src1, %src2; }";
  const auto max = resolveSingleInstruction(max_source);
  ASSERT_TRUE(max);
  expectAccepted(checker::check(std::get<Max>(*max), kContext));
}

/** Packed bfloat FMA still requires its exact b16 storage container. */
TEST(RegisterTypePolicy, PackedBfloatStorageRemainsExact) {
  constexpr std::string_view source =
      ".entry kernel() { .reg .b16 %dst, %src2, %src3; .reg .f16 %src1; "
      "fma.rn.bf16 %dst, %src1, %src2, %src3; }";
  const auto fma = resolveSingleInstruction(source);
  ASSERT_TRUE(fma);
  expectTypeMismatch(checker::check(std::get<Fma>(*fma), kContext),
                     occurrenceRange(source, "%src1", 2));
}

/** Packed-half negation and conversion use compatible containers, not exact identity. */
TEST(RegisterTypePolicy, PackedHalfContainersRemainSameWidth) {
  constexpr std::string_view neg_source =
      ".entry kernel() { .reg .f16x2 %dst; .reg .b32 %src; neg.f16x2 %dst, "
      "%src; }";
  const auto neg = resolveSingleInstruction(neg_source);
  ASSERT_TRUE(neg);
  expectAccepted(checker::check(std::get<Neg>(*neg), kContext));

  constexpr std::string_view cvt_f16x2_source =
      ".entry kernel() { .reg .f16x2 %dst; .reg .b32 %src1, %src2; "
      "cvt.rn.f16x2.f32 %dst, %src1, %src2; }";
  const auto cvt_f16x2 = resolveSingleInstruction(cvt_f16x2_source);
  ASSERT_TRUE(cvt_f16x2);
  expectAccepted(checker::check(std::get<Cvt>(*cvt_f16x2), kContext));

  constexpr std::string_view cvt_b32_source =
      ".entry kernel() { .reg .b32 %dst; .reg .f32 %src1, %src2; "
      "cvt.rn.f16x2.f32 %dst, %src1, %src2; }";
  const auto cvt_b32 = resolveSingleInstruction(cvt_b32_source);
  ASSERT_TRUE(cvt_b32);
  expectAccepted(checker::check(std::get<Cvt>(*cvt_b32), kContext));

  constexpr std::string_view wrong_destination =
      ".entry kernel() { .reg .u32 %dst; .reg .f32 %src1, %src2; "
      "cvt.rn.f16x2.f32 %dst, %src1, %src2; }";
  const auto rejected_destination = resolveSingleInstruction(wrong_destination);
  ASSERT_TRUE(rejected_destination);
  expectTypeMismatch(
      checker::check(std::get<Cvt>(*rejected_destination), kContext),
      occurrenceRange(wrong_destination, "%dst", 2));

  constexpr std::string_view wrong_source =
      ".entry kernel() { .reg .b32 %dst; .reg .u32 %src1; .reg .f32 %src2; "
      "cvt.rn.f16x2.f32 %dst, %src1, %src2; }";
  const auto rejected_source = resolveSingleInstruction(wrong_source);
  ASSERT_TRUE(rejected_source);
  expectTypeMismatch(checker::check(std::get<Cvt>(*rejected_source), kContext),
                     occurrenceRange(wrong_source, "%src1", 2));

  constexpr std::string_view cvt_f32_s32_source =
      ".entry kernel() { .reg .b32 %dst; .reg .u32 %src; cvt.rn.f32.s32 %dst, "
      "%src; }";
  const auto cvt_f32_s32 = resolveSingleInstruction(cvt_f32_s32_source);
  ASSERT_TRUE(cvt_f32_s32);
  expectAccepted(checker::check(std::get<Cvt>(*cvt_f32_s32), kContext));
}

/** Generated metadata distinguishes ordinary compatibility from packed exact storage. */
TEST(RegisterTypePolicy, GeneratedDescriptorsExposeTheWidthPolicy) {
  const auto same_width = base::ScalarTypeSizePolicy::SameWidth;
  expectBindingPolicy(Mul::get_resolved_descriptor(), "WideU32", same_width);
  expectBindingPolicy(Mad::get_resolved_descriptor(), "WideU32", same_width);
  expectBindingPolicy(Mad::get_resolved_descriptor(), "RnF32", same_width);
  expectBindingPolicy(Popc::get_resolved_descriptor(), "B32", same_width);
  expectBindingPolicy(Clz::get_resolved_descriptor(), "B32", same_width);
  expectBindingPolicy(Clz::get_resolved_descriptor(), "B64", same_width);
  expectBindingPolicy(Bfind::get_resolved_descriptor(), "ShiftamtU32",
                      same_width);
  expectBindingPolicy(Bfe::get_resolved_descriptor(), "U32", same_width);
  expectBindingPolicy(Div::get_resolved_descriptor(), "RnF32", same_width);
  expectBindingPolicy(Div::get_resolved_descriptor(), "RnF64", same_width);
  expectBindingPolicy(Min::get_resolved_descriptor(), "NanF32", same_width);
  expectBindingPolicy(Max::get_resolved_descriptor(), "NanF32", same_width);
  expectBindingPolicy(Neg::get_resolved_descriptor(), "F16x2", same_width);
  expectBindingPolicy(Cvt::get_resolved_descriptor(), "RnF16x2F32", same_width);
  expectBindingPolicy(Fma::get_resolved_descriptor(), "Bf16",
                      base::ScalarTypeSizePolicy::Exact);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
