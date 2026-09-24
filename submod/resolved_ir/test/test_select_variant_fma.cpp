#include <gtest/gtest.h>

#include <tuple>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/fma/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/fma/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/fma/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(ResolveFma, SelectsCompleteFmaVariantFamily) {
  const auto expect_variant = [](std::string_view source,
                                 Fma::VariantType expected) {
    const auto resolved = resolve<Fma>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_EQ(resolved->variant.index(), static_cast<size_t>(expected));
  };

  expect_variant("fma.rn.ftz.sat.f32 %f0, %f1, %f2, %f3;",
                 Fma::VariantType::RnF32);
  expect_variant("fma.rz.ftz.sat.f32 %f0, %f1, %f2, %f3;",
                 Fma::VariantType::DirectedF32);
  expect_variant("fma.rn.f64 %d0, %d1, %d2, %d3;", Fma::VariantType::RnF64);
  expect_variant("fma.rp.f64 %d0, %d1, %d2, %d3;",
                 Fma::VariantType::DirectedF64);
  expect_variant("fma.rm.ftz.f32x2 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::F32x2);
  expect_variant("fma.rn.ftz.sat.f16 %h0, %h1, %h2, %h3;",
                 Fma::VariantType::RnF16);
  expect_variant("fma.rn.ftz.sat.f16x2 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::RnF16x2);
  expect_variant("fma.rn.ftz.relu.f16 %h0, %h1, %h2, %h3;",
                 Fma::VariantType::HalfRelu);
  expect_variant("fma.rn.oob.sat.f16x2 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::HalfOob);
  expect_variant("fma.rn.oob.relu.f16 %h0, %h1, %h2, %h3;",
                 Fma::VariantType::HalfOobRelu);
  expect_variant("fma.rn.relu.bf16 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::Bf16);
  expect_variant("fma.rn.bf16x2 %b0, %b1, %b2, %b3;", Fma::VariantType::Bf16x2);
  expect_variant("fma.rn.oob.bf16 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::Bf16Oob);
  expect_variant("fma.rn.oob.relu.bf16x2 %b0, %b1, %b2, %b3;",
                 Fma::VariantType::Bf16x2Oob);
  expect_variant("fma.rp.sat.f32.f16 %f0, %h1, %h2, %f3;",
                 Fma::VariantType::MixedF32F16);
  expect_variant("fma.rm.f32.bf16 %f0, %b1, %b2, %f3;",
                 Fma::VariantType::MixedF32Bf16);
}

TEST(ResolveFma, ResolvesEveryCanonicalModifierCombination) {
  constexpr std::array<std::string_view, 4> roundings{"rn", "rz", "rm", "rp"};
  constexpr std::array<std::string_view, 2> optional_flags{"", ".ftz"};
  constexpr std::array<std::string_view, 2> saturation_flags{"", ".sat"};
  constexpr std::array<std::string_view, 2> half_types{"f16", "f16x2"};
  constexpr std::array<std::string_view, 2> bfloat_types{"bf16", "bf16x2"};
  constexpr std::array<std::string_view, 2> mixed_input_types{"f16", "bf16"};
  constexpr std::array<std::string_view, 2> relu_flags{"", ".relu"};
  size_t resolved_count = 0;

  const auto expect_resolved = [&resolved_count](const std::string& source) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Fma>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ++resolved_count;
  };

  for (const auto rounding : roundings) {
    for (const auto ftz : optional_flags) {
      for (const auto sat : saturation_flags) {
        expect_resolved("fma." + std::string(rounding) + std::string(ftz) +
                        std::string(sat) + ".f32 %f0, %f1, %f2, %f3;");
      }
    }
    expect_resolved("fma." + std::string(rounding) +
                    ".f64 %d0, %d1, %d2, %d3;");
    for (const auto ftz : optional_flags) {
      expect_resolved("fma." + std::string(rounding) + std::string(ftz) +
                      ".f32x2 %b0, %b1, %b2, %b3;");
    }
  }

  for (const auto type : half_types) {
    const std::string_view registers =
        type == "f16" ? "%h0, %h1, %h2, %h3" : "%b0, %b1, %b2, %b3";
    for (const auto ftz : optional_flags) {
      for (const auto sat : saturation_flags) {
        expect_resolved("fma.rn" + std::string(ftz) + std::string(sat) + "." +
                        std::string(type) + " " + std::string(registers) + ";");
      }
      expect_resolved("fma.rn" + std::string(ftz) + ".relu." +
                      std::string(type) + " " + std::string(registers) + ";");
    }
    for (const auto sat : saturation_flags) {
      expect_resolved("fma.rn.oob" + std::string(sat) + "." +
                      std::string(type) + " " + std::string(registers) + ";");
    }
    expect_resolved("fma.rn.oob.relu." + std::string(type) + " " +
                    std::string(registers) + ";");
  }

  for (const auto type : bfloat_types) {
    for (const auto relu : relu_flags) {
      expect_resolved("fma.rn" + std::string(relu) + "." + std::string(type) +
                      " %b0, %b1, %b2, %b3;");
      expect_resolved("fma.rn.oob" + std::string(relu) + "." +
                      std::string(type) + " %b0, %b1, %b2, %b3;");
    }
  }

  for (const auto input_type : mixed_input_types) {
    const std::string_view inputs =
        input_type == "f16" ? "%h1, %h2" : "%b1, %b2";
    for (const auto rounding : roundings) {
      for (const auto sat : saturation_flags) {
        expect_resolved("fma." + std::string(rounding) + std::string(sat) +
                        ".f32." + std::string(input_type) + " %f0, " +
                        std::string(inputs) + ", %f3;");
      }
    }
  }

  EXPECT_EQ(resolved_count, 70U);
}

TEST(ResolveFma, PreservesExpandedVariantModifiersAndTypes) {
  const auto directed =
      resolve<Fma>(parse_instruction("fma.rz.ftz.sat.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(directed.has_value()) << directed.error().message;
  const auto* directed_f32 = std::get_if<Fma::DirectedF32>(&directed->variant);
  ASSERT_NE(directed_f32, nullptr);
  EXPECT_EQ(directed_f32->rounding.value, RoundingMode::Rz);
  EXPECT_TRUE(directed_f32->ftz.value);
  EXPECT_TRUE(directed_f32->saturate.value);
  EXPECT_EQ(Fma::DirectedF32::type, ScalarType::F32);

  const auto half_relu = resolve<Fma>(
      parse_instruction("fma.rn.ftz.relu.f16x2 %b0, %b1, %b2, %b3;"));
  ASSERT_TRUE(half_relu.has_value()) << half_relu.error().message;
  const auto* relu = std::get_if<Fma::HalfRelu>(&half_relu->variant);
  ASSERT_NE(relu, nullptr);
  EXPECT_TRUE(relu->ftz.value);
  EXPECT_TRUE(Fma::HalfRelu::relu);
  EXPECT_EQ(relu->type.value, ScalarType::F16x2);

  const auto oob =
      resolve<Fma>(parse_instruction("fma.rn.oob.sat.f16 %h0, %h1, %h2, %h3;"));
  ASSERT_TRUE(oob.has_value()) << oob.error().message;
  const auto* half_oob = std::get_if<Fma::HalfOob>(&oob->variant);
  ASSERT_NE(half_oob, nullptr);
  EXPECT_TRUE(Fma::HalfOob::oob);
  EXPECT_TRUE(half_oob->saturate.value);
  EXPECT_EQ(half_oob->type.value, ScalarType::F16);

  const auto bf16 =
      resolve<Fma>(parse_instruction("fma.rn.relu.bf16 %b0, %b1, %b2, %b3;"));
  ASSERT_TRUE(bf16.has_value()) << bf16.error().message;
  const auto* bfloat = std::get_if<Fma::Bf16>(&bf16->variant);
  ASSERT_NE(bfloat, nullptr);
  EXPECT_TRUE(bfloat->relu.value);
  EXPECT_EQ(Fma::Bf16::type, ScalarType::BF16);

  const auto mixed = resolve<Fma>(
      parse_instruction("fma.rp.sat.f32.bf16 %f0, %b1, %b2, %f3;"));
  ASSERT_TRUE(mixed.has_value()) << mixed.error().message;
  const auto* mixed_bf16 = std::get_if<Fma::MixedF32Bf16>(&mixed->variant);
  ASSERT_NE(mixed_bf16, nullptr);
  EXPECT_EQ(mixed_bf16->rounding.value, RoundingMode::Rp);
  EXPECT_TRUE(mixed_bf16->saturate.value);
  EXPECT_EQ(Fma::MixedF32Bf16::result_type, ScalarType::F32);
  EXPECT_EQ(Fma::MixedF32Bf16::input_type, ScalarType::BF16);
}

TEST(ResolveFma, AcceptsFloatingImmediatesOnlyWhereTheFormAllowsThem) {
  const auto f32 = resolve<Fma>(parse_instruction(
      "fma.rn.f32 %f0, 0d3ff0000000000000, 1.5, 0f3f800000;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  const auto* f32_variant = std::get_if<Fma::RnF32>(&f32->variant);
  ASSERT_NE(f32_variant, nullptr);
  EXPECT_EQ(std::get<ResolvedImmediate>(f32_variant->src1.value).type,
            ScalarType::F32);
  EXPECT_EQ(std::get<ResolvedImmediate>(f32_variant->src2.value).type,
            ScalarType::F32);
  EXPECT_EQ(std::get<ResolvedImmediate>(f32_variant->src3.value).type,
            ScalarType::F32);

  const auto f64 = resolve<Fma>(parse_instruction(
      "fma.rp.f64 %d0, 0f3f800000, 1.5, 0d3ff0000000000000;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  const auto* f64_variant = std::get_if<Fma::DirectedF64>(&f64->variant);
  ASSERT_NE(f64_variant, nullptr);
  EXPECT_EQ(std::get<ResolvedImmediate>(f64_variant->src1.value).type,
            ScalarType::F64);
  EXPECT_EQ(std::get<ResolvedImmediate>(f64_variant->src2.value).type,
            ScalarType::F64);
  EXPECT_EQ(std::get<ResolvedImmediate>(f64_variant->src3.value).type,
            ScalarType::F64);

  const auto widened = resolve<Fma>(
      parse_instruction("fma.rn.f64 %d0, 0f00000001, 0f80000000, 0f7f800000;"));
  ASSERT_TRUE(widened.has_value()) << widened.error().message;
  const auto* widened_variant = std::get_if<Fma::RnF64>(&widened->variant);
  ASSERT_NE(widened_variant, nullptr);
  EXPECT_EQ(std::get<ResolvedImmediate>(widened_variant->src1.value).bits,
            0x36a0000000000000ULL);
  EXPECT_EQ(std::get<ResolvedImmediate>(widened_variant->src2.value).bits,
            0x8000000000000000ULL);
  EXPECT_EQ(std::get<ResolvedImmediate>(widened_variant->src3.value).bits,
            0x7ff0000000000000ULL);

  for (const auto source : {
           "fma.rn.f16 %h0, 1.0, %h2, %h3;",
           "fma.rn.f16x2 %b0, 1.0, %b2, %b3;",
           "fma.rn.bf16 %b0, 1.0, %b2, %b3;",
           "fma.rn.f32x2 %b0, 1.0, %b2, %b3;",
           "fma.rn.f32.bf16 %f0, 1.0, %b2, %f3;",
           "fma.rn.f32.bf16 %f0, %b1, %b2, 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Fma>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveFma, NarrowsFloatingLiteralsForSinglePrecisionAccumulators) {
  const auto standard = resolve<Fma>(
      parse_instruction("fma.rn.f32 %f0, 1e300, -1e300, 0d7fefffffffffffff;"));
  ASSERT_TRUE(standard.has_value()) << standard.error().message;
  const auto* standard_variant = std::get_if<Fma::RnF32>(&standard->variant);
  ASSERT_NE(standard_variant, nullptr);
  EXPECT_EQ(std::get<ResolvedImmediate>(standard_variant->src1.value).bits,
            0x7f800000U);
  EXPECT_EQ(std::get<ResolvedImmediate>(standard_variant->src2.value).bits,
            0xff800000U);
  EXPECT_EQ(std::get<ResolvedImmediate>(standard_variant->src3.value).bits,
            0x7f800000U);

  const auto mixed =
      resolve<Fma>(parse_instruction("fma.rn.f32.f16 %f0, %h1, %h2, -1e300;"));
  ASSERT_TRUE(mixed.has_value()) << mixed.error().message;
  const auto* mixed_variant = std::get_if<Fma::MixedF32F16>(&mixed->variant);
  ASSERT_NE(mixed_variant, nullptr);
  EXPECT_EQ(std::get<ResolvedImmediate>(mixed_variant->src3.value).bits,
            0xff800000U);

  for (const auto source : {
           "fma.rn.f64 %d0, 1e400, %d2, %d3;",
           "fma.rn.f64 %d0, 1e-400, %d2, %d3;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Fma>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveFma, RejectsMissingRoundingAndConflictingFlags) {
  for (const auto source : {
           "fma.f32 %f0, %f1, %f2, %f3;",
           "fma.f64 %d0, %d1, %d2, %d3;",
           "fma.f32x2 %b0, %b1, %b2, %b3;",
           "fma.f16 %h0, %h1, %h2, %h3;",
           "fma.bf16 %b0, %b1, %b2, %b3;",
           "fma.rz.f16 %h0, %h1, %h2, %h3;",
           "fma.rp.bf16x2 %b0, %b1, %b2, %b3;",
           "fma.f32.f16 %f0, %h1, %h2, %f3;",
           "fma.rn.ftz.f64 %d0, %d1, %d2, %d3;",
           "fma.rn.sat.f32x2 %b0, %b1, %b2, %b3;",
           "fma.rn.sat.relu.f16 %h0, %h1, %h2, %h3;",
           "fma.rn.ftz.oob.f16 %h0, %h1, %h2, %h3;",
           "fma.rn.oob.sat.relu.f16 %h0, %h1, %h2, %h3;",
           "fma.rn.ftz.bf16 %b0, %b1, %b2, %b3;",
           "fma.rn.sat.bf16x2 %b0, %b1, %b2, %b3;",
           "fma.rn.ftz.f32.bf16 %f0, %b1, %b2, %f3;",
       }) {
    const auto selected = selectVariant<Fma>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksEveryGeneratedFmaVariantAvailability) {
  using FmaAvailabilityCase = std::tuple<std::string_view, uint16_t, uint16_t,
                                         uint16_t, uint16_t, uint32_t>;
  constexpr std::array<FmaAvailabilityCase, 16> cases{{
      {"fma.rn.f32 %f0, %f1, %f2, %f3;", 2U, 0U, 1U, 9U, 20U},
      {"fma.rz.ftz.sat.f32 %f0, %f1, %f2, %f3;", 2U, 0U, 1U, 9U, 20U},
      {"fma.rn.f64 %d0, %d1, %d2, %d3;", 1U, 4U, 1U, 3U, 13U},
      {"fma.rp.f64 %d0, %d1, %d2, %d3;", 1U, 4U, 1U, 3U, 13U},
      {"fma.rn.f32x2 %b0, %b1, %b2, %b3;", 8U, 6U, 8U, 5U, 100U},
      {"fma.rn.f16 %h0, %h1, %h2, %h3;", 4U, 2U, 4U, 1U, 53U},
      {"fma.rn.f16x2 %b0, %b1, %b2, %b3;", 4U, 2U, 4U, 1U, 53U},
      {"fma.rn.relu.f16 %h0, %h1, %h2, %h3;", 7U, 0U, 6U, 9U, 80U},
      {"fma.rn.oob.sat.f16 %h0, %h1, %h2, %h3;", 8U, 1U, 8U, 0U, 90U},
      {"fma.rn.oob.relu.f16x2 %b0, %b1, %b2, %b3;", 8U, 1U, 8U, 0U, 90U},
      {"fma.rn.relu.bf16 %b0, %b1, %b2, %b3;", 7U, 0U, 6U, 9U, 80U},
      {"fma.rn.bf16x2 %b0, %b1, %b2, %b3;", 7U, 0U, 6U, 9U, 80U},
      {"fma.rn.oob.bf16 %b0, %b1, %b2, %b3;", 8U, 1U, 8U, 0U, 90U},
      {"fma.rn.oob.relu.bf16x2 %b0, %b1, %b2, %b3;", 8U, 1U, 8U, 0U, 90U},
      {"fma.rn.f32.f16 %f0, %h1, %h2, %f3;", 8U, 6U, 8U, 5U, 100U},
      {"fma.rp.sat.f32.bf16 %f0, %b1, %b2, %f3;", 8U, 6U, 8U, 5U, 100U},
  }};

  for (const auto& [source, ptx_major, ptx_minor, old_ptx_major, old_ptx_minor,
                    sm] : cases) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto fma = resolve<Fma>(*ast);
    ASSERT_TRUE(fma.has_value()) << fma.error().message;

    const auto old_sm =
        check(*fma, Context{.target = {.ptx_version = {ptx_major, ptx_minor},
                                       .sm_version = sm - 1U},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    ASSERT_EQ(old_sm.error().size(), 1U);
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);

    const auto old_ptx = check(
        *fma, Context{.target = {.ptx_version = {old_ptx_major, old_ptx_minor},
                                 .sm_version = sm},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    ASSERT_EQ(old_ptx.error().size(), 1U);
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);

    EXPECT_TRUE(
        check(*fma, Context{.target = {.ptx_version = {ptx_major, ptx_minor},
                                       .sm_version = sm},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
