#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse the complete module used to exercise public-IR revalidation. */
syntax_ast::AstModule parse_module(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "PTX source did not parse."
                          : parsed.diagnostics.front().message);
    return {};
  }
  return std::move(*parsed);
}

/** Resolve one source-valid floating Add and retain its checker context. */
struct ResolvedFloatAdd {
  Add instruction;
  checker::Context context;
};

/**
 * Resolve a source-valid full module, then copy the Add for public-IR mutation.
 *
 * The returned context retains the module target and Add source range so the
 * checker path is identical to a consumer revalidating one edited instruction.
 */
ResolvedFloatAdd resolve_float_add(std::string_view rounding_suffix = ".rn") {
  const std::string source = std::string{R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  .reg .f32 %f<3>;
  mov.f32 %f1, 1.0;
  mov.f32 %f2, 2.0;
)ptx"} + "  add" + std::string{rounding_suffix} +
                             R"ptx(.f32 %f0, %f1, %f2;
  ret;
}
)ptx";
  const auto ast = parse_module(source);
  const auto resolved = resolveModule(ast);
  if (!resolved) {
    ADD_FAILURE() << resolved.error().front().message;
    return {};
  }
  if (resolved->functions.size() != 1 ||
      resolved->functions.front().body.size() != 4 ||
      resolved->functions.front().instruction_ranges.size() != 4) {
    ADD_FAILURE()
        << "The valid module did not retain its expected Add body entry.";
    return {};
  }
  const auto* add = std::get_if<Add>(&resolved->functions.front().body[2]);
  if (add == nullptr) {
    ADD_FAILURE() << "The selected source instruction did not resolve as Add.";
    return {};
  }
  return {
      .instruction = *add,
      .context =
          {
              .target = {.ptx_version = {8, 0}, .sm_version = 80},
              .instruction_range =
                  resolved->functions.front().instruction_ranges[2],
          },
  };
}

/** Return the expected domain failure and assert its source-range anchoring. */
void expect_domain_failure(const checker::CheckResult& checked,
                           const SourceRange& expected_range) {
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1U);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModifierValueDomainMismatch);
  EXPECT_EQ(checked.error().front().range, expected_range);
}

/** Edited public IR must reject a named rounding enum outside the Add domain. */
TEST(ModifierDomainRevalidation, RejectsMutatedFloatingAddRzi) {
  auto candidate = resolve_float_add();
  auto* selected = std::get_if<Add::FloatF32>(&candidate.instruction.variant);
  ASSERT_NE(selected, nullptr);
  selected->rounding.value = RoundingMode::Rzi;
  ASSERT_FALSE(selected->rounding.locs.empty());

  expect_domain_failure(
      checker::check(candidate.instruction, candidate.context),
      selected->rounding.locs.front());
}

/** Legal Add rounding values remain legal even without special availability. */
TEST(ModifierDomainRevalidation, PreservesLegalFloatingAddRoundingValues) {
  for (const RoundingMode rounding :
       std::array{RoundingMode::Rn, RoundingMode::Rz, RoundingMode::Rm,
                  RoundingMode::Rp}) {
    SCOPED_TRACE(static_cast<int>(rounding));
    auto candidate = resolve_float_add();
    auto* selected = std::get_if<Add::FloatF32>(&candidate.instruction.variant);
    ASSERT_NE(selected, nullptr);
    selected->rounding.value = rounding;
    EXPECT_TRUE(
        checker::check(candidate.instruction, candidate.context).has_value());
  }
}

/** Omitted rounding retains its per-field default, while missing locs do not relax domain checks. */
TEST(ModifierDomainRevalidation, UsesDefaultAndFallbackRangeWithoutProvenance) {
  auto candidate = resolve_float_add("");
  auto* selected = std::get_if<Add::FloatF32>(&candidate.instruction.variant);
  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(selected->rounding.value, RoundingMode::Rn);
  EXPECT_TRUE(selected->rounding.locs.empty());
  EXPECT_TRUE(
      checker::check(candidate.instruction, candidate.context).has_value());

  selected->rounding.value = RoundingMode::Rzi;
  expect_domain_failure(
      checker::check(candidate.instruction, candidate.context),
      candidate.context.instruction_range);
}

/** Invalid and unnamed enum values share the same variant-domain invariant. */
TEST(ModifierDomainRevalidation, RejectsInvalidAndUnnamedRoundingValues) {
  for (const RoundingMode rounding :
       std::array{RoundingMode::Invalid, static_cast<RoundingMode>(0xff)}) {
    for (const bool retain_provenance : {true, false}) {
      SCOPED_TRACE(static_cast<int>(rounding));
      SCOPED_TRACE(retain_provenance);
      auto candidate = resolve_float_add();
      auto* selected =
          std::get_if<Add::FloatF32>(&candidate.instruction.variant);
      ASSERT_NE(selected, nullptr);
      selected->rounding.value = rounding;
      ASSERT_FALSE(selected->rounding.locs.empty());
      const SourceRange expected_range =
          retain_provenance ? selected->rounding.locs.front()
                            : candidate.context.instruction_range;
      if (!retain_provenance)
        selected->rounding.locs.clear();
      expect_domain_failure(
          checker::check(candidate.instruction, candidate.context),
          expected_range);
    }
  }
}

/** Domain membership does not replace target availability for a legal value. */
TEST(ModifierDomainRevalidation, RetainsLegalRoundingAvailabilityChecks) {
  auto candidate = resolve_float_add(".rm");
  auto* selected = std::get_if<Add::FloatF32>(&candidate.instruction.variant);
  ASSERT_NE(selected, nullptr);
  ASSERT_FALSE(selected->rounding.locs.empty());
  candidate.context.target.sm_version = 10;

  const auto checked = checker::check(candidate.instruction, candidate.context);
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1U);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(checked.error().front().range, selected->rounding.locs.front());
}

/** Source spelling rejection is distinct from public resolved-IR revalidation. */
TEST(ModifierDomainRevalidation, SourceTextStillRejectsAddRzi) {
  PtxSyntaxParser parser("add.rzi.f32 %f0, %f1, %f2;");
  const auto parsed = parser.parseInstruction();
  ASSERT_TRUE(parsed.has_value()) << parsed.diagnostics.front().message;
  EXPECT_FALSE(resolve<Add>(*parsed).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
