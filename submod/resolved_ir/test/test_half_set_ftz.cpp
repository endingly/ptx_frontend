#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <string>
#include <string_view>
#include <utility>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Source cohorts retain their own comparison and physical-width domains. */
enum class SetFtzFamily { Bit, Integer, F64 };
/** One written source format with its exact physical declaration and family. */
struct SetFtzType {
  /** PTX suffix and declaration spelling without the leading dot. */
  std::string_view spelling;
  /** Exact expected source format, never a diagnostic case conversion. */
  ScalarType type;
  /** Existing generated plain/Boolean class pair. */
  SetFtzFamily family;
};
constexpr std::array set_ftz_types{
    SetFtzType{"b16", ScalarType::B16, SetFtzFamily::Bit},
    SetFtzType{"b32", ScalarType::B32, SetFtzFamily::Bit},
    SetFtzType{"b64", ScalarType::B64, SetFtzFamily::Bit},
    SetFtzType{"u16", ScalarType::U16, SetFtzFamily::Integer},
    SetFtzType{"u32", ScalarType::U32, SetFtzFamily::Integer},
    SetFtzType{"u64", ScalarType::U64, SetFtzFamily::Integer},
    SetFtzType{"s16", ScalarType::S16, SetFtzFamily::Integer},
    SetFtzType{"s32", ScalarType::S32, SetFtzFamily::Integer},
    SetFtzType{"s64", ScalarType::S64, SetFtzFamily::Integer},
    SetFtzType{"f64", ScalarType::F64, SetFtzFamily::F64}};
/** Initialize every real source and predicate independently of the result flag. */
std::string set_ftz_source(const SetFtzType& type,
                           std::string_view comparison = "eq",
                           std::string_view boolean = "", bool ftz = false) {
  const std::string initialization =
      type.family == SetFtzFamily::F64
          ? "mov.f64 a,1.0; mov.f64 b,2.0; "
          : "mov." + std::string(type.spelling) + " a,1; mov." +
                std::string(type.spelling) + " b,2; ";
  return ".version 4.2\n.target sm_53\n.entry kernel(){.reg .f16 h; .reg ." +
         std::string(type.spelling) + " a,b; .reg .pred p; " + initialization +
         "mov.pred p,1; set." + std::string(comparison) +
         (boolean.empty() ? std::string{} : "." + std::string(boolean)) +
         (ftz ? ".ftz" : "") + ".f16." + std::string(type.spelling) + " h,a,b" +
         (boolean.empty() ? ";}" : ",p;}");
}
/** Assert the exact selected class without touching the not-yet-present flag field. */
void expect_set_ftz_kind(const Instruction& instruction, const SetFtzType& type,
                         bool boolean) {
  const auto expected =
      type.family == SetFtzFamily::Bit
          ? (boolean ? SetHalfF16BitBoolean::kind : SetHalfF16Bit::kind)
      : type.family == SetFtzFamily::Integer
          ? (boolean ? SetHalfF16IntegerBoolean::kind : SetHalfF16Integer::kind)
          : (boolean ? SetHalfF16F64Boolean::kind : SetHalfF16F64::kind);
  EXPECT_EQ(instruction.instruction_kind(), expected);
}
TEST(HalfSetFtz, AdmitsWrittenFlagBesideMatchedOmissionControls) {
  for (const size_t index : {0u, 7u, 9u})
    for (const bool boolean : {false, true}) {
      const auto& type = set_ftz_types[index];
      const auto control = test_helpers::parseModule(
          set_ftz_source(type, "eq", boolean ? "and" : "", false));
      ASSERT_MODULE_PARSE_SUCCEEDS(control);
      const auto nominal = resolveAndValidateModule(*control);
      ASSERT_TRUE(nominal) << nominal.error().front().message;
      expect_set_ftz_kind(*nominal->functions.front().body.back(), type,
                          boolean);
      const auto ast = test_helpers::parseModule(
          set_ftz_source(type, "eq", boolean ? "and" : "", true));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
}
/** Borrow the six new flag fields and unchanged operands from a live instruction. */
struct SetFtzFields {
  /** Owned optional flag and exact source token locations. */
  WithLocs<bool>& ftz;
  /** Existing comparator and its source locations. */
  WithLocs<ComparisonOperator>& comparison;
  /** Physical F16 destination reference. */
  WithLocs<ResolvedRegisterRef>& dst;
  /** Ordered source values, independent of the FTZ permission. */
  WithLocs<RegOrImm>& src1;
  /** Second source follows the first in written operand order. */
  WithLocs<RegOrImm>& src2;
  /** Null for plain form; otherwise borrowed Boolean algorithm and predicate. */
  WithLocs<BooleanOperator>* boolean;
  /** Null for plain form; otherwise the borrowed predicate combination source. */
  WithLocs<ResolvedPredicateSource>* combine;
};
/** Inspect the closed class set without a new generated accessor or template. */
SetFtzFields set_ftz_fields(Instruction& instruction, const SetFtzType& type,
                            bool boolean) {
  expect_set_ftz_kind(instruction, type, boolean);
  if (type.family == SetFtzFamily::Bit) {
    if (boolean) {
      auto& form = dynamic_cast<SetHalfF16BitBoolean&>(instruction);
      EXPECT_EQ(form.stype.value, type.type);
      return {form.ftz,  form.comparison, form.dst,     form.src1,
              form.src2, &form.boolean,   &form.combine};
    }
    auto& form = dynamic_cast<SetHalfF16Bit&>(instruction);
    EXPECT_EQ(form.stype.value, type.type);
    return {form.ftz,  form.comparison, form.dst, form.src1,
            form.src2, nullptr,         nullptr};
  }
  if (type.family == SetFtzFamily::Integer) {
    if (boolean) {
      auto& form = dynamic_cast<SetHalfF16IntegerBoolean&>(instruction);
      EXPECT_EQ(form.stype.value, type.type);
      return {form.ftz,  form.comparison, form.dst,     form.src1,
              form.src2, &form.boolean,   &form.combine};
    }
    auto& form = dynamic_cast<SetHalfF16Integer&>(instruction);
    EXPECT_EQ(form.stype.value, type.type);
    return {form.ftz,  form.comparison, form.dst, form.src1,
            form.src2, nullptr,         nullptr};
  }
  if (boolean) {
    auto& form = dynamic_cast<SetHalfF16F64Boolean&>(instruction);
    EXPECT_EQ(form.stype, type.type);
    return {form.ftz,  form.comparison, form.dst,     form.src1,
            form.src2, &form.boolean,   &form.combine};
  }
  auto& form = dynamic_cast<SetHalfF16F64&>(instruction);
  EXPECT_EQ(form.stype, type.type);
  return {form.ftz,  form.comparison, form.dst, form.src1,
          form.src2, nullptr,         nullptr};
}
/** Complete instruction context matches the minimum valid source fixture. */
const checker::Context set_ftz_context{
    .target = {.ptx_version = {4, 2}, .sm_version = 53}};
/** Resolve strict IR while source text, parser and syntax AST die on return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_set_ftz(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Preserve optional flag state through independent module/function copy and clone. */
void expect_set_ftz_lifetime(const ResolvedModule& module, bool valid = true) {
  EXPECT_EQ(bool(module.functions.front().body.back()->check(set_ftz_context)),
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
  EXPECT_EQ(bool(function_assigned.body.back()->check(set_ftz_context)), valid);
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_EQ(bool(validateModule(assigned)), valid);
}
/** One exact comparator spelling and its typed operation. */
struct SetFtzComparison {
  /** Source suffix without a leading dot. */
  std::string_view spelling;
  /** Expected existing comparator enum. */
  ComparisonOperator value;
};
constexpr std::array set_ftz_comparisons{
    SetFtzComparison{"eq", ComparisonOperator::Eq},
    SetFtzComparison{"ne", ComparisonOperator::Ne},
    SetFtzComparison{"lt", ComparisonOperator::Lt},
    SetFtzComparison{"le", ComparisonOperator::Le},
    SetFtzComparison{"gt", ComparisonOperator::Gt},
    SetFtzComparison{"ge", ComparisonOperator::Ge},
    SetFtzComparison{"equ", ComparisonOperator::Equ},
    SetFtzComparison{"neu", ComparisonOperator::Neu},
    SetFtzComparison{"ltu", ComparisonOperator::Ltu},
    SetFtzComparison{"leu", ComparisonOperator::Leu},
    SetFtzComparison{"gtu", ComparisonOperator::Gtu},
    SetFtzComparison{"geu", ComparisonOperator::Geu},
    SetFtzComparison{"num", ComparisonOperator::Num},
    SetFtzComparison{"nan", ComparisonOperator::Nan}};
TEST(HalfSetFtz, ClosesAllWrittenAndOmittedSuffixProducts) {
  constexpr std::array<std::string_view, 4> booleans{"", "and", "or", "xor"};
  constexpr std::array boolean_values{BooleanOperator::Invalid,
                                      BooleanOperator::And, BooleanOperator::Or,
                                      BooleanOperator::Xor};
  size_t products = 0;
  for (const auto& type : set_ftz_types) {
    const size_t count = type.family == SetFtzFamily::Bit       ? 2
                         : type.family == SetFtzFamily::Integer ? 6
                                                                : 14;
    for (size_t comparison = 0; comparison < count; ++comparison)
      for (size_t boolean = 0; boolean < booleans.size(); ++boolean) {
        const auto& cmp = set_ftz_comparisons[comparison];
        const auto algorithm = booleans[boolean];
        ++products;
        auto omitted =
            owned_set_ftz(set_ftz_source(type, cmp.spelling, algorithm, false));
        auto written =
            owned_set_ftz(set_ftz_source(type, cmp.spelling, algorithm, true));
        ASSERT_TRUE(omitted) << omitted.error().front().message;
        ASSERT_TRUE(written) << written.error().front().message;
        const auto source = test_helpers::parseModule(
            set_ftz_source(type, cmp.spelling, algorithm, true));
        ASSERT_MODULE_PARSE_SUCCEEDS(source);
        const auto draft = resolveModuleOnly(*source);
        ASSERT_TRUE(draft);
        EXPECT_TRUE(validateModule(*source, *written));
        auto absent = set_ftz_fields(*omitted->functions.front().body.back(),
                                     type, boolean != 0);
        auto present = set_ftz_fields(*written->functions.front().body.back(),
                                      type, boolean != 0);
        EXPECT_FALSE(absent.ftz.value);
        EXPECT_TRUE(absent.ftz.locs.empty());
        EXPECT_TRUE(present.ftz.value);
        EXPECT_EQ(present.ftz.locs.size(), 1u);
        EXPECT_GT(present.ftz.locs.front().start.column, 0);
        EXPECT_EQ(absent.comparison.value, cmp.value);
        EXPECT_EQ(present.comparison.value, cmp.value);
        EXPECT_EQ(present.dst.value.symbol_id, absent.dst.value.symbol_id);
        EXPECT_EQ(present.dst.value.declared_type, ScalarType::F16);
        const auto& first = std::get<ResolvedRegisterRef>(present.src1.value);
        const auto& second = std::get<ResolvedRegisterRef>(present.src2.value);
        EXPECT_EQ(first.declared_type, type.type);
        EXPECT_EQ(second.declared_type, type.type);
        EXPECT_EQ(first.symbol_id,
                  std::get<ResolvedRegisterRef>(absent.src1.value).symbol_id);
        EXPECT_EQ(second.symbol_id,
                  std::get<ResolvedRegisterRef>(absent.src2.value).symbol_id);
        EXPECT_NE(first.symbol_id, second.symbol_id);
        if (boolean) {
          ASSERT_NE(present.boolean, nullptr);
          EXPECT_EQ(present.boolean->value, boolean_values[boolean]);
          EXPECT_EQ(absent.boolean->value, boolean_values[boolean]);
          const auto& predicate =
              std::get<ResolvedPredicate>(present.combine->value);
          EXPECT_EQ(predicate.register_ref.register_class,
                    ResolvedRegisterClass::Predicate);
          EXPECT_TRUE(predicate.register_ref.symbol_id);
        }
        expect_set_ftz_lifetime(*omitted);
        expect_set_ftz_lifetime(*written);
        const std::string family = type.family == SetFtzFamily::Bit ? "bit"
                                   : type.family == SetFtzFamily::Integer
                                       ? "integer"
                                       : "f64";
        std::cout << "CELL SETFTZ::set::set_half_f16_" << family
                  << (boolean ? "_boolean" : "")
                  << "::dtype=f16::stype=" << type.spelling
                  << "::cmp=" << cmp.spelling
                  << "::bool=" << (algorithm.empty() ? "none" : algorithm)
                  << "::ftz=written\n";
      }
  }
  EXPECT_EQ(products, 224u);
}
TEST(HalfSetFtz, PreservesCurrentPresenceAndSourceIdentityAfterAstDeath) {
  for (const size_t index : {0u, 7u, 9u})
    for (const bool boolean : {false, true}) {
      const auto& type = set_ftz_types[index];
      const auto algorithm = boolean ? "and" : "";
      auto omitted =
          owned_set_ftz(set_ftz_source(type, "eq", algorithm, false));
      auto written = owned_set_ftz(set_ftz_source(type, "eq", algorithm, true));
      ASSERT_TRUE(omitted && written);
      auto absent = set_ftz_fields(*omitted->functions.front().body.back(),
                                   type, boolean);
      auto present = set_ftz_fields(*written->functions.front().body.back(),
                                    type, boolean);
      const auto locations = present.ftz.locs;
      absent.ftz.value = true;
      expect_set_ftz_lifetime(*omitted, false);
      const auto invalid = validateModule(*omitted);
      ASSERT_FALSE(invalid);
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
            return diagnostic.message.find("Optional modifier 'ftz'") !=
                   std::string::npos;
          }));
      absent.ftz.value = false;
      expect_set_ftz_lifetime(*omitted);
      present.ftz.value = false;
      expect_set_ftz_lifetime(*written, false);
      present.ftz.locs.clear();
      expect_set_ftz_lifetime(*written);
      const auto original = test_helpers::parseModule(
          set_ftz_source(type, "eq", algorithm, true));
      ASSERT_MODULE_PARSE_SUCCEEDS(original);
      // Retained syntax matches; the current coherent operation may omit the flag.
      EXPECT_TRUE(validateModule(*original, *written));
      present.ftz.value = true;
      present.ftz.locs = locations;
      const auto replacement = test_helpers::parseModule(
          set_ftz_source(type, "eq", algorithm, false));
      ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
      ASSERT_TRUE(resolveAndValidateModule(*replacement));
      const auto mismatch = validateModule(*replacement, *written);
      ASSERT_FALSE(mismatch);
      EXPECT_TRUE(
          std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                       checker::CheckDiagnosticKind::ModuleSourceMismatch &&
                   diagnostic.message.find("Optional modifier 'ftz'") ==
                       std::string::npos;
          }));
      expect_set_ftz_lifetime(*written);
    }
}
TEST(HalfSetFtz, KeepsComparisonBooleanAndModifierOrderBoundaries) {
  const auto& bit = set_ftz_types[0];
  const auto& integer = set_ftz_types[7];
  for (const auto comparison : {"lt", "lo", "ls", "hi", "hs"}) {
    const auto ast =
        test_helpers::parseModule(set_ftz_source(bit, comparison, "", true));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto comparison : {"num", "nan", "lo", "ls", "hi", "hs"}) {
    const auto ast = test_helpers::parseModule(
        set_ftz_source(integer, comparison, "and", true));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto replacement : {" h,a,b;}", " h,a,b,1.0;}"}) {
    auto source = set_ftz_source(integer, "eq", "and", true);
    source.replace(source.rfind(" h,a,b,p;}"), 10, replacement);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  auto extra = set_ftz_source(integer, "eq", "", true);
  extra.replace(extra.rfind(" h,a,b;}"), 8, " h,a,b,p;}");
  const auto extra_ast = test_helpers::parseModule(extra);
  ASSERT_MODULE_PARSE_SUCCEEDS(extra_ast);
  EXPECT_FALSE(resolveAndValidateModule(*extra_ast));
  for (const auto replacement :
       {"set.eq.ftz.ftz.f16", "set.ftz.eq.f16", "set.eq.f16.ftz"}) {
    auto source = set_ftz_source(bit, "eq", "", true);
    source.replace(source.find("set.eq.ftz.f16"), 14, replacement);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
TEST(HalfSetFtz, PreservesOtherFtzProductionsAndTargetWindows) {
  const std::string prefix = ".version 9.3\n.target sm_100\n.entry kernel(){";
  constexpr std::array<std::pair<std::string_view, std::string_view>, 5>
      restricted{{{".reg .b16 d,a,b;", "set.eq.bf16.b16 d,a,b;"},
                  {".reg .b32 d,a,b;", "set.eq.bf16x2.bf16x2 d,a,b;"},
                  {".reg .u32 d; .reg .f64 a,b;", "set.eq.u32.f64 d,a,b;"},
                  {".reg .u32 d; .reg .b32 a,b;", "set.eq.u32.b32 d,a,b;"},
                  {".reg .u32 d; .reg .s32 a,b;", "set.lt.u32.s32 d,a,b;"}}};
  for (const auto& [declarations, instruction] : restricted) {
    const auto control = test_helpers::parseModule(
        prefix + std::string(declarations) + std::string(instruction) + "}");
    ASSERT_MODULE_PARSE_SUCCEEDS(control);
    ASSERT_TRUE(resolveAndValidateModule(*control));
    std::string ftz(instruction);
    ftz.insert(ftz.find('.', 4), ".ftz");
    const auto ast = test_helpers::parseModule(
        prefix + std::string(declarations) + ftz + "}");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  const auto retained = test_helpers::parseModule(
      prefix +
      ".reg .f16 h,a,b; .reg .f32 x,y; .reg .u32 r; .reg .f16x2 p,q,t; "
      "set.eq.ftz.f16.f16 h,a,b; set.eq.ftz.f16.f32 h,x,y; "
      "set.eq.ftz.u32.f32 r,x,y; set.eq.ftz.f16x2.f16x2 p,q,t;}");
  ASSERT_MODULE_PARSE_SUCCEEDS(retained);
  EXPECT_TRUE(resolveAndValidateModule(*retained));
  for (const size_t index : {0u, 7u, 9u})
    for (const bool boolean : {false, true}) {
      const auto source = set_ftz_source(set_ftz_types[index], "eq",
                                         boolean ? "and" : "", true);
      auto module = owned_set_ftz(source);
      ASSERT_TRUE(module);
      auto version = set_ftz_context;
      version.target.ptx_version = {
          4,
          1};  // Synthetic instruction context isolates PTX from SM53 header minimum.
      const auto early = module->functions.front().body.back()->check(version);
      ASSERT_FALSE(early);
      EXPECT_TRUE(
          std::ranges::any_of(early.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::UnsupportedPtxVersion;
          }));
      auto target_source = source;
      target_source.replace(target_source.find("sm_53"), 5, "sm_52");
      const auto ast = test_helpers::parseModule(target_source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto wrong_target = resolveAndValidateModule(*ast);
      ASSERT_FALSE(wrong_target);
      EXPECT_TRUE(
          std::ranges::any_of(wrong_target.error(), [](const auto& diagnostic) {
            return diagnostic.checker_kind ==
                   checker::CheckDiagnosticKind::UnsupportedSmVersion;
          }));
    }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
