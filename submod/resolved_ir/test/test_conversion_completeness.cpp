#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

/** Parse, resolve, and target-check one complete module expected to be valid. */
void expectModuleAccepted(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  const auto validation = validateModule(*module);
  ASSERT_TRUE(validation.has_value()) << validation.error().front().message;
}

/** Parse a complete module whose conversion form must fail variant selection. */
void expectModuleResolutionRejected(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveModuleOnly(*ast).has_value());
}

/** Parse and resolve a module whose selected form must fail target or type checks. */
void expectModuleValidationRejected(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  EXPECT_FALSE(validateModule(*module).has_value());
}

/** Check that a complete module fails validation with the selected diagnostic. */
void expectModuleValidationDiagnostic(
    std::string_view source, checker::CheckDiagnosticKind diagnostic_kind) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  const auto validation = validateModule(*module);
  ASSERT_FALSE(validation.has_value());
  ASSERT_FALSE(validation.error().empty());
  EXPECT_EQ(validation.error().front().kind, diagnostic_kind);
}

/** Parse and resolve a module whose selected form must violate an instruction rule. */
void expectModuleRuleViolation(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  const auto validation = validateModule(*module);
  ASSERT_FALSE(validation.has_value());
  ASSERT_FALSE(validation.error().empty());
  EXPECT_EQ(validation.error().front().kind,
            checker::CheckDiagnosticKind::RuleViolation);
}

/** Describes one scalar type and the physical register that represents it. */
struct ScalarConversionType {
  /** PTX scalar type token used by the conversion instruction. */
  std::string_view ptx_type;
  /** Physical register type that can hold the scalar conversion operand. */
  std::string_view register_type;
  /** Unique destination register name for this scalar type. */
  std::string_view destination_register;
  /** Unique source register name for this scalar type. */
  std::string_view source_register;
  /** Whether the scalar type belongs to PTX's floating-point category. */
  bool is_float;
  /** Logical scalar width in bits, used to select float narrowing rounding. */
  unsigned width_bits;
};

/** The PTX scalar types supported by ordinary `cvt` instructions. */
constexpr ScalarConversionType kOrdinaryScalarConversionTypes[] = {
    {"u8", "u16", "dst_u8", "src_u8", false, 8},
    {"s8", "s16", "dst_s8", "src_s8", false, 8},
    {"u16", "u16", "dst_u16", "src_u16", false, 16},
    {"s16", "s16", "dst_s16", "src_s16", false, 16},
    {"u32", "u32", "dst_u32", "src_u32", false, 32},
    {"s32", "s32", "dst_s32", "src_s32", false, 32},
    {"u64", "u64", "dst_u64", "src_u64", false, 64},
    {"s64", "s64", "dst_s64", "src_s64", false, 64},
    {"f16", "f16", "dst_f16", "src_f16", true, 16},
    {"bf16", "b16", "dst_bf16", "src_bf16", true, 16},
    {"f32", "f32", "dst_f32", "src_f32", true, 32},
    {"f64", "f64", "dst_f64", "src_f64", true, 64},
};

/** Return the minimum legal rounding modifier for one ordinary scalar pair. */
std::string_view ordinaryCvtModifier(const ScalarConversionType& destination,
                                     const ScalarConversionType& source) {
  if (!destination.is_float && !source.is_float) {
    return {};
  }
  if (destination.is_float && !source.is_float) {
    return "rn";
  }
  if (!destination.is_float && source.is_float) {
    return "rzi";
  }
  if ((destination.ptx_type == "f16" && source.ptx_type == "bf16") ||
      (destination.ptx_type == "bf16" && source.ptx_type == "f16")) {
    return {};
  }
  return destination.width_bits < source.width_bits ? "rn" : "";
}

/** Build a complete module containing every ordinary scalar conversion pair. */
std::string buildOrdinaryScalarConversionModule() {
  std::string module = ".version 9.3\n.target sm_90\n.entry kernel() {\n";
  for (const ScalarConversionType& type : kOrdinaryScalarConversionTypes) {
    module += "  .reg .";
    module += type.register_type;
    module += " %";
    module += type.destination_register;
    module += ", %";
    module += type.source_register;
    module += ";\n";
  }
  for (const ScalarConversionType& destination :
       kOrdinaryScalarConversionTypes) {
    for (const ScalarConversionType& source : kOrdinaryScalarConversionTypes) {
      module += "  cvt";
      if (const std::string_view modifier =
              ordinaryCvtModifier(destination, source);
          !modifier.empty()) {
        module += ".";
        module += modifier;
      }
      module += ".";
      module += destination.ptx_type;
      module += ".";
      module += source.ptx_type;
      module += " %";
      module += destination.destination_register;
      module += ", %";
      module += source.source_register;
      module += ";\n";
    }
  }
  module += "}\n";
  return module;
}

/** Cover explicit CVTA spaces and declaration-bound symbol address sources. */
TEST(ConversionCompleteness, ResolvesCvtaExplicitSpacesAndSymbolAddresses) {
  expectModuleAccepted(R"ptx(
.version 8.3
.target sm_90
.global .align 8 .u64 global_value;
.const .align 8 .u64 constant_value;
.shared .align 8 .u64 shared_value;
.entry kernel(.param .u64 parameter) {
  .reg .u32 %r<3>;
  .reg .u64 %rd<4>;
  cvta.global.u64 %rd0, global_value+8;
  cvta.const.u64 %rd0, constant_value;
  cvta.local.u32 %r0, %r1;
  cvta.shared::cta.u64 %rd0, shared_value+8;
  cvta.shared::cluster.u64 %rd1, shared_value;
  cvta.param.u64 %rd2, parameter;
  cvta.param::entry.u64 %rd2, parameter+8;
  cvta.to.shared::cta.u32 %r0, %r1;
  cvta.to.shared::cluster.u64 %rd0, %rd1;
  cvta.to.param::entry.u64 %rd0, %rd1;
}
)ptx");
}

/** Reject CVTA symbol sources whose declared state space disagrees with the modifier. */
TEST(ConversionCompleteness, RejectsCvtaWrongSymbolStateSpace) {
  expectModuleValidationDiagnostic(
      R"ptx(
.version 8.3
.target sm_90
.global .u32 global_value;
.entry kernel() {
  .reg .u64 %rd0;
  cvta.shared::cta.u64 %rd0, global_value;
}
)ptx",
      checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  expectModuleValidationDiagnostic(
      R"ptx(
.version 8.3
.target sm_70
.func device(.param .u32 formal) {
  .reg .u64 %rd0;
  cvta.param::entry.u64 %rd0, formal;
}
)ptx",
      checker::CheckDiagnosticKind::ParameterQualifierMismatch);
  expectModuleValidationDiagnostic(
      R"ptx(
.version 8.3
.target sm_70
.func device(.param .u32 formal) {
  .reg .u64 %rd0;
  cvta.param.u64 %rd0, formal+4;
}
)ptx",
      checker::CheckDiagnosticKind::ParameterQualifierMismatch);
  expectModuleValidationDiagnostic(
      R"ptx(
.version 8.3
.target sm_70
.func device(.param .u32 formal) {
  .reg .u64 %rd0;
  cvta.local.u64 %rd0, formal;
}
)ptx",
      checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

/** Preserve CVTA's equal-width register contract and register-only `.to` source. */
TEST(ConversionCompleteness, EnforcesCvtaWidthAndToSourceContracts) {
  expectModuleValidationRejected(R"ptx(
.version 7.8
.target sm_30
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  cvta.shared::cta.u32 %r0, %rd0;
}
)ptx");
  expectModuleResolutionRejected(R"ptx(
.version 7.8
.target sm_30
.shared .u32 shared_value;
.entry kernel() {
  .reg .u32 %r0;
  cvta.to.shared::cta.u32 %r0, shared_value;
}
)ptx");
}

/** Enforce explicit CVTA sub-qualifier version and target availability. */
TEST(ConversionCompleteness, EnforcesCvtaExplicitSpaceAvailability) {
  for (
      const std::string_view source : {
          R"ptx(.version 7.7 .target sm_30 .entry kernel() { .reg .u32 %r<2>; cvta.shared::cta.u32 %r0, %r1; })ptx",
          R"ptx(.version 7.8 .target sm_20 .entry kernel() { .reg .u32 %r<2>; cvta.to.shared::cta.u32 %r0, %r1; })ptx",
          R"ptx(.version 7.8 .target sm_80 .entry kernel() { .reg .u64 %rd<2>; cvta.shared::cluster.u64 %rd0, %rd1; })ptx",
          R"ptx(.version 8.2 .target sm_70 .entry kernel() { .reg .u32 %r<2>; cvta.to.param::entry.u32 %r0, %r1; })ptx",
          R"ptx(.version 8.3 .target sm_60 .entry kernel() { .reg .u64 %rd<2>; cvta.param::entry.u64 %rd0, %rd1; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleValidationRejected(source);
  }
  for (
      const std::string_view source : {
          R"ptx(.version 7.8 .target sm_30 .entry kernel() { .reg .u32 %r<2>; cvta.shared::cta.u32 %r0, %r1; })ptx",
          R"ptx(.version 7.8 .target sm_90 .entry kernel() { .reg .u64 %rd<2>; cvta.shared::cluster.u64 %rd0, %rd1; })ptx",
          R"ptx(.version 8.3 .target sm_70 .entry kernel() { .reg .u64 %rd<2>; cvta.param::entry.u64 %rd0, %rd1; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleAccepted(source);
  }
}

/** Exercise ordinary integer and floating conversion families in one module. */
TEST(ConversionCompleteness, ResolvesOrdinaryTypesRoundingAndSaturation) {
  expectModuleAccepted(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .u32 %r<4>;
  .reg .s32 %s<4>;
  .reg .f32 %f<4>;
  .reg .f64 %fd0;
  cvt.s32.u32 %s0, %r0;
  cvt.sat.u8.s32 %r1, %s0;
  cvt.rn.f32.u32 %f0, %r0;
  cvt.rp.f32.s32 %f1, %s0;
  cvt.rni.f32.f32 %f2, %f1;
  cvt.rzi.s32.f32 %s1, %f2;
  cvt.rmi.u8.f32 %r2, %f2;
  cvt.rpi.s8.f32 %s2, %f2;
  cvt.rn.f32.f64 %f3, %fd0;
}
)ptx");
}

/** Retained scalar variants keep their legal FTZ and saturation modifiers. */
TEST(ConversionCompleteness, ResolvesRetainedScalarVariantModifiers) {
  expectModuleAccepted(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .u32 %r0;
  .reg .s32 %s0;
  .reg .f32 %f0;
  .reg .f64 %fd0;
  cvt.sat.s32.u32 %s0, %r0;
  cvt.rn.ftz.sat.f32.f64 %f0, %fd0;
  cvt.rn.ftz.sat.f32.u32 %f0, %r0;
  cvt.rn.ftz.sat.f32.s32 %f0, %s0;
  cvt.rzi.ftz.sat.u32.f32 %r0, %f0;
}
)ptx");
}

/** Distinguish signed and unsigned integer ranges when `.sat` is selected. */
TEST(ConversionCompleteness, ChecksIntegerSaturationRangeContainment) {
  expectModuleAccepted(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .u64 %unsigned_destination;
  .reg .s32 %signed_source;
  cvt.sat.u64.s32 %unsigned_destination, %signed_source;
}
)ptx");
  expectModuleRuleViolation(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .s64 %signed_destination;
  .reg .u32 %unsigned_source;
  cvt.sat.s64.u32 %signed_destination, %unsigned_source;
}
)ptx");
}

/** Resolve every specification-defined ordinary scalar source/destination pair. */
TEST(ConversionCompleteness, ResolvesCompleteOrdinaryScalarMatrix) {
  expectModuleAccepted(buildOrdinaryScalarConversionModule());
}

/** Accept scalar immediate sources and wider physical integer registers. */
TEST(ConversionCompleteness,
     ResolvesOrdinaryImmediateAndWiderRegisterOperands) {
  expectModuleAccepted(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .f32 %float_destination;
  .reg .s32 %integer_destination;
  .reg .s64 %wide_integer_destination;
  .reg .u64 %wide_integer_source;
  cvt.rn.f32.s16 %float_destination, 7;
  cvt.rn.f32.s32 %float_destination, 7;
  cvt.rzi.s32.f32 %integer_destination, 1.0;
  cvt.s32.u32 %wide_integer_destination, %wide_integer_source;
}
)ptx");
}

/** Keep f16/bf16 directed-rounding forms distinct from integer-rounding forms. */
TEST(ConversionCompleteness, ResolvesF16Bf16DirectedRoundingForms) {
  expectModuleAccepted(R"ptx(
.version 7.8
.target sm_90
.entry kernel() {
  .reg .f16 %h<8>;
  .reg .b16 %b<8>;
  cvt.bf16.f16 %b0, %h0;
  cvt.rn.bf16.f16 %b1, %h1;
  cvt.rz.bf16.f16 %b2, %h2;
  cvt.rm.bf16.f16 %b3, %h3;
  cvt.rp.bf16.f16 %b4, %h4;
  cvt.f16.bf16 %h0, %b0;
  cvt.rn.f16.bf16 %h1, %b1;
  cvt.rz.f16.bf16 %h2, %b2;
  cvt.rm.f16.bf16 %h3, %b3;
  cvt.rp.f16.bf16 %h4, %b4;
}
)ptx");
}

/** Cover scalar and two-input packed half, bfloat, and TF32 syntax families. */
TEST(ConversionCompleteness, ResolvesPackedHalfBfloatAndTf32Families) {
  expectModuleAccepted(R"ptx(
.version 8.1
.target sm_80
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .f16 %h0;
  .reg .b16 %b0;
  .reg .b32 %p0;
  cvt.rn.satfinite.f16.f32 %h0, %f0;
  cvt.rz.f16x2.f32 %p0, %f0, %f1;
  cvt.rn.satfinite.bf16.f32 %b0, %f0;
  cvt.rz.satfinite.bf16x2.f32 %p0, %f0, %f1;
  cvt.rna.tf32.f32 %p0, %f0;
}
)ptx");
}

/** Cover the FP8 x2 and stochastic x4 conversion families. */
TEST(ConversionCompleteness, ResolvesFp8X2AndX4Families) {
  expectModuleAccepted(R"ptx(
.version 8.7
.target sm_100a
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .b16 %b16<2>;
  .reg .b32 %b32<2>;
  cvt.rn.satfinite.e4m3x2.f32 %b160, %f0, %f1;
  cvt.rn.satfinite.e5m2x2.f32 %b161, %f2, %f3;
  cvt.rn.f16x2.e4m3x2 %b320, %b160;
  cvt.rs.satfinite.e4m3x4.f32 %b321, {%f0, %f1, %f2, %f3}, %b320;
}
)ptx");
}

/** Cover FP4, FP6, and unsigned-exponent packed conversion representatives. */
TEST(ConversionCompleteness, ResolvesLowBitAndUnsignedExponentFamilies) {
  expectModuleAccepted(R"ptx(
.version 8.7
.target sm_100a
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .b8 %b8<2>;
  .reg .b16 %b16<3>;
  .reg .b32 %b32<4>;
  cvt.rn.satfinite.e2m1x2.f32 %b80, %f0, %f1;
  cvt.rn.f16x2.e2m1x2 %b320, %b80;
  cvt.rs.satfinite.e2m1x4.f32 %b160, {%f0, %f1, %f2, %f3}, %b321;
  cvt.rn.satfinite.e2m3x2.f32 %b161, %f0, %f1;
  cvt.rn.f16x2.e2m3x2 %b322, %b161;
  cvt.rs.satfinite.e3m2x4.f32 %b323, {%f0, %f1, %f2, %f3}, %b321;
  cvt.rz.satfinite.ue8m0x2.f32 %b162, %f0, %f1;
  cvt.rp.satfinite.ue8m0x2.bf16x2 %b162, %b320;
  cvt.rn.bf16x2.ue8m0x2 %b320, %b162;
}
)ptx");
}

/** Cover the PTX 9.2 family-target scaled low-bit to bf16x2 form. */
TEST(ConversionCompleteness, ResolvesScaledFamilyTargetBfloatExpansion) {
  expectModuleAccepted(R"ptx(
.version 9.2
.target sm_100f
.entry kernel() {
  .reg .b16 %b16<3>;
  .reg .b32 %b32<2>;
  cvt.rn.scaled::n2::ue8m0.bf16x2.e4m3x2 %b320, %b160, %b161;
  cvt.rn.satfinite.scaled::n2::ue8m0.bf16x2.e2m1x2 %b321, %b162, %b161;
}
)ptx");
}

/** Cover every direction involving the exact-target s2f6x2 instruction type. */
TEST(ConversionCompleteness, ResolvesS2f6X2FamiliesOnExactTargets) {
  expectModuleAccepted(R"ptx(
.version 9.1
.target sm_121a
.entry kernel() {
  .reg .f32 %f<2>;
  .reg .b16 %b16<3>;
  .reg .b32 %b32<3>;
  cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 %b160, %f0, %f1, %b161;
  cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.bf16x2 %b162, %b320, %b161;
  cvt.rn.satfinite.scaled::n2::ue8m0.bf16x2.s2f6x2 %b321, %b160, %b161;
}
)ptx");
}

/** Reject ordinary modifier combinations through the conversion rule hook. */
TEST(ConversionCompleteness, RejectsIllegalOrdinaryModifiers) {
  for (
      const std::string_view source : {
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .u32 %r0; .reg .f32 %f0; cvt.f32.u32 %f0, %r0; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .s32 %s0; .reg .f32 %f0; cvt.s32.f32 %s0, %f0; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .u32 %r0; .reg .f32 %f0; cvt.rni.f32.u32 %f0, %r0; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .u32 %r0, %r1; cvt.rn.s32.u32 %r0, %r1; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .f16 %h0, %h1; cvt.rn.ftz.f16.f16 %h0, %h1; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .f16 %h0; .reg .b16 %b0; cvt.rni.bf16.f16 %b0, %h0; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .f32 %f; .reg .f64 %d; cvt.f32.f64 %f, %d; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .f32 %f; .reg .f64 %d; cvt.rn.f64.f32 %d, %f; })ptx",
          R"ptx(.version 9.3 .target sm_90 .entry kernel() { .reg .f32 %f; .reg .u32 %r; cvt.rn.u32.f32 %r, %f; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleRuleViolation(source);
  }
}

/** Reject incomplete mandatory-modifier and modern operand-layout combinations. */
TEST(ConversionCompleteness, RejectsModernModifierAndOperandFailures) {
  for (
      const std::string_view source : {
          R"ptx(.entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.e4m3x2.f32 %b0, %f0, %f1; })ptx",
          R"ptx(.entry kernel() { .reg .f32 %f0, %f1, %f2; .reg .b32 %d0, %r0; cvt.rs.satfinite.e4m3x4.f32 %d0, {%f0, %f1, %f2}, %r0; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleResolutionRejected(source);
  }

  expectModuleValidationRejected(R"ptx(
.version 8.7
.target sm_100a
.entry kernel() {
  .reg .f32 %f<4>;
  .reg .b32 %d0;
  .reg .b64 %rbits;
  cvt.rs.satfinite.e4m3x4.f32 %d0, {%f0, %f1, %f2, %f3}, %rbits;
}
)ptx");
}

/** Enforce physical register widths that PTX forbids from widening. */
TEST(ConversionCompleteness, RejectsWiderBfloatAndTf32Containers) {
  for (
      const std::string_view source : {
          R"ptx(.version 7.8 .target sm_90 .entry kernel() { .reg .f32 %f0; .reg .b32 %wide; cvt.rn.bf16.f32 %wide, %f0; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleValidationRejected(source);
  }
  expectModuleValidationRejected(
      R"ptx(.version 8.6 .target sm_100 .entry kernel() { .reg .f32 %f0; .reg .b64 %wide; cvt.rn.satfinite.tf32.f32 %wide, %f0; })ptx");
}

/** Distinguish the PTX 7.8/SM90 and PTX 8.1/SM89 FP8 availability paths. */
TEST(ConversionCompleteness, EnforcesFp8VersionAndTargetBoundaries) {
  expectModuleAccepted(R"ptx(
.version 7.8
.target sm_90
.entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.satfinite.e4m3x2.f32 %b0, %f0, %f1; }
)ptx");
  expectModuleAccepted(R"ptx(
.version 8.1
.target sm_89
.entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.satfinite.e4m3x2.f32 %b0, %f0, %f1; }
)ptx");

  for (
      const std::string_view source : {
          R"ptx(.version 8.0 .target sm_89 .entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.satfinite.e4m3x2.f32 %b0, %f0, %f1; })ptx",
          R"ptx(.version 7.8 .target sm_89 .entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.satfinite.e4m3x2.f32 %b0, %f0, %f1; })ptx",
      }) {
    SCOPED_TRACE(source);
    expectModuleValidationRejected(source);
  }
}

/** Keep packed FP8 f16x2 and bf16x2 source availability independent. */
TEST(ConversionCompleteness, EnforcesPackedFp8SourceAvailability) {
  for (const std::string_view header : {
           ".version 7.8 .target sm_90",
           ".version 8.1 .target sm_89",
       }) {
    const std::string body = std::string(header) + R"ptx( .entry kernel() {
  .reg .b16 %dst0, %dst1;
  .reg .b32 %src;
  cvt.rn.satfinite.e4m3x2.f16x2 %dst0, %src;
  cvt.rn.satfinite.e5m2x2.f16x2 %dst1, %src;
})ptx";
    expectModuleAccepted(body);
  }
  for (const std::string_view header : {
           ".version 7.7 .target sm_90",
           ".version 8.0 .target sm_89",
       }) {
    const std::string body = std::string(header) + R"ptx( .entry kernel() {
  .reg .b16 %dst;
  .reg .b32 %src;
  cvt.rn.satfinite.e4m3x2.f16x2 %dst, %src;
})ptx";
    expectModuleValidationRejected(body);
  }
  expectModuleAccepted(R"ptx(
.version 9.1
.target sm_100f
.entry kernel() {
  .reg .b16 %dst0, %dst1;
  .reg .b32 %src;
  cvt.rn.satfinite.e4m3x2.bf16x2 %dst0, %src;
  cvt.rn.satfinite.e5m2x2.bf16x2 %dst1, %src;
}
)ptx");
  for (const std::string_view header : {
           ".version 9.0 .target sm_100f",
           ".version 9.1 .target sm_100",
           ".version 9.1 .target sm_90",
       }) {
    const std::string body = std::string(header) + R"ptx( .entry kernel() {
  .reg .b16 %dst0, %dst1;
  .reg .b32 %src;
  cvt.rn.satfinite.e4m3x2.bf16x2 %dst0, %src;
  cvt.rn.satfinite.e5m2x2.bf16x2 %dst1, %src;
})ptx";
    expectModuleValidationRejected(body);
  }
}

/** Preserve family and exact-target availability distinctions for new formats. */
TEST(ConversionCompleteness, EnforcesFamilyAndExactTargetBoundaries) {
  expectModuleAccepted(R"ptx(
.version 8.8
.target sm_100f
.entry kernel() { .reg .f32 %f0, %f1; .reg .b8 %b0; cvt.rn.satfinite.e2m1x2.f32 %b0, %f0, %f1; }
)ptx");
  expectModuleAccepted(R"ptx(
.version 8.6
.target sm_100a
.entry kernel() { .reg .f32 %f0, %f1; .reg .b8 %b0; cvt.rn.satfinite.e2m1x2.f32 %b0, %f0, %f1; }
)ptx");
  expectModuleValidationRejected(R"ptx(
.version 8.7
.target sm_100f
.entry kernel() { .reg .f32 %f0, %f1; .reg .b8 %b0; cvt.rn.satfinite.e2m1x2.f32 %b0, %f0, %f1; }
)ptx");
  expectModuleValidationRejected(R"ptx(
.version 8.6
.target sm_100
.entry kernel() { .reg .f32 %f0, %f1; .reg .b8 %b0; cvt.rn.satfinite.e2m1x2.f32 %b0, %f0, %f1; }
)ptx");
  expectModuleValidationRejected(R"ptx(
.version 9.1
.target sm_121
.entry kernel() { .reg .f32 %f0, %f1; .reg .b16 %b0; cvt.rn.satfinite.s2f6x2.f32 %b0, %f0, %f1; }
)ptx");
}

/** Require the scale qualifier and extra operand to agree after resolution. */
TEST(ConversionCompleteness, ChecksScaleQualifierAgainstOwnedLayout) {
  expectModuleRuleViolation(R"ptx(.version 9.3 .target sm_100a .entry kernel() {
    .reg .b16 %b0; .reg .b32 %d0;
    cvt.rn.scaled::n2::ue8m0.bf16x2.e4m3x2 %d0, %b0;
  })ptx");
  expectModuleRuleViolation(R"ptx(.version 9.3 .target sm_100a .entry kernel() {
    .reg .b16 %b0, %b1; .reg .b32 %d0;
    cvt.rn.bf16x2.e4m3x2 %d0, %b0, %b1;
  })ptx");
}

/** Check flag-specific introduction boundaries independently from base forms. */
TEST(ConversionCompleteness, ChecksFiniteSaturationIntroduction) {
  for (const std::string_view instruction :
       {"cvt.rn.satfinite.f16x2.f32 %r, %f, 1.0;",
        "cvt.rz.satfinite.f16x2.f32 %r, %f, 1.0;",
        "cvt.rn.satfinite.bf16x2.f32 %r, %f, 1.0;",
        "cvt.rna.satfinite.tf32.f32 %r, %f;"}) {
    const std::string body =
        ".target sm_80 .entry k() { .reg .b32 %r; .reg .f32 %f; " +
        std::string(instruction) + " }";
    SCOPED_TRACE(instruction);
    expectModuleValidationRejected(".version 8.0 " + body);
    expectModuleAccepted(".version 8.1 " + body);
  }
  expectModuleValidationRejected(R"ptx(.version 8.5 .target sm_100 .entry k() {
    .reg .b32 %r; .reg .f32 %f; cvt.rn.satfinite.tf32.f32 %r, %f;
  })ptx");
  expectModuleValidationRejected(R"ptx(.version 8.6 .target sm_90 .entry k() {
    .reg .b32 %r; .reg .f32 %f; cvt.rz.satfinite.tf32.f32 %r, %f;
  })ptx");
  expectModuleAccepted(R"ptx(.version 8.6 .target sm_100 .entry k() {
    .reg .b32 %r; .reg .f32 %f; cvt.rn.satfinite.tf32.f32 %r, %f;
  })ptx");
}

/** Exercise the general scalar modifiers and modern relaxed register widths. */
TEST(ConversionCompleteness, AcceptsScalarFlagsAndModernContainers) {
  expectModuleAccepted(R"ptx(.version 9.3 .target sm_100a .entry k() {
    .reg .b16 %h; .reg .b32 %r; .reg .b64 %wide; .reg .f32 %f;
    cvt.rn.ftz.sat.f16.f32 %h, %f;
    cvt.rz.ftz.sat.f16.f32 %h, %f;
    cvt.rm.ftz.sat.f16.f32 %h, %f;
    cvt.rp.ftz.sat.f16.f32 %h, %f;
    cvt.rm.ftz.bf16.f32 %h, %f;
    cvt.rp.ftz.bf16.f32 %h, %f;
    cvt.rn.relu.f16x2.f32 %wide, %f, 1.0;
    cvt.rs.f16x2.f32 %wide, 1.0, %f, %r;
    cvt.rs.bf16x2.f32 %r, 1.0, %f, %r;
    cvt.rn.satfinite.e4m3x2.f16x2 %wide, %wide;
    cvt.rn.f16x2.e4m3x2 %wide, %wide;
  })ptx");
}

/** Keep FP4's b8 source distinct from FP6's b16 minimum, including scaling. */
TEST(ConversionCompleteness, DistinguishesFp4AndFp6SourceContainers) {
  expectModuleAccepted(R"ptx(.version 9.3 .target sm_121a .entry k() {
    .reg .b8 %small; .reg .b16 %scale; .reg .b32 %r;
    cvt.rn.f16x2.e2m1x2 %r, %small;
    cvt.rn.bf16x2.e2m1x2 %r, %small;
    cvt.rn.scaled::n2::ue8m0.bf16x2.e2m1x2 %r, %small, %scale;
  })ptx");
  expectModuleValidationRejected(R"ptx(.version 9.3 .target sm_121a .entry k() {
    .reg .b8 %small; .reg .b32 %r;
    cvt.rn.f16x2.e2m3x2 %r, %small;
  })ptx");
}

/** Use FP4's b8 destination without weakening FP6's b16 contract. */
TEST(ConversionCompleteness, EnforcesPackedFp4AndFp6DestinationWidths) {
  expectModuleAccepted(R"ptx(
.version 9.1
.target sm_100f
.entry kernel() {
  .reg .b8 %fp4_f16, %fp4_bf16;
  .reg .b16 %fp4_wide, %fp6_f16, %fp6_bf16;
  .reg .b32 %f16_src, %bf16_src;
  .reg .b64 %wide_f16_src;
  cvt.rn.satfinite.e2m1x2.f16x2 %fp4_f16, %wide_f16_src;
  cvt.rn.satfinite.e2m1x2.bf16x2 %fp4_bf16, %bf16_src;
  cvt.rn.satfinite.e2m1x2.bf16x2 %fp4_wide, %bf16_src;
  cvt.rn.satfinite.e2m3x2.f16x2 %fp6_f16, %f16_src;
  cvt.rn.satfinite.e3m2x2.bf16x2 %fp6_bf16, %bf16_src;
}
)ptx");
  expectModuleValidationRejected(R"ptx(
.version 9.1
.target sm_100f
.entry kernel() {
  .reg .b8 %fp6;
  .reg .b32 %src;
  cvt.rn.satfinite.e2m3x2.f16x2 %fp6, %src;
}
)ptx");
  expectModuleValidationRejected(R"ptx(
.version 9.1
.target sm_100f
.entry kernel() {
  .reg .b8 %fp4;
  .reg .b64 %wide_bf16_src;
  cvt.rn.satfinite.e2m1x2.bf16x2 %fp4, %wide_bf16_src;
}
)ptx");
}

/** Distinguish exact architecture introductions from later family spellings. */
TEST(ConversionCompleteness, ChecksLowbitArchitectureAndFamilyIntroductions) {
  const std::string body = R"ptx(.entry k() {
    .reg .b8 %d; .reg .f32 %f;
    cvt.rn.satfinite.e2m1x2.f32 %d, %f, %f;
  })ptx";
  expectModuleAccepted(".version 8.6 .target sm_120a " + body);
  expectModuleAccepted(".version 9.0 .target sm_110a " + body);
  expectModuleValidationRejected(".version 8.9 .target sm_110a " + body);
  expectModuleAccepted(".version 8.8 .target sm_120f " + body);
  expectModuleValidationRejected(".version 8.7 .target sm_120f " + body);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
