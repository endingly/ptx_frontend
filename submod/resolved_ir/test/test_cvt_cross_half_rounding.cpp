#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <string>
#include <string_view>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Written mode and its exact operation enum, independent of diagnostic spelling. */
struct HalfRound {
  /** Source suffix without its leading dot. */
  std::string_view spelling;
  /** Requested operation; source constants are not executed in this mode. */
  RoundingMode value;
};
constexpr std::array half_integer_rounds{
    HalfRound{"rni", RoundingMode::Rni}, HalfRound{"rzi", RoundingMode::Rzi},
    HalfRound{"rmi", RoundingMode::Rmi}, HalfRound{"rpi", RoundingMode::Rpi}};
/** Initialize independent legal physical carriers without an alternate declaration. */
std::string cross_half_source(bool forward, std::string_view mode,
                              bool sat = false, bool ftz = false,
                              std::string_view carrier = "f16") {
  const std::string initialization =
      carrier == "b128"
          ? ".reg .b64 lo,hi; mov.b64 lo,0x3e00; mov.b64 hi,0; mov.b128 "
            "h,{lo,hi}; "
          : std::string(carrier == "b32" ? "mov.b32" : "mov.b16") +
                " h,0x3e00; ";
  return ".version 9.3\n.target sm_90\n.entry kernel(){.reg ." +
         std::string(carrier) + " h; .reg .b16 b; " + initialization +
         "mov.b16 b,0x3fc0; cvt" +
         (mode.empty() ? std::string{} : "." + std::string(mode)) +
         (ftz ? ".ftz" : "") + (sat ? ".sat" : "") +
         (forward ? ".bf16.f16 b,h;}" : ".f16.bf16 h,b;}");
}
/** Borrow only the two existing cross-format concrete forms during inspection. */
struct HalfFields {
  /** Operation and written ranges owned by the supplied instruction. */
  WithLocs<RoundingMode>& rounding;
  /** Independent SAT value and written ranges. */
  WithLocs<bool>& saturate;
  /** Physical destination, independent of the16-bit logical format. */
  WithLocs<ResolvedRegisterRef>& dst;
  /** Source reference or converted typed word. */
  WithLocs<ResolvedCvtSource>& src;
};
/** Inspect typed endpoints and expose existing fields without adding a public adapter. */
HalfFields half_fields(Instruction& instruction, bool forward) {
  if (forward) {
    auto& form = dynamic_cast<CvtBf16FromNonF32Base&>(instruction);
    EXPECT_EQ(form.dst_type, ScalarType::BF16);
    EXPECT_EQ(form.src_type.value, ScalarType::F16);
    return {form.rounding, form.saturate, form.dst, form.src};
  }
  auto& form = dynamic_cast<CvtNonF32BaseFromBf16&>(instruction);
  EXPECT_EQ(form.dst_type.value, ScalarType::F16);
  EXPECT_EQ(form.src_type, ScalarType::BF16);
  return {form.rounding, form.saturate, form.dst, form.src};
}
/** Complete direct-check context matching initialized fixtures. */
const checker::Context half_context{
    .target = {.ptx_version = {9, 3}, .sm_version = 90}};
/** Return a declaration-valid draft after destroying source text, parser and AST. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_cross_half(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}
/** Recheck typed operations after module/function copy, assignment and instruction clone. */
void expect_half_lifetime(const ResolvedModule& module) {
  EXPECT_TRUE(validateModule(module));
  ResolvedModule copied(module), assigned;
  assigned = copied;
  EXPECT_TRUE(validateModule(copied));
  EXPECT_TRUE(validateModule(assigned));
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_TRUE(function.body.back()->check(half_context));
  EXPECT_TRUE(function_assigned.body.back()->check(half_context));
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_TRUE(validateModule(assigned));
}
TEST(CvtCrossHalfRounding, RetainsExplicitFloatingAndReverseOmissionControls) {
  for (const bool forward : {false, true})
    for (const auto mode : {"rn", "rz", "rm", "rp", ""})
      for (const bool sat : {false, true}) {
        if ((forward && sat) || (forward && std::string_view(mode).empty()))
          continue;
        const auto ast =
            test_helpers::parseModule(cross_half_source(forward, mode, sat));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto strict = resolveAndValidateModule(*ast);
        ASSERT_TRUE(strict) << strict.error().front().message;
      }
}
TEST(CvtCrossHalfRounding, RejectsOrdinaryLossyOmissionDespiteStoredDefault) {
  const auto ast = test_helpers::parseModule(cross_half_source(true, ""));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto draft = resolveModuleOnly(*ast);
  ASSERT_TRUE(draft);
  auto fields = half_fields(*draft->functions.front().body.back(), true);
  EXPECT_EQ(fields.rounding.value, RoundingMode::Rn);
  EXPECT_TRUE(fields.rounding.locs.empty());
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  EXPECT_FALSE(draft->functions.front().body.back()->check(half_context));
  EXPECT_FALSE(validateModule(*draft));
}
TEST(CvtCrossHalfRounding, AdmitsAllTwelveTypedIntegerOperations) {
  for (const bool forward : {false, true})
    for (const auto& mode : half_integer_rounds)
      for (const bool sat : {false, true}) {
        if (forward && sat)
          continue;
        SCOPED_TRACE(mode.spelling);
        SCOPED_TRACE(forward);
        SCOPED_TRACE(sat);
        const auto source = cross_half_source(forward, mode.spelling, sat);
        const auto ast = test_helpers::parseModule(source);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        auto draft = resolveModuleOnly(*ast);
        ASSERT_TRUE(draft) << draft.error().front().message;
        auto fields =
            half_fields(*draft->functions.front().body.back(), forward);
        EXPECT_EQ(fields.rounding.value, mode.value);
        EXPECT_FALSE(fields.rounding.locs.empty());
        EXPECT_EQ(fields.saturate.value, sat);
        EXPECT_EQ(fields.saturate.locs.empty(), !sat);
        const auto& reference = std::get<ResolvedRegisterRef>(fields.src.value);
        EXPECT_TRUE(reference.symbol_id);
        EXPECT_TRUE(fields.dst.value.symbol_id);
        EXPECT_NE(reference.symbol_id, fields.dst.value.symbol_id);
        EXPECT_EQ(reference.spelling, forward ? "h" : "b");
        EXPECT_EQ(fields.dst.value.spelling, forward ? "b" : "h");
        EXPECT_EQ(reference.declared_type,
                  forward ? ScalarType::F16 : ScalarType::B16);
        EXPECT_FALSE(fields.src.locs.empty());
        EXPECT_TRUE(resolveAndValidateModule(*ast));
        EXPECT_TRUE(draft->functions.front().body.back()->check(half_context));
        EXPECT_TRUE(validateModule(*ast, *draft));
        auto owned = owned_cross_half(source);
        ASSERT_TRUE(owned);
        expect_half_lifetime(*owned);
      }
}
TEST(CvtCrossHalfRounding, ChecksTheCompleteFiniteControlMatrix) {
  constexpr std::array modes{
      HalfRound{"", RoundingMode::Invalid}, HalfRound{"rn", RoundingMode::Rn},
      HalfRound{"rz", RoundingMode::Rz},    HalfRound{"rm", RoundingMode::Rm},
      HalfRound{"rp", RoundingMode::Rp},    HalfRound{"rni", RoundingMode::Rni},
      HalfRound{"rzi", RoundingMode::Rzi},  HalfRound{"rmi", RoundingMode::Rmi},
      HalfRound{"rpi", RoundingMode::Rpi},  HalfRound{"rna", RoundingMode::Rna},
      HalfRound{"rs", RoundingMode::Rs}};
  size_t products = 0, allowed_count = 0;
  for (const bool forward : {false, true})
    for (const auto& mode : modes)
      for (const bool sat : {false, true})
        for (const bool ftz : {false, true}) {
          const bool allowed = !ftz && mode.value != RoundingMode::Rna &&
                               mode.value != RoundingMode::Rs &&
                               (!forward || (!sat && !mode.spelling.empty()));
          const std::string id =
              "CVTHALF::" +
              std::string(forward ? "bf16_from_f16" : "f16_from_bf16") + "::" +
              (mode.spelling.empty() ? std::string("absent")
                                     : std::string(mode.spelling)) +
              "::sat" + (sat ? "1" : "0") + "::ftz" + (ftz ? "1" : "0");
          SCOPED_TRACE(id);
          ++products;
          allowed_count += allowed;
          const auto ast = test_helpers::parseModule(
              cross_half_source(forward, mode.spelling, sat, ftz));
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          auto draft = resolveModuleOnly(*ast);
          const auto strict = resolveAndValidateModule(*ast);
          std::cout << "CELL " << id << " allow=" << allowed
                    << " draft=" << bool(draft) << " strict=" << bool(strict)
                    << '\n';
          EXPECT_EQ(bool(strict), allowed);
          if (draft) {
            EXPECT_EQ(
                bool(draft->functions.front().body.back()->check(half_context)),
                allowed);
            EXPECT_EQ(bool(validateModule(*ast, *draft)), allowed);
          }
          if (allowed) {
            ASSERT_TRUE(draft);
            auto fields =
                half_fields(*draft->functions.front().body.back(), forward);
            EXPECT_EQ(fields.rounding.value,
                      mode.spelling.empty() ? RoundingMode::Rn : mode.value);
            EXPECT_EQ(fields.rounding.locs.empty(), mode.spelling.empty());
          }
        }
  EXPECT_EQ(products, 88u);
  EXPECT_EQ(allowed_count, 26u);
}
TEST(CvtCrossHalfRounding,
     DistinguishesDefaultAbsenceFromCurrentNondefaultPresence) {
  auto module = owned_cross_half(cross_half_source(true, "rni"));
  ASSERT_TRUE(module);
  const auto ast = test_helpers::parseModule(cross_half_source(true, "rni"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto fields = half_fields(*module->functions.front().body.back(), true);
  for (const auto& mode : half_integer_rounds) {
    fields.rounding.value = mode.value;
    EXPECT_TRUE(validateModule(*ast, *module));
    fields.rounding.locs.clear();
    EXPECT_TRUE(module->functions.front().body.back()->check(half_context));
    expect_half_lifetime(*module);
  }
  fields.rounding.value = RoundingMode::Rn;
  const auto missing = validateModule(*module);
  ASSERT_FALSE(missing);
  EXPECT_TRUE(std::ranges::any_of(missing.error(), [](const auto& diagnostic) {
    return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
           diagnostic.message.find("requires explicit rounding") !=
               std::string::npos;
  }));
  const auto replacement =
      test_helpers::parseModule(cross_half_source(true, "rzi"));
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  ASSERT_TRUE(resolveAndValidateModule(*replacement));
  fields.rounding.value = RoundingMode::Rni;
  const auto mismatch = validateModule(*replacement, *module);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  // Original syntax still matches retained identity; current typed operations are rechecked.
  EXPECT_TRUE(validateModule(*ast, *module));

  std::array modifiers{
      checker::ModifierValueView{
          .kind_id = "dst_type",
          .value_kind = checker::ModifierValueKind::ScalarType,
          .scalar_type = ScalarType::BF16,
          .is_present = true},
      checker::ModifierValueView{
          .kind_id = "src_type",
          .value_kind = checker::ModifierValueKind::ScalarType,
          .scalar_type = ScalarType::F16,
          .is_present = true},
      checker::ModifierValueView{
          .kind_id = "rounding",
          .value_kind = checker::ModifierValueKind::RoundingMode,
          .rounding_mode = RoundingMode::Rni,
          .is_present = false}};
  EXPECT_FALSE(checker::check_cvt_rule(modifiers, {}, half_context));
  modifiers[2].is_present = true;
  EXPECT_TRUE(checker::check_cvt_rule(modifiers, {}, half_context));
  modifiers[2].rounding_mode = RoundingMode::Rna;
  EXPECT_FALSE(checker::check_cvt_rule(modifiers, {}, half_context));
  modifiers[0].scalar_type = ScalarType::F16;
  modifiers[1].scalar_type = ScalarType::BF16;
  modifiers[2].is_present = false;
  EXPECT_TRUE(checker::check_cvt_rule(modifiers, {}, half_context));
}
TEST(CvtCrossHalfRounding, KeepsInputWordConversionIndependentOfOperationMode) {
  for (const auto& mode : half_integer_rounds) {
    auto source = cross_half_source(true, mode.spelling);
    source.replace(source.rfind(" b,h;"), 5, " b,1.5;");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    ASSERT_TRUE(resolveAndValidateModule(*ast));
    auto module = owned_cross_half(source);
    ASSERT_TRUE(module);
    auto fields = half_fields(*module->functions.front().body.back(), true);
    const auto& word = std::get<ResolvedImmediate>(fields.src.value);
    EXPECT_EQ(word.type, ScalarType::F16);
    EXPECT_EQ(word.bits, 0x3e00u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    EXPECT_EQ(fields.rounding.value, mode.value);
    expect_half_lifetime(*module);
  }
}
TEST(CvtCrossHalfRounding,
     UsesLogicalFormatSizeAndRetainsCarrierAndTargetLimits) {
  for (const bool forward : {false, true}) {
    for (const auto carrier : {"f16", "b16", "b32", "b128"}) {
      auto module = owned_cross_half(
          cross_half_source(forward, "rni", false, false, carrier));
      ASSERT_TRUE(module) << module.error().front().message;
      expect_half_lifetime(*module);
      auto fields =
          half_fields(*module->functions.front().body.back(), forward);
      const auto& reg = forward
                            ? std::get<ResolvedRegisterRef>(fields.src.value)
                            : fields.dst.value;
      const auto* metadata =
          base::find_scalar_type_metadata("." + std::string(carrier));
      ASSERT_NE(metadata, nullptr);
      EXPECT_EQ(reg.declared_type, metadata->type);
    }
    for (const auto carrier : {"s16", "u16", "f16", "b32"}) {
      auto source = cross_half_source(forward, "rni");
      source.replace(source.find(".reg .b16 b"), 11,
                     ".reg ." + std::string(carrier) + " b");
      if (std::string_view(carrier) == "b32")
        source.replace(source.find("mov.b16 b"), 9, "mov.b32 b");
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
  for (const bool forward : {false, true})
    for (const auto bad : {"v", "p"}) {
      auto source = cross_half_source(forward, "rni");
      source.insert(source.find("mov.b16 h"),
                    ".reg .v2 .b16 v; .reg .pred p; mov.b16 v.x,0x3e00; "
                    "mov.b16 v.y,0; mov.pred p,1; ");
      source.replace(source.rfind(forward ? " b,h;" : " h,b;"), 5,
                     forward ? " b," + std::string(bad) + ";"
                             : " " + std::string(bad) + ",b;");
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  auto source = cross_half_source(true, "rni", false, false, "b32");
  source.replace(source.find("9.3"), 3, "7.8");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  auto context = half_context;
  context.target.ptx_version = {
      7, 7};  // Synthetic instruction-local threshold, not a legal header.
  const auto direct = module->functions.front().body.back()->check(context);
  ASSERT_FALSE(direct);
  EXPECT_TRUE(std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::UnsupportedPtxVersion;
  }));
  auto low_sm = source;
  low_sm.replace(low_sm.find("sm_90"), 5, "sm_80");
  const auto low_ast = test_helpers::parseModule(low_sm);
  ASSERT_MODULE_PARSE_SUCCEEDS(low_ast);
  const auto low_sm_result = resolveAndValidateModule(*low_ast);
  ASSERT_FALSE(low_sm_result);
  EXPECT_TRUE(
      std::ranges::any_of(low_sm_result.error(), [](const auto& diagnostic) {
        return diagnostic.checker_kind ==
               checker::CheckDiagnosticKind::UnsupportedSmVersion;
      }));
  auto joint = source;
  joint.replace(joint.find("7.8"), 3, "7.7");
  const auto joint_ast = test_helpers::parseModule(joint);
  ASSERT_MODULE_PARSE_SUCCEEDS(joint_ast);
  EXPECT_FALSE(resolveAndValidateModule(
      *joint_ast));  // Header and instruction both need7.8.
  for (auto& region : module->header.regions)
    region.version = checker::PtxVersion{7, 7};
  for (auto& function : module->functions)
    function.source_version = checker::PtxVersion{7, 7};
  const auto retarget = validateModule(*module);
  ASSERT_FALSE(retarget);
  EXPECT_TRUE(std::ranges::any_of(retarget.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::UnsupportedPtxVersion;
  }));
  EXPECT_TRUE(
      std::ranges::none_of(retarget.error(), [](const auto& diagnostic) {
        return diagnostic.kind ==
               checker::CheckDiagnosticKind::ModuleSourceMismatch;
      }));
}
TEST(CvtCrossHalfRounding, PreservesAdjacentSameAndDifferentFormatRules) {
  const std::string prefix =
      ".version 9.3\n.target sm_90\n.entry kernel(){"
      ".reg .f16 h<2>; .reg .b16 b<2>; .reg .f32 f; .reg .f64 d; .reg .u32 i; ";
  const auto legal = test_helpers::parseModule(
      prefix +
      "cvt.rni.f16.f16 h1,h0; cvt.rni.bf16.bf16 b1,b0; "
      "cvt.rni.u32.f32 i,f; cvt.rn.f32.u32 f,i; cvt.rn.f32.f64 f,d; "
      "cvt.f64.f32 d,f;}");
  ASSERT_MODULE_PARSE_SUCCEEDS(legal);
  EXPECT_TRUE(resolveAndValidateModule(*legal));
  for (const auto instruction :
       {"cvt.rni.f32.f16 f,h0;", "cvt.rni.f16.f32 h0,f;"}) {
    const auto ast = test_helpers::parseModule(prefix + instruction + "}");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
