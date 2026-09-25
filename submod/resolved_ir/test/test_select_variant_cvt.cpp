#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt/resolution.gen.hpp>
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

TEST(ResolveCvt, SelectsFrozenS32U32Variant) {
  const auto ast = parse_instruction("cvt.s32.u32 %s0, %r0;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* cvt = std::get_if<Cvt::S32U32>(&resolved->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::S32U32::dst_type, ScalarType::S32);
  EXPECT_EQ(Cvt::S32U32::src_type, ScalarType::U32);
}

TEST(ResolveCvt, SelectsFrozenRnF32F64Variant) {
  const auto ast = parse_instruction("cvt.rn.f32.f64 %f0, %fd0;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* cvt = std::get_if<Cvt::RnF32F64>(&resolved->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::RnF32F64::rounding, RoundingMode::Rn);
  EXPECT_EQ(Cvt::RnF32F64::dst_type, ScalarType::F32);
  EXPECT_EQ(Cvt::RnF32F64::src_type, ScalarType::F64);
}

TEST(ResolveCvt, SelectsFrozenMixedVariants) {
  const auto to_float =
      resolve<Cvt>(parse_instruction("cvt.rn.f32.u32 %f0, %r0;"));
  ASSERT_TRUE(to_float.has_value()) << to_float.error().message;
  EXPECT_NE(std::get_if<Cvt::RnF32U32>(&to_float->variant), nullptr);

  const auto to_integer =
      resolve<Cvt>(parse_instruction("cvt.rzi.u32.f32 %r0, %f0;"));
  ASSERT_TRUE(to_integer.has_value()) << to_integer.error().message;
  const auto* cvt = std::get_if<Cvt::RziU32F32>(&to_integer->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::RziU32F32::rounding, RoundingMode::Rzi);
  EXPECT_EQ(Cvt::RziU32F32::dst_type, ScalarType::U32);
  EXPECT_EQ(Cvt::RziU32F32::src_type, ScalarType::F32);
}

TEST(ResolveCvt, SelectsM12RnS32AndPackedF16x2Variants) {
  const auto scalar =
      resolve<Cvt>(parse_instruction("cvt.rn.f32.s32 %f0, %r0;"));
  ASSERT_TRUE(scalar.has_value()) << scalar.error().message;
  ASSERT_NE(std::get_if<Cvt::RnF32S32>(&scalar->variant), nullptr);

  const auto packed =
      resolve<Cvt>(parse_instruction("cvt.rn.f16x2.f32 %r0, %f0, %f1;"));
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  ASSERT_NE(std::get_if<Cvt::RnF16x2F32>(&packed->variant), nullptr);
  EXPECT_EQ(Cvt::RnF16x2F32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Cvt::RnF16x2F32::dst_type, ScalarType::F16x2);
  EXPECT_EQ(Cvt::RnF16x2F32::src_type, ScalarType::F32);
}

/** Select scalar syntax independently from cross-field conversion rules. */
TEST(ResolveCvt, SelectsScalarAndPackedFormsButRejectsMissingInputs) {
  for (const auto source :
       {"cvt.rz.f32.s32 %f0, %r0;", "cvt.rn.f32.s16 %f0, %r0;",
        "cvt.rz.f16x2.f32 %r0, %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(selectVariant<Cvt>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Cvt>(parse_instruction("cvt.rn.f16x2.f32 %r0, %f0;"))
                   .has_value());
}

/** Select scalar syntax independently from cross-field conversion rules. */
TEST(ResolveCvt, SelectsOrdinaryFloatSyntaxBeforeRuleChecking) {
  for (const auto source :
       {"cvt.f32.f64 %f0, %fd0;", "cvt.rz.f32.f64 %f0, %fd0;",
        "cvt.rn.f64.f32 %fd0, %f0;"}) {
    const auto selected = selectVariant<Cvt>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_TRUE(selected.has_value());
  }
}

/** Select scalar syntax independently from cross-field conversion rules. */
TEST(ResolveCvt, SelectsMixedSyntaxBeforeRuleChecking) {
  for (const auto source :
       {"cvt.rz.f32.u32 %f0, %r0;", "cvt.rn.u32.f32 %r0, %f0;",
        "cvt.rzi.f32.u32 %f0, %r0;"}) {
    const auto selected = selectVariant<Cvt>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_TRUE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedCvtS32U32Availability) {
  PtxSyntaxParser parser("cvt.s32.u32 %s0, %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected =
      check(*cvt, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*cvt, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtRnF32F64Availability) {
  PtxSyntaxParser parser("cvt.rn.f32.f64 %f0, %fd0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected =
      check(*cvt, Context{.target = {.ptx_version = {1, 0}, .sm_version = 12},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*cvt, Context{.target = {.ptx_version = {1, 0}, .sm_version = 13},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtRziU32F32Availability) {
  PtxSyntaxParser parser("cvt.rzi.u32.f32 %r0, %f0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected =
      check(*cvt, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*cvt, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedM12CvtAvailability) {
  PtxSyntaxParser scalar_parser("cvt.rn.f32.s32 %f0, %r0;");
  const auto scalar_ast = scalar_parser.parseInstruction();
  ASSERT_TRUE(scalar_ast.has_value()) << scalar_ast.diagnostics.front().message;
  const auto scalar = resolve<Cvt>(*scalar_ast);
  ASSERT_TRUE(scalar.has_value()) << scalar.error().message;
  EXPECT_FALSE(
      check(*scalar, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = scalar_ast->range})
          .has_value());
  EXPECT_TRUE(
      check(*scalar, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = scalar_ast->range})
          .has_value());

  PtxSyntaxParser packed_parser("cvt.rn.f16x2.f32 %r0, %f0, %f1;");
  const auto packed_ast = packed_parser.parseInstruction();
  ASSERT_TRUE(packed_ast.has_value()) << packed_ast.diagnostics.front().message;
  const auto packed = resolve<Cvt>(*packed_ast);
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  EXPECT_FALSE(check(*packed, Context{.target = {.ptx_version = {6, 9},
                                                 .sm_version = 80},
                                      .instruction_range = packed_ast->range})
                   .has_value());
  EXPECT_FALSE(check(*packed, Context{.target = {.ptx_version = {7, 0},
                                                 .sm_version = 79},
                                      .instruction_range = packed_ast->range})
                   .has_value());
  EXPECT_TRUE(check(*packed,
                    Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                            .instruction_range = packed_ast->range})
                  .has_value());
}

/** `cvt.pack` enforces base and sub-byte type-value target boundaries. */
TEST(ResolvedIrChecker, ChecksGeneratedCvtPackAvailability) {
  for (const auto source : {"cvt.pack.sat.u16.s32 %r0, %r1, %r2;",
                            "cvt.pack.sat.s8.s32.b32 %r0, %r1, %r2, 0;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto cvt = resolve<Cvt>(*ast);
    ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
    EXPECT_FALSE(
        check(*cvt, Context{.target = {.ptx_version = {6, 4}, .sm_version = 72},
                            .instruction_range = ast->range})
            .has_value());
    EXPECT_FALSE(
        check(*cvt, Context{.target = {.ptx_version = {6, 5}, .sm_version = 71},
                            .instruction_range = ast->range})
            .has_value());
    EXPECT_TRUE(
        check(*cvt, Context{.target = {.ptx_version = {6, 5}, .sm_version = 72},
                            .instruction_range = ast->range})
            .has_value());
  }

  PtxSyntaxParser parser("cvt.pack.sat.u4.s32.b32 %r0, %r1, %r2, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  EXPECT_FALSE(
      check(*cvt, Context{.target = {.ptx_version = {6, 5}, .sm_version = 74},
                          .instruction_range = ast->range})
          .has_value());
  EXPECT_TRUE(
      check(*cvt, Context{.target = {.ptx_version = {6, 5}, .sm_version = 75},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
