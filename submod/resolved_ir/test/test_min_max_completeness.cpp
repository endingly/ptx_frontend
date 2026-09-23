#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve every floating MIN/MAX cohort, selecting layouts purely by arity. */
TEST(MinMaxCompleteness, ResolvesBinaryAndTernaryCohortsByArity) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<8>; .reg .f64 %d<4>; .reg .f16 %h<4>; .reg .f16x2 %hx<4>;
  .reg .b16 %b<4>; .reg .b32 %r<4>;
  min.f32 %f0, %f1, %f2;
  min.NaN.f32 %f0, %f1, %f2;
  min.ftz.NaN.xorsign.abs.f32 %f0, %f1, %f2;
  min.abs.f32 %f0, %f1, %f2, %f3;
  min.ftz.NaN.abs.f32 %f0, %f1, %f2, %f3;
  min.f64 %d0, %d1, %d2;
  min.f16 %h0, %h1, %h2;
  min.f16x2 %hx0, %hx1, %hx2;
  min.bf16 %b0, %b1, %b2;
  min.bf16x2 %r0, %r1, %r2;
  max.f32 %f0, %f1, %f2;
  max.xorsign.abs.f32 %f0, %f1, %f2;
  max.abs.f32 %f0, %f1, %f2, %f3;
  max.bf16x2 %r0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 14u);

  const auto& binary = std::get<Min::F32>(std::get<Min>(body[0]).variant);
  EXPECT_EQ(binary.operand_layout, (ResolvedOperandLayoutTag{0}));
  EXPECT_FALSE(binary.ftz.value);
  EXPECT_FALSE(binary.nan.value);
  EXPECT_FALSE(binary.xorsign_abs.value);
  EXPECT_TRUE(
      std::holds_alternative<Min::F32::BinaryOperands>(binary.operands));

  const auto& nan = std::get<Min::F32>(std::get<Min>(body[1]).variant);
  EXPECT_TRUE(nan.nan.value);
  EXPECT_FALSE(nan.xorsign_abs.value);

  const auto& paired = std::get<Min::F32>(std::get<Min>(body[2]).variant);
  EXPECT_TRUE(paired.ftz.value);
  EXPECT_TRUE(paired.nan.value);
  EXPECT_TRUE(paired.xorsign_abs.value);
  EXPECT_FALSE(paired.abs.value);
  EXPECT_EQ(paired.operand_layout, (ResolvedOperandLayoutTag{0}));

  const auto& ternary = std::get<Min::F32>(std::get<Min>(body[3]).variant);
  EXPECT_EQ(ternary.operand_layout, (ResolvedOperandLayoutTag{1}));
  EXPECT_TRUE(ternary.abs.value);
  EXPECT_FALSE(ternary.xorsign_abs.value);
  EXPECT_TRUE(
      std::holds_alternative<Min::F32::TernaryOperands>(ternary.operands));

  const auto& ternary_nan = std::get<Min::F32>(std::get<Min>(body[4]).variant);
  EXPECT_TRUE(ternary_nan.abs.value);
  EXPECT_TRUE(ternary_nan.nan.value);
  EXPECT_EQ(ternary_nan.operand_layout, (ResolvedOperandLayoutTag{1}));

  EXPECT_EQ(Min::F64::type, ScalarType::F64);
  EXPECT_EQ(Min::F16::type, ScalarType::F16);
  EXPECT_EQ(Min::F16x2::type, ScalarType::F16x2);
  EXPECT_EQ(Min::Bf16::type, ScalarType::BF16);
  EXPECT_EQ(Min::Bf16x2::type, ScalarType::BF16x2);

  const auto& max_paired = std::get<Max::F32>(std::get<Max>(body[11]).variant);
  EXPECT_TRUE(max_paired.xorsign_abs.value);
  EXPECT_EQ(max_paired.operand_layout, (ResolvedOperandLayoutTag{0}));
  const auto& max_ternary = std::get<Max::F32>(std::get<Max>(body[12]).variant);
  EXPECT_TRUE(max_ternary.abs.value);
  EXPECT_EQ(max_ternary.operand_layout, (ResolvedOperandLayoutTag{1}));

  // Pin the generated descriptor's per-layout rejection sets. Slots are
  // addressed by their variant-local index: ftz=0, nan=1, xorsign_abs=2,
  // abs=3, type=4.
  const auto& f32 = Min::get_checker_descriptor().variants[1];
  ASSERT_EQ(f32.operand_layouts.size(), 2u);
  EXPECT_EQ(f32.operand_layouts[0].layout_name, "binary");
  EXPECT_EQ(f32.operand_layouts[1].layout_name, "ternary");
  ASSERT_EQ(f32.operand_layouts[0].forbidden_modifiers.size(), 1u);
  EXPECT_EQ(f32.operand_layouts[0].forbidden_modifiers.front().value, 3u);
  ASSERT_EQ(f32.operand_layouts[1].forbidden_modifiers.size(), 1u);
  EXPECT_EQ(f32.operand_layouts[1].forbidden_modifiers.front().value, 2u);
}

/** Accept floating literals in every scalar source position of both arities. */
TEST(MinMaxCompleteness, AcceptsScalarSourceLiterals) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .f32 %f<4>; .reg .f64 %d<4>;
  min.f32 %f0, 0f3f800000, %f1;
  min.f32 %f0, %f1, 1.0;
  max.f32 %f0, %f1, 0f00000000;
  min.f64 %d0, 1.0, %d1;
  min.f64 %d0, %d1, 1.0;
  min.f32 %f0, %f1, %f2, 1.0;
  min.abs.f32 %f0, 1.0, %f1, %f2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  EXPECT_EQ(resolved->functions.front().body.size(), 7u);
}

/** Reject literals outside the scalar source positions. */
TEST(MinMaxCompleteness, RejectsLiteralsWhereRegistersAreRequired) {
  for (const auto source : {
           "min.f32 1.0, %f1, %f2;",
           "min.f32 %f0, 1, %f1;",
           "min.f64 %d0, 1, %d1;",
           "min.f16 %h0, %h1, 1.0;",
           "min.f16x2 %r0, %r1, 1.0;",
           "min.bf16 %b0, %b1, 1.0;",
           "min.bf16x2 %r0, %r1, 1.0;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Reject the spellings each cohort or layout forbids, and the wrong arities. */
TEST(MinMaxCompleteness, RejectsIllegalSpellings) {
  for (const auto source : {
           "min.xorsign.f32 %f0, %f1, %f2;",
           "min.nan.f32 %f0, %f1, %f2;",
           "min.abs.f64 %d0, %d1, %d2;",
           "min.xorsign.abs.f64 %d0, %d1, %d2;",
           "min.ftz.bf16 %b0, %b1, %b2;",
           "min.abs.bf16 %b0, %b1, %b2;",
           "min.abs.f16 %h0, %h1, %h2;",
           "min.f32 %f0, %f1;",
           "min.f32 %f0, %f1, %f2, %f3, %f4;",
           "min.f64 %d0, %d1;",
           "max.xorsign.f32 %f0, %f1, %f2;",
           "max.abs.f64 %d0, %d1, %d2;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveInstruction(*parsed).has_value());
  }
}

/** Reject a paired spelling on the three-source layout and vice versa. */
TEST(MinMaxCompleteness, RejectsModifiersForbiddenByTheSelectedLayout) {
  for (const auto source : {
           "min.abs.f32 %f0, %f1, %f2;",
           "min.ftz.abs.f32 %f0, %f1, %f2;",
           "min.xorsign.abs.f32 %f0, %f1, %f2, %f3;",
           "max.abs.f32 %f0, %f1, %f2;",
           "max.xorsign.abs.f32 %f0, %f1, %f2, %f3;",
       }) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    // The variant and the layout are both selectable; only the checker knows
    // that the chosen layout rejects the spelled modifier.
    const auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto checked = std::visit(
        [](const auto& instruction) {
          return checker::check(
              instruction, checker::Context{.target = {.ptx_version = {9, 3},
                                                       .sm_version = 100}});
        },
        *resolved);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ModifierNotAllowedForLayout);
  }
}

/** Keep the positional aggregate form of the public modifier view source-compatible. */
TEST(MinMaxCompleteness, PreservesLegacyModifierViewAggregateInitialization) {
  const checker::ModifierValueView legacy{"ftz",
                                          checker::ModifierValueKind::Bool};
  EXPECT_EQ(legacy.kind_id, "ftz");
  EXPECT_EQ(legacy.value_kind, checker::ModifierValueKind::Bool);
  EXPECT_FALSE(legacy.is_present);
  EXPECT_TRUE(legacy.locations.empty());
  // The appended slot identity defaults, so older initializers stay valid.
  EXPECT_EQ(legacy.slot.value, 0u);
}

/** Report the offending modifier's own range when it is still retained. */
TEST(MinMaxCompleteness, ReportsForbiddenModifierOwnRange) {
  const auto parsed =
      test_helpers::parseInstruction("min.abs.f32 %f0, %f1, %f2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveInstruction(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

  const auto& variant = std::get<Min::F32>(std::get<Min>(*resolved).variant);
  ASSERT_FALSE(variant.abs.locs.empty());
  const auto checked = std::visit(
      [](const auto& instruction) {
        return checker::check(instruction,
                              checker::Context{.target = {.ptx_version = {9, 3},
                                                          .sm_version = 100}});
      },
      *resolved);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModifierNotAllowedForLayout);
  EXPECT_EQ(checked.error().front().range, variant.abs.locs.front());
}

/** Keep matching by slot identity once the modifier's retained locations are gone. */
TEST(MinMaxCompleteness, MatchesForbiddenSlotAfterLocationsAreCleared) {
  const auto parsed =
      test_helpers::parseInstruction("min.abs.f32 %f0, %f1, %f2;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveInstruction(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

  auto& variant = std::get<Min::F32>(std::get<Min>(*resolved).variant);
  variant.abs.locs.clear();
  const auto checked = std::visit(
      [](const auto& instruction) {
        return checker::check(instruction,
                              checker::Context{.target = {.ptx_version = {9, 3},
                                                          .sm_version = 100}});
      },
      *resolved);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModifierNotAllowedForLayout);
  // Without modifier provenance the diagnostic falls back to the context range.
  EXPECT_EQ(checked.error().front().range, (SourceRange{}));
}

/** Check each cohort's independent PTX and target floor. */
TEST(MinMaxCompleteness, ChecksIndependentAvailability) {
  /** One representative source and its independent availability boundary. */
  struct AvailabilityCase {
    /** Representative instruction source. */
    const char* source;
    /** First PTX version accepting the cohort. */
    checker::PtxVersion ptx;
    /** First target SM accepting the cohort. */
    unsigned sm;
    /** Previous valid PTX version, when one exists below the floor. */
    std::optional<checker::PtxVersion> older_ptx;
    /** Previous target SM below the floor. */
    unsigned older_sm;
  };
  for (const auto& item : {
           AvailabilityCase{
               "min.f32 %f0, %f1, %f2;", {1, 0}, 0, std::nullopt, 0},
           AvailabilityCase{"min.NaN.f32 %f0, %f1, %f2;",
                            {7, 0},
                            80,
                            checker::PtxVersion{6, 5},
                            75},
           AvailabilityCase{"min.xorsign.abs.f32 %f0, %f1, %f2;",
                            {7, 2},
                            86,
                            checker::PtxVersion{7, 1},
                            85},
           AvailabilityCase{"min.abs.f32 %f0, %f1, %f2, %f3;",
                            {8, 8},
                            100,
                            checker::PtxVersion{8, 7},
                            90},
           AvailabilityCase{
               "min.f64 %d0, %d1, %d2;", {1, 0}, 13, std::nullopt, 12},
           AvailabilityCase{"min.f16 %h0, %h1, %h2;",
                            {7, 0},
                            80,
                            checker::PtxVersion{6, 5},
                            75},
           AvailabilityCase{"min.bf16 %b0, %b1, %b2;",
                            {7, 0},
                            80,
                            checker::PtxVersion{6, 5},
                            75},
           AvailabilityCase{"max.abs.f32 %f0, %f1, %f2, %f3;",
                            {8, 8},
                            100,
                            checker::PtxVersion{8, 7},
                            90},
       }) {
    SCOPED_TRACE(item.source);
    const auto parsed = test_helpers::parseInstruction(item.source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto check_at = [&](checker::TargetInfo target) {
      return std::visit(
          [&](const auto& instruction) {
            return checker::check(instruction,
                                  checker::Context{.target = target});
          },
          *resolved);
    };
    EXPECT_TRUE(
        check_at({.ptx_version = item.ptx, .sm_version = item.sm}).has_value());
    if (item.older_ptx) {
      const auto ptx_failure =
          check_at({.ptx_version = *item.older_ptx, .sm_version = item.sm});
      ASSERT_FALSE(ptx_failure.has_value());
      EXPECT_EQ(ptx_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    }
    if (item.sm != 0) {
      const auto sm_failure =
          check_at({.ptx_version = item.ptx, .sm_version = item.older_sm});
      ASSERT_FALSE(sm_failure.has_value());
      EXPECT_EQ(sm_failure.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedSmVersion);
    }
  }
}

/** Preserve owned layout selection and reject a tag/payload disagreement. */
TEST(MinMaxCompleteness, OwnsLayoutSelectionAndRevalidatesTag) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() { .reg .f32 %f<4>;
  min.f32 %f0, %f1, %f2; min.f32 %f0, %f1, %f2, %f3; }
)ptx";
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());

  auto& ternary = std::get<Min::F32>(
      std::get<Min>(owned->functions.front().body[1]).variant);
  EXPECT_EQ(ternary.operand_layout, (ResolvedOperandLayoutTag{1}));
  ternary.operand_layout = ResolvedOperandLayoutTag{0};
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
