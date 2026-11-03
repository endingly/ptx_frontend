#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/arithmetic/fma.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <string>
#include <string_view>
#include <vector>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** One existing half family and a legal physical-carrier source witness. */
struct HalfFmaCase {
  /** Stable canonical variant identity used for metadata joins. */
  std::string_view variant;
  /** Written instruction suffix after rounding. */
  std::string_view suffix;
  /** Exact logical scalar or packed source/destination format. */
  ScalarType type;
  /** Selected existing public instruction identity. */
  InstructionKind kind;
};
constexpr std::array half_fma_cases{
    HalfFmaCase{"fma_rn_f16", ".f16", ScalarType::F16, FmaRnF16::kind},
    HalfFmaCase{"fma_rn_f16x2", ".f16x2", ScalarType::F16x2, FmaRnF16x2::kind},
    HalfFmaCase{"fma_half_relu", ".relu.f16", ScalarType::F16,
                FmaHalfRelu::kind},
    HalfFmaCase{"fma_half_oob", ".oob.f16", ScalarType::F16, FmaHalfOob::kind},
    HalfFmaCase{"fma_half_oob_relu", ".oob.relu.f16", ScalarType::F16,
                FmaHalfOobRelu::kind},
    HalfFmaCase{"fma_bf16", ".bf16", ScalarType::BF16, FmaBf16::kind},
    HalfFmaCase{"fma_bf16x2", ".bf16x2", ScalarType::BF16x2, FmaBf16x2::kind},
    HalfFmaCase{"fma_bf16_oob", ".oob.bf16", ScalarType::BF16,
                FmaBf16Oob::kind},
    HalfFmaCase{"fma_bf16x2_oob", ".oob.bf16x2", ScalarType::BF16x2,
                FmaBf16x2Oob::kind}};
/** Initialize real Bit carriers, keeping every FMA source register-valued. */
std::string half_fma_source(const HalfFmaCase& item, bool rounding = true,
                            std::string_view replacement = {}) {
  const bool packed =
      item.type == ScalarType::F16x2 || item.type == ScalarType::BF16x2;
  const std::string physical = packed ? "b32" : "b16";
  return ".version 9.3\n.target sm_100\n.entry kernel(){.reg ." + physical +
         " d,a,b,c; mov." + physical + " a,1; mov." + physical + " b,2; mov." +
         physical + " c,0; fma" + (rounding ? ".rn" : "") +
         std::string(replacement.empty() ? item.suffix : replacement) +
         " d,a,b,c;}";
}
/** Complete context for direct checks at the same recognized modern profile. */
const checker::Context half_fma_context{
    .target = {.ptx_version = {9, 3}, .sm_version = 100}};
/** Borrow the owned instance control from the closed family of existing classes. */
WithLocs<RoundingMode>& half_fma_rounding(Instruction& instruction) {
  switch (instruction.instruction_kind()) {
    case FmaRnF16::kind:
      return dynamic_cast<FmaRnF16&>(instruction).rounding;
    case FmaRnF16x2::kind:
      return dynamic_cast<FmaRnF16x2&>(instruction).rounding;
    case FmaHalfRelu::kind:
      return dynamic_cast<FmaHalfRelu&>(instruction).rounding;
    case FmaHalfOob::kind:
      return dynamic_cast<FmaHalfOob&>(instruction).rounding;
    case FmaHalfOobRelu::kind:
      return dynamic_cast<FmaHalfOobRelu&>(instruction).rounding;
    case FmaBf16::kind:
      return dynamic_cast<FmaBf16&>(instruction).rounding;
    case FmaBf16x2::kind:
      return dynamic_cast<FmaBf16x2&>(instruction).rounding;
    case FmaBf16Oob::kind:
      return dynamic_cast<FmaBf16Oob&>(instruction).rounding;
    case FmaBf16x2Oob::kind:
      return dynamic_cast<FmaBf16x2Oob&>(instruction).rounding;
    default:
      ADD_FAILURE() << "Unexpected half FMA class";
      return dynamic_cast<FmaRnF16&>(instruction).rounding;
  }
}
/** Return IR only after its source text, parser and syntax AST have died. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_half_fma(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Check actual payloads through independent module/function owners and clones. */
void expect_half_fma_lifetime(const ResolvedModule& module, bool valid = true) {
  EXPECT_EQ(bool(module.functions.front().body.back()->check(half_fma_context)),
            valid);
  EXPECT_EQ(bool(validateModule(module)), valid);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  EXPECT_EQ(bool(validateModule(copied)), valid);
  EXPECT_EQ(bool(validateModule(assigned)), valid);
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_NE(function.body.back().get(),
            module.functions.front().body.back().get());
  EXPECT_EQ(bool(function_assigned.body.back()->check(half_fma_context)),
            valid);
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_EQ(bool(validateModule(assigned)), valid);
}
TEST(HalfFmaModifiers, RetainsNineExplicitRegisterControls) {
  for (const auto& item : half_fma_cases) {
    SCOPED_TRACE(item.variant);
    const auto ast = test_helpers::parseModule(half_fma_source(item));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_EQ(module->functions.front().body.back()->instruction_kind(),
              item.kind);
  }
}
TEST(HalfFmaModifiers, AdmitsOmittedRnBesideAllNineControls) {
  for (const auto& item : half_fma_cases) {
    SCOPED_TRACE(item.variant);
    const auto ast = test_helpers::parseModule(half_fma_source(item, false));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto draft = resolveModuleOnly(*ast);
    std::cout << item.variant << " omitted-stage="
              << (draft ? "draft" : "resolution-rejected; owned=N/A") << '\n';
    EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
    EXPECT_TRUE(resolveAndValidateModule(*ast));
  }
}
TEST(HalfFmaModifiers, RejectsOobSatWithoutChangingPlainOrReluControls) {
  for (const auto type : {ScalarType::F16, ScalarType::F16x2}) {
    auto item = half_fma_cases[3];
    item.type = type;
    const auto suffix = type == ScalarType::F16 ? ".f16" : ".f16x2";
    for (const auto control : {".oob", ".oob.relu"}) {
      const auto ast = test_helpers::parseModule(
          half_fma_source(item, true, std::string(control) + suffix));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
    const auto ast = test_helpers::parseModule(
        half_fma_source(item, true, std::string(".oob.sat") + suffix));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveModuleOnly(*ast));
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    const auto omitted = test_helpers::parseModule(
        half_fma_source(item, false, std::string(".oob.sat") + suffix));
    ASSERT_MODULE_PARSE_SUCCEEDS(omitted);
    EXPECT_FALSE(resolveAndValidateModule(*omitted));
  }
}
TEST(HalfFmaModifiers, ClosesAllFortyEightTypedProductsAfterAstDeath) {
  size_t count = 0;
  for (size_t family = 0; family < half_fma_cases.size(); ++family) {
    const auto& original = half_fma_cases[family];
    const bool two_types = family >= 2 && family <= 4;
    const size_t type_count = two_types ? 2 : 1;
    for (size_t type_index = 0; type_index < type_count; ++type_index) {
      auto item = original;
      if (two_types && type_index)
        item.type = ScalarType::F16x2;
      const bool packed =
          item.type == ScalarType::F16x2 || item.type == ScalarType::BF16x2;
      const std::string type = item.type == ScalarType::F16     ? "f16"
                               : item.type == ScalarType::F16x2 ? "f16x2"
                               : item.type == ScalarType::BF16  ? "bf16"
                                                                : "bf16x2";
      for (const bool ftz : {false, true})
        for (const bool sat : {false, true})
          for (const bool relu : {false, true}) {
            if (ftz && family > 2)
              continue;
            if (sat && family > 1)
              continue;
            const bool fixed_relu = family == 2 || family == 4;
            const bool optional_relu = family >= 5;
            if (fixed_relu ? !relu : (!optional_relu && relu))
              continue;
            const bool oob = family == 3 || family == 4 || family >= 7;
            const std::string suffix =
                (oob ? ".oob" : std::string{}) + (ftz ? ".ftz" : "") +
                (sat ? ".sat" : "") + (relu ? ".relu" : "") + "." + type;
            for (const bool written : {false, true}) {
              SCOPED_TRACE(std::string(item.variant) + suffix +
                           (written ? ".rn" : ""));
              const auto text = half_fma_source(item, written, suffix);
              const auto ast = test_helpers::parseModule(text);
              ASSERT_MODULE_PARSE_SUCCEEDS(ast);
              ASSERT_TRUE(resolveModuleOnly(*ast));
              auto module = owned_half_fma(text);
              ASSERT_TRUE(module) << module.error().front().message;
              EXPECT_TRUE(validateModule(*ast, *module));
              auto& instruction = *module->functions.front().body.back();
              EXPECT_EQ(instruction.instruction_kind(), item.kind);
              auto& rounding = half_fma_rounding(instruction);
              EXPECT_EQ(rounding.value, RoundingMode::Rn);
              EXPECT_EQ(rounding.locs.empty(), !written);
              if (written) {
                ASSERT_EQ(rounding.locs.size(), 1u);
                EXPECT_GT(rounding.locs.front().start.column, 0);
              }
              expect_half_fma_lifetime(*module);
              ++count;
              std::cout << "CELL " << item.variant << " type=" << type
                        << " rn=" << written << " oob=" << oob << " ftz=" << ftz
                        << " sat=" << sat << " relu=" << relu << '\n';
            }
          }
    }
  }
  EXPECT_EQ(count, 48u);
}
TEST(HalfFmaModifiers, PreservesCurrentPresenceAndRejectsNonRnDomains) {
  for (const auto& item : half_fma_cases) {
    auto module = owned_half_fma(half_fma_source(item));
    ASSERT_TRUE(module);
    auto& control = half_fma_rounding(*module->functions.front().body.back());
    const auto locations = control.locs;
    for (const auto mode :
         {RoundingMode::Rz, RoundingMode::Rm, RoundingMode::Rp,
          RoundingMode::Rni, RoundingMode::Invalid})
      for (const bool located : {false, true}) {
        control.value = mode;
        control.locs = located ? locations : decltype(locations){};
        expect_half_fma_lifetime(*module, false);
      }
    control.value = RoundingMode::Rn;
    control.locs.clear();
    expect_half_fma_lifetime(*module);
    const auto original = test_helpers::parseModule(half_fma_source(item));
    ASSERT_MODULE_PARSE_SUCCEEDS(original);
    // Original syntax still matches retained identity; current typed RN may omit its token.
    EXPECT_TRUE(validateModule(*original, *module));
    control.locs = locations;
    expect_half_fma_lifetime(*module);
    const auto replacement =
        test_helpers::parseModule(half_fma_source(item, false));
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto mismatch = validateModule(*replacement, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}
TEST(HalfFmaModifiers, RetainsClosedModifierCarrierAndAvailabilityBoundaries) {
  for (const auto& item : half_fma_cases) {
    for (const auto mode : {".rz", ".rm", ".rp", ".rni", ".rn.rn"}) {
      const auto ast = test_helpers::parseModule(half_fma_source(
          item, false, std::string(mode) + std::string(item.suffix)));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    for (const bool written : {false, true}) {
      auto module = owned_half_fma(half_fma_source(item, written));
      ASSERT_TRUE(module);
      const auto index = size_t(&item - half_fma_cases.data());
      const uint16_t major = index <= 1                                 ? 4
                             : (index == 2 || index == 5 || index == 6) ? 7
                                                                        : 8;
      const uint16_t minor = major == 4 ? 2 : major == 8 ? 1 : 0;
      const unsigned sm = major == 4 ? 53 : major == 7 ? 80 : 90;
      checker::Context context{
          .target = {.ptx_version = {major, minor}, .sm_version = sm}};
      EXPECT_TRUE(module->functions.front().body.back()->check(context));
      // Numeric-unit thresholds may be synthetic (for example SM79 and PTX6.9).
      // They isolate comparisons and do not claim recognized source profiles.
      context.target.sm_version = sm - 1;
      EXPECT_FALSE(module->functions.front().body.back()->check(context));
      context.target.sm_version = sm;
      context.target.ptx_version = {
          major, static_cast<uint16_t>(minor ? minor - 1 : 0)};
      if (!minor)
        context.target.ptx_version = {static_cast<uint16_t>(major - 1), 9};
      EXPECT_FALSE(module->functions.front().body.back()->check(context));
      const unsigned below_sm = sm == 53 ? 52 : sm == 80 ? 75 : 89;
      const auto original = half_fma_source(item, written);
      const auto body = original.substr(original.find("\n.entry"));
      const std::string version = ".version " + std::to_string(major) + "." +
                                  std::to_string(minor) + "\n.target sm_";
      const auto minimum =
          test_helpers::parseModule(version + std::to_string(sm) + body);
      ASSERT_MODULE_PARSE_SUCCEEDS(minimum);
      ASSERT_TRUE(resolveAndValidateModule(*minimum));
      const auto below =
          test_helpers::parseModule(version + std::to_string(below_sm) + body);
      ASSERT_MODULE_PARSE_SUCCEEDS(below);
      const auto rejected = resolveAndValidateModule(*below);
      ASSERT_FALSE(rejected);
      EXPECT_TRUE(
          std::ranges::any_of(rejected.error(), [](const auto& diagnostic) {
            return diagnostic.checker_kind ==
                   checker::CheckDiagnosticKind::UnsupportedSmVersion;
          }));
    }
  }
  for (const auto suffix :
       {".ftz.bf16", ".sat.bf16", ".ftz.bf16x2", ".sat.bf16x2", ".oob.ftz.f16",
        ".sat.relu.f16", ".oob.sat.relu.f16", ".ftz.rn.f16"}) {
    const auto& item =
        std::string_view(suffix).find("bf16x2") != std::string_view::npos
            ? half_fma_cases[6]
        : std::string_view(suffix).find("bf16") != std::string_view::npos
            ? half_fma_cases[5]
            : half_fma_cases[0];
    const auto ast =
        test_helpers::parseModule(half_fma_source(item, false, suffix));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
