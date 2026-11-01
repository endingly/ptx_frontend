#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <ptx_frontend/resolved_ir/model/data_movement/cvta.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>

namespace ptx_frontend::resolved_ir {
namespace {
using namespace checker;
constexpr std::array qualifiers{ParameterAddressQualifier::Default,
                                ParameterAddressQualifier::Entry,
                                ParameterAddressQualifier::Function};
/** Build the active current memory identity, retaining old aggregate defaults. */
ModifierValueView current(ParameterAddressQualifier qualifier) {
  return {.kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter,
          .is_present = true,
          .parameter_qualifier = qualifier};
}
/** Exact domain identity and availability selection remain independent of row order. */
TEST(ParameterModifierIdentity, ExactDomainAndAvailabilityMatrices) {
  const Context context{.target = {.ptx_version = {8, 2}, .sm_version = 90}};
  for (const auto expected : qualifiers) {
    const std::array domain{ModifierValueDomainDescriptor{
        .kind_id = "space",
        .value_kind = ModifierValueKind::MemoryStateSpace,
        .memory_state_space = MemoryStateSpace::Parameter,
        .parameter_qualifier = expected}};
    const std::array available{ModifierValueAvailabilityDescriptor{
        .kind_id = "space",
        .value_kind = ModifierValueKind::MemoryStateSpace,
        .memory_state_space = MemoryStateSpace::Parameter,
        .availability = {.minimum_ptx_version = {8, 3}},
        .parameter_qualifier = expected}};
    for (const auto actual : qualifiers) {
      const std::array views{current(actual)};
      EXPECT_EQ(bool(check_modifier_value_domain(domain, views, context)),
                expected == actual);
      EXPECT_EQ(
          bool(check_modifier_value_availability(available, views, context)),
          expected != actual);
    }
  }
  std::array rows{
      ModifierValueAvailabilityDescriptor{
          .kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter,
          .availability = {.minimum_ptx_version = {8, 3}},
          .parameter_qualifier = ParameterAddressQualifier::Entry},
      ModifierValueAvailabilityDescriptor{
          .kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter,
          .availability = {.minimum_ptx_version = {8, 3}},
          .parameter_qualifier = ParameterAddressQualifier::Function},
      ModifierValueAvailabilityDescriptor{
          .kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter}};
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(check_modifier_value_availability(
        rows, std::array{current(qualifiers[0])}, context));
    EXPECT_FALSE(check_modifier_value_availability(
        rows, std::array{current(qualifiers[1])}, context));
    EXPECT_FALSE(check_modifier_value_availability(
        rows, std::array{current(qualifiers[2])}, context));
    std::rotate(rows.begin(), rows.begin() + 1, rows.end());
  }
  const std::array domain{
      ModifierValueDomainDescriptor{
          .kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter},
      ModifierValueDomainDescriptor{
          .kind_id = "space",
          .value_kind = ModifierValueKind::MemoryStateSpace,
          .memory_state_space = MemoryStateSpace::Parameter,
          .parameter_qualifier = ParameterAddressQualifier::Function}};
  EXPECT_TRUE(check_modifier_value_domain(
      domain, std::array{current(qualifiers[0])}, context));
  EXPECT_FALSE(check_modifier_value_domain(
      domain, std::array{current(qualifiers[1])}, context));
  EXPECT_TRUE(check_modifier_value_domain(
      domain, std::array{current(qualifiers[2])}, context));
}
/** Malformed active metadata rejects even absent, unmatched, or on a high target. */
TEST(ParameterModifierIdentity, RejectsMalformedAndPreservesSemanticPresence) {
  const Context context{.target = {.ptx_version = {9, 3}, .sm_version = 90}};
  const std::array domain{ModifierValueDomainDescriptor{
      .kind_id = "space",
      .value_kind = ModifierValueKind::MemoryStateSpace,
      .memory_state_space = MemoryStateSpace::Parameter}};
  const std::array available{ModifierValueAvailabilityDescriptor{
      .kind_id = "space",
      .value_kind = ModifierValueKind::MemoryStateSpace,
      .memory_state_space = MemoryStateSpace::Parameter}};
  for (int mode = 0; mode < 5; ++mode) {
    auto view = current(qualifiers[0]);
    view.is_present = false;
    if (mode == 0)
      view.parameter_qualifier = std::nullopt;
    if (mode == 1)
      view.parameter_qualifier = static_cast<ParameterAddressQualifier>(255);
    if (mode == 2)
      view.memory_state_space = static_cast<MemoryStateSpace>(255);
    if (mode >= 3) {
      view.memory_state_space = MemoryStateSpace::Global;
      view.parameter_qualifier = qualifiers[mode - 2];
    }
    EXPECT_FALSE(
        check_modifier_value_domain(domain, std::array{view}, context));
    EXPECT_FALSE(check_modifier_value_availability(available, std::array{view},
                                                   context));
    EXPECT_FALSE(
        check_modifier_value_availability({}, std::array{view}, context));
  }
  for (int mode = 0; mode < 4; ++mode) {
    auto d = domain[0];
    auto a = available[0];
    if (mode == 0) {
      d.parameter_qualifier = a.parameter_qualifier =
          static_cast<ParameterAddressQualifier>(255);
    }
    if (mode == 1) {
      d.memory_state_space = a.memory_state_space =
          static_cast<MemoryStateSpace>(255);
    }
    if (mode >= 2) {
      d.memory_state_space = a.memory_state_space = MemoryStateSpace::Global;
      d.parameter_qualifier = a.parameter_qualifier = qualifiers[mode - 1];
    }
    EXPECT_FALSE(check_modifier_value_domain(
        std::array{d}, std::array{current(qualifiers[0])}, context));
    EXPECT_FALSE(check_modifier_value_availability(
        std::array{a}, std::array{current(qualifiers[0])}, context));
  }
  auto row = available[0];
  row.parameter_qualifier = qualifiers[1];
  row.availability.minimum_ptx_version = {8, 3};
  auto absent = current(qualifiers[1]);
  absent.is_present = false;
  const Context old{.target = {.ptx_version = {8, 2}, .sm_version = 90}};
  EXPECT_FALSE(check_modifier_value_availability(std::array{row},
                                                 std::array{absent}, old));
  absent.parameter_qualifier = qualifiers[0];
  row.parameter_qualifier = qualifiers[0];
  EXPECT_TRUE(check_modifier_value_availability(std::array{row},
                                                std::array{absent}, old));
}
/** Preserve source-aware and source-dead validation of the matched legal baselines. */
TEST(ParameterModifierIdentity, PlainHistoricalAndStagingSources) {
  const std::array<std::string, 2> sources{
      ".version 1.4\n.target sm_13\n.entry k(.param .u32 x){.reg .u32 %r; "
      "ld.param.u32 %r,[x]; ret;}",
      ".version 2.0\n.target sm_20\n.extern .func (.param .b32 y) f(.param "
      ".b32 x);\n.entry k(){.reg .b32 %r; .param .b32 a,b; st.param.b32 "
      "[a],%r; call (b),f,(a); ld.param.b32 %r,[b]; ret;}"};
  for (const auto& source : sources) {
    auto module = [&] {
      std::string input = source;
      PtxSyntaxParser parser(input);
      auto ast = parser.parseModule();
      EXPECT_TRUE(ast && ast.diagnostics.empty());
      auto owned = resolveModuleOnly(*ast);
      EXPECT_TRUE(owned);
      if (owned)
        EXPECT_TRUE(validateModule(*ast, *owned));
      auto strict = resolveAndValidateModule(*ast);
      EXPECT_TRUE(strict);
      return strict;
    }();
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
    auto copy = *module;
    ResolvedModule assigned;
    assigned = copy;
    auto function = std::ranges::find_if(
        assigned.functions,
        [](const auto& candidate) { return !candidate.body.empty(); });
    ASSERT_NE(function, assigned.functions.end());
    function->body.front() = function->body.front()->clone();
    EXPECT_TRUE(validateModule(copy));
    EXPECT_TRUE(validateModule(assigned));
  }
  PtxSyntaxParser parser(
      ".version 1.3\n.target sm_13\n.entry k(){.reg .u32 %r; .param .u32 x; "
      "ld.param.u32 %r,[x]; ret;}");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto legacy = resolveModuleOnly(*ast);
  EXPECT_FALSE(legacy);
  if (!legacy) {
    ASSERT_FALSE(legacy.error().empty());
    EXPECT_NE(legacy.error().front().message.find("Body-local call .param"),
              std::string::npos);
  }
  EXPECT_FALSE(resolveAndValidateModule(*ast));
}
/** Qualified source floors survive location erasure; current coherent edits choose identity. */
TEST(ParameterModifierIdentity, QualifiedSourcesAndCurrentAddress) {
  for (const auto& suffix : {std::string("entry"), std::string("func")}) {
    const auto source = std::string(".version 8.3\n.target sm_70\n") +
                        (suffix == "entry" ? ".entry" : ".func") +
                        " k(.param .u32 x){.reg .u32 %r; ld.param::" + suffix +
                        ".u32 %r,[x];}";
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto older_source = source;
    older_source.replace(older_source.find("8.3"), 3, "8.2");
    PtxSyntaxParser older_parser(older_source);
    auto older_ast = older_parser.parseModule();
    ASSERT_TRUE(older_ast);
    auto older_owned = resolveModuleOnly(*older_ast);
    ASSERT_TRUE(older_owned);
    EXPECT_FALSE(resolveAndValidateModule(*older_ast));
    EXPECT_FALSE(validateModule(*older_ast, *older_owned));
    EXPECT_FALSE(validateModule(*older_owned));
    EXPECT_TRUE(validateModule(*ast, *module));
    EXPECT_TRUE(validateModule(*module));
    auto& ld = dynamic_cast<LdExplicitScalar&>(
        *module->functions.front().body.front());
    EXPECT_EQ(ld.address.value.parameter_qualifier,
              suffix == "entry" ? ParameterAddressQualifier::Entry
                                : ParameterAddressQualifier::Function);
    EXPECT_EQ(
        std::get<ResolvedSymbolRef>(ld.address.value.base).parameter_qualifier,
        ParameterAddressQualifier::Default);
    Context context{.target = {.ptx_version = {8, 2}, .sm_version = 70}};
    EXPECT_FALSE(ld.check(context));
    ld.state_space.locs.clear();
    EXPECT_FALSE(ld.check(context));
    context.target.ptx_version = {8, 3};
    EXPECT_TRUE(ld.check(context));
    ld.address.value.parameter_qualifier = qualifiers[0];
    context.target.ptx_version = {8, 2};
    EXPECT_TRUE(ld.check(context));
    // Current typed edits do not rewrite the retained source association.
    EXPECT_TRUE(validateModule(*ast, *module));
    auto mismatched_source = source;
    mismatched_source.replace(mismatched_source.find("param::" + suffix),
                              7 + suffix.size(), "param");
    PtxSyntaxParser mismatch_parser(mismatched_source);
    auto mismatched_ast = mismatch_parser.parseModule();
    ASSERT_TRUE(mismatched_ast);
    EXPECT_FALSE(validateModule(*mismatched_ast, *module));
    ld.address.value.parameter_qualifier =
        static_cast<ParameterAddressQualifier>(255);
    EXPECT_FALSE(ld.check(context));
  }
}
/** The store domain admits plain/Function identities and rejects Entry independently. */
TEST(ParameterModifierIdentity, StoreCurrentDomainAndUnusedPayloadMembers) {
  PtxSyntaxParser parser("st.param.u32 [%rd0], %r0;");
  auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  auto resolved = resolveSt(*ast);
  ASSERT_TRUE(resolved);
  auto& st = dynamic_cast<StExplicitScalar&>(**resolved);
  checker::Context context{.target = {.ptx_version = {8, 3}, .sm_version = 70}};
  st.address.value.parameter_qualifier = ParameterAddressQualifier::Default;
  EXPECT_TRUE(st.check(context));
  st.address.value.parameter_qualifier = ParameterAddressQualifier::Function;
  EXPECT_TRUE(st.check(context));
  st.address.value.parameter_qualifier = ParameterAddressQualifier::Entry;
  EXPECT_FALSE(st.check(context));
  const std::array domain{ModifierValueDomainDescriptor{
      .kind_id = "flag", .value_kind = ModifierValueKind::Bool}};
  const std::array available{ModifierValueAvailabilityDescriptor{
      .kind_id = "flag", .value_kind = ModifierValueKind::Bool}};
  const std::array view{ModifierValueView{
      .kind_id = "flag",
      .value_kind = ModifierValueKind::Bool,
      .memory_state_space = static_cast<MemoryStateSpace>(255),
      .is_present = true,
      .parameter_qualifier = std::nullopt}};
  EXPECT_TRUE(check_modifier_value_domain(domain, view, context));
  EXPECT_TRUE(check_modifier_value_availability(available, view, context));
}
/** Fixed facts remain authoritative across register, selected symbol, and address forms. */
TEST(ParameterModifierIdentity, FixedCanonicalAndFrameDerivedPayloads) {
  for (const auto width : {32, 64}) {
    const auto suffix = std::to_string(width);
    const auto reg = width == 32 ? "%r" : "%rd";
    for (const auto offset : {false, true}) {
      const auto source =
          std::string(
              ".version 7.7\n.target sm_70\n.entry k(.param .u64 x){.reg .u") +
          suffix + " " + reg + "; cvta.param.u" + suffix + " " + reg + ",x" +
          (offset ? "+4" : "") + ";}";
      PtxSyntaxParser parser(source);
      auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(validateModule(*ast, *module));
      EXPECT_TRUE(validateModule(*module));
      checker::Context context{
          .target = {.ptx_version = {7, 7}, .sm_version = 70}};
      auto& instruction = *module->functions.front().body.front();
      auto mutate = [&](auto& cvta) {
        if (auto* symbol = std::get_if<ResolvedSymbolRef>(&cvta.src.value)) {
          EXPECT_EQ(symbol->parameter_qualifier,
                    ParameterAddressQualifier::Entry);
          symbol->parameter_qualifier =
              static_cast<ParameterAddressQualifier>(255);
        } else {
          auto& address = std::get<ResolvedAddress>(cvta.src.value);
          EXPECT_EQ(address.parameter_qualifier,
                    ParameterAddressQualifier::Entry);
          EXPECT_EQ(
              std::get<ResolvedSymbolRef>(address.base).parameter_qualifier,
              ParameterAddressQualifier::Default);
          address.parameter_qualifier =
              static_cast<ParameterAddressQualifier>(255);
        }
      };
      if (width == 32)
        mutate(dynamic_cast<CvtaParamU32&>(instruction));
      else
        mutate(dynamic_cast<CvtaParamU64&>(instruction));
      EXPECT_FALSE(instruction.check(context));
    }
  }
  for (const auto offset : {false, true}) {
    const auto source = std::string(
                            ".version 8.3\n.target sm_70\n.entry k(.param .u64 "
                            "x){.reg .u64 %rd; cvta.param::entry.u64 %rd,x") +
                        (offset ? "+4" : "") + ";}";
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*ast, *module));
    EXPECT_TRUE(validateModule(*module));
    auto& cvta = dynamic_cast<CvtaParamEntryU64&>(
        *module->functions.front().body.front());
    checker::Context context{
        .target = {.ptx_version = {8, 2}, .sm_version = 70}};
    EXPECT_FALSE(cvta.check(context));
    context.target.ptx_version = {8, 3};
    EXPECT_TRUE(cvta.check(context));
    if (auto* symbol = std::get_if<ResolvedSymbolRef>(&cvta.src.value))
      symbol->parameter_qualifier = ParameterAddressQualifier::Default;
    else
      std::get<ResolvedAddress>(cvta.src.value).parameter_qualifier =
          ParameterAddressQualifier::Default;
    EXPECT_FALSE(cvta.check(context));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
