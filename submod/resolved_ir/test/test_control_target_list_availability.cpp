#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Isolate unused target-list presence with real contexts and no address-size directive. */
std::string targetListSource(bool calls, std::string_view version,
                             std::string_view target, bool nested = false,
                             bool include_list = true) {
  std::string source;
  if (!version.empty())
    source += ".version " + std::string(version) + "\n";
  if (!target.empty())
    source += ".target " + std::string(target) + "\n";
  source += ".func f();\n.entry kernel() {\n";
  if (nested)
    source += "{\n";
  if (include_list)
    source +=
        calls ? "targets: .calltargets f;\n" : "targets: .branchtargets L;\n";
  source += "L: ret;\n";
  if (nested)
    source += "}\n";
  return source + "}\n";
}

/** Strict matched positives establish legal unused directives before threshold negatives. */
TEST(ControlTargetListAvailability, FreshStrictUnusedControls) {
  for (const bool calls : {false, true}) {
    SCOPED_TRACE(calls);
    PtxSyntaxParser parser(targetListSource(calls, "2.1", "sm_20"));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
    ASSERT_EQ(module->functions.size(), 2);
    EXPECT_EQ(module->functions.back().call_target_sets.size(), calls ? 1 : 0);
    EXPECT_EQ(module->functions.back().branch_target_sets.size(),
              calls ? 0 : 1);
  }
}

/** Both published minima apply without a branch or indirect-call consumer. */
TEST(ControlTargetListAvailability, RejectsSourceBelowEitherPublishedMinimum) {
  for (const bool calls : {false, true})
    for (const bool old_version : {false, true}) {
      SCOPED_TRACE(calls);
      SCOPED_TRACE(old_version);
      PtxSyntaxParser parser(targetListSource(
          calls, old_version ? "2.0" : "2.1", old_version ? "sm_20" : "sm_13"));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
}
/** Produce an owning module after all source, parser and AST storage dies. */
std::optional<ResolvedModule> targetListOwned(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveModuleOnly(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Borrow the exact declaration extent without treating consumers as producers. */
SourceRange targetListRange(const ResolvedFunction& function, bool calls) {
  return calls ? function.call_target_sets.front().range
               : function.branch_target_sets.front().range;
}

/** Change only known context facts, retaining every region/function cache association. */
void retargetTargetList(ResolvedModule& module,
                        std::optional<checker::PtxVersion> version,
                        std::optional<std::string> target) {
  for (auto& region : module.header.regions) {
    region.version = version;
    if (!region.target_options.empty() && target)
      region.target_options.front() = *target;
  }
  for (auto& function : module.functions) {
    function.source_version = version;
    function.source_target = target;
  }
}

/** Require the located published minima, rather than a source/cache/signature failure. */
void expectTargetListUnavailable(const checker::CheckResult& result,
                                 SourceRange range, bool calls) {
  ASSERT_FALSE(result);
  const std::string message =
      std::string(calls ? ".calltargets" : ".branchtargets") +
      " requires PTX ISA >= 2.1 and SM >= 20.";
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.kind ==
               checker::CheckDiagnosticKind::UnsupportedAvailability &&
           error.range == range && error.message == message;
  }));
  EXPECT_TRUE(std::ranges::none_of(result.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

/** Actual source-dead nested records survive value copies and coherent context changes. */
TEST(ControlTargetListAvailability, RechecksNestedOwnedCopiesAndAssignments) {
  for (const bool calls : {false, true}) {
    auto original =
        targetListOwned(targetListSource(calls, "2.1", "sm_20", true));
    ASSERT_TRUE(original);
    ASSERT_TRUE(validateModule(*original));
    const auto range = targetListRange(original->functions.back(), calls);
    for (const bool old_version : {false, true}) {
      auto edited = *original;
      retargetTargetList(
          edited,
          old_version ? checker::PtxVersion{2, 0} : checker::PtxVersion{2, 1},
          old_version ? "sm_20" : "sm_13");
      expectTargetListUnavailable(validateModule(edited), range, calls);
      const auto copied = edited;
      ResolvedModule assigned = *original;
      assigned = copied;
      auto moved = std::move(assigned);
      for (auto& function : moved.functions)
        for (auto& instruction : function.body)
          instruction = instruction->clone();
      expectTargetListUnavailable(validateModule(copied), range, calls);
      expectTargetListUnavailable(validateModule(moved), range, calls);
      EXPECT_TRUE(validateModule(*original));
    }
    auto plain =
        targetListOwned(targetListSource(calls, "2.0", "sm_20", true, false));
    ASSERT_TRUE(plain);
    EXPECT_TRUE(validateModule(*plain));
  }
}

/** Optional facts remain independent, including unknown real-catalog lookup results. */
TEST(ControlTargetListAvailability, ChecksEachPartialContextInBothRoutes) {
  /** Each row separates known failure, absent prerequisites and unknown target diagnostics. */
  struct Context {
    /** Written optional context; empty text means the corresponding directive is absent. */
    std::string_view version;
    std::string_view target;
    /** A known prerequisite fails independently of the other prerequisite. */
    bool unavailable;
    /** Target spelling has no recognized profile and retains UnknownTarget diagnostics. */
    bool unknown;
  };
  const Context cases[] = {
      {"2.1", "sm_20", false, false}, {"2.0", "sm_20", true, false},
      {"2.1", "sm_13", true, false},  {"2.0", "", true, false},
      {"", "sm_13", true, false},     {"2.1", "", false, false},
      {"", "sm_20", false, false},    {"", "", false, false},
      {"2.0", "sm_999", true, true},  {"2.1", "sm_999", false, true}};
  for (const bool calls : {false, true})
    for (const auto& context : cases) {
      SCOPED_TRACE(std::string(context.version) + "/" +
                   std::string(context.target));
      const auto donor_version =
          context.version == "2.0" ? "2.1" : context.version;
      const auto donor_target =
          context.target == "sm_13" ? "sm_20" : context.target;
      auto module =
          targetListOwned(targetListSource(calls, donor_version, donor_target));
      ASSERT_TRUE(module);
      const auto version = context.version.empty()
                               ? std::optional<checker::PtxVersion>{}
                               : std::optional{context.version == "2.0"
                                                   ? checker::PtxVersion{2, 0}
                                                   : checker::PtxVersion{2, 1}};
      const auto target = context.target.empty()
                              ? std::optional<std::string>{}
                              : std::optional{std::string(context.target)};
      retargetTargetList(*module, version, target);
      PtxSyntaxParser parser(
          targetListSource(calls, context.version, context.target));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto source_draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(source_draft);
      const auto range = targetListRange(module->functions.back(), calls);
      const auto source_range =
          targetListRange(source_draft->functions.back(), calls);
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        const auto owned = validateModule(*module, policy);
        const auto supplied = validateModule(*ast, *module, policy);
        const auto source = validateModule(*ast, *source_draft, policy);
        if (context.unavailable) {
          expectTargetListUnavailable(owned, range, calls);
          expectTargetListUnavailable(supplied, range, calls);
          expectTargetListUnavailable(source, source_range, calls);
        } else if (context.unknown) {
          for (const auto* result : {&owned, &supplied, &source}) {
            ASSERT_FALSE(*result);
            EXPECT_TRUE(
                std::ranges::any_of(result->error(), [](const auto& error) {
                  return error.kind ==
                         checker::CheckDiagnosticKind::UnknownTarget;
                }));
            EXPECT_TRUE(
                std::ranges::none_of(result->error(), [](const auto& error) {
                  return error.message.find(
                             "requires PTX ISA >= 2.1 and SM >= 20") !=
                         std::string::npos;
                }));
          }
        } else if (policy == ModuleValidationPolicy::AvailableContext ||
                   (!context.version.empty() && !context.target.empty())) {
          EXPECT_TRUE(owned);
          EXPECT_TRUE(supplied);
          EXPECT_TRUE(source);
        } else {
          for (const auto* result : {&owned, &supplied, &source}) {
            ASSERT_FALSE(*result);
            EXPECT_TRUE(
                std::ranges::any_of(result->error(), [](const auto& error) {
                  return error.kind ==
                         checker::CheckDiagnosticKind::MissingValidationContext;
                }));
          }
        }
      }
    }
}

/** A present supplied declaration cannot disappear when the current vector is cleared. */
TEST(ControlTargetListAvailability, ChecksSuppliedPresenceAfterCurrentRemoval) {
  for (const bool calls : {false, true}) {
    auto module =
        targetListOwned(targetListSource(calls, "2.1", "sm_20", true));
    ASSERT_TRUE(module);
    if (calls)
      module->functions.back().call_target_sets.clear();
    else
      module->functions.back().branch_target_sets.clear();
    ASSERT_TRUE(validateModule(*module));
    for (const bool old_version : {false, true}) {
      PtxSyntaxParser parser(
          targetListSource(calls, old_version ? "2.0" : "2.1",
                           old_version ? "sm_20" : "sm_13", true));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft);
      expectTargetListUnavailable(
          validateModule(*ast, *module),
          targetListRange(draft->functions.back(), calls), calls);
    }
  }
}

/** Public edited-AST rebinding adds real current metadata without rewriting source identity. */
TEST(ControlTargetListAvailability,
     ChecksCurrentPresenceWithOriginalListFreeSource) {
  for (const bool calls : {false, true}) {
    const std::string declaration =
        calls ? "targets: .calltargets f;" : "targets: .branchtargets L;";
    const auto source =
        ".version 2.0\n.target sm_20\n.func f();\n.entry kernel() {\nL: "
        "ret;\n" +
        std::string(declaration.size(), ' ') + "\n}\n";
    PtxSyntaxParser original_parser(source);
    const auto ast = original_parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto current = resolveAndValidateModule(*ast);
    ASSERT_TRUE(current);
    auto donor_source = source;
    donor_source.replace(
        donor_source.find(std::string(declaration.size(), ' ')),
        declaration.size(), declaration);
    donor_source.replace(donor_source.find("2.0"), 3, "2.1");
    PtxSyntaxParser donor_parser(donor_source);
    const auto donor_ast = donor_parser.parseModule();
    ASSERT_TRUE(donor_ast && donor_ast.diagnostics.empty());
    auto donor = resolveAndValidateModule(*donor_ast);
    ASSERT_TRUE(donor);
    // The appended metadata leaves every original symbol/scope/range unchanged.
    // Whole-table value replacement uses the public owning table, not a mutator.
    current->symbols = donor->symbols;
    current->functions.back().call_target_sets =
        donor->functions.back().call_target_sets;
    current->functions.back().branch_target_sets =
        donor->functions.back().branch_target_sets;
    const auto range = targetListRange(current->functions.back(), calls);
    expectTargetListUnavailable(validateModule(*current), range, calls);
    expectTargetListUnavailable(validateModule(*ast, *current), range, calls);
    retargetTargetList(*current, checker::PtxVersion{2, 1}, "sm_20");
    EXPECT_TRUE(validateModule(*current));
    // The supplied original context still governs current retained presence.
    expectTargetListUnavailable(validateModule(*ast, *current), range, calls);
  }
}

/** Only the exact function-selected region supplies each list's current facts. */
TEST(ControlTargetListAvailability,
     UsesSelectedRegionWithoutFirstRegionFallback) {
  for (const bool calls : {false, true}) {
    auto source = targetListSource(calls, "2.1", "sm_20");
    auto second = source.substr(source.find(".entry kernel()"));
    second.replace(second.find("kernel"), 6, "second");
    source += ".target sm_20\n" + second;
    auto module = targetListOwned(source);
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    const auto index = *module->functions.back().source_region;
    ASSERT_NE(index, *module->functions[1].source_region);
    module->header.regions[index].target_options.front() = "sm_13";
    module->functions.back().source_target = "sm_13";
    const auto range = targetListRange(module->functions.back(), calls);
    const auto result = validateModule(*module);
    expectTargetListUnavailable(result, range, calls);
    EXPECT_EQ(std::ranges::count_if(
                  result.error(),
                  [](const auto& error) {
                    return error.message.find(
                               "requires PTX ISA >= 2.1 and SM >= 20") !=
                           std::string::npos;
                  }),
              1);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(validateModule(*ast, *module));
    auto low_source = source;
    low_source.replace(low_source.rfind("sm_20"), 5, "sm_13");
    PtxSyntaxParser low_parser(low_source);
    const auto low_ast = low_parser.parseModule();
    ASSERT_TRUE(low_ast && low_ast.diagnostics.empty());
    expectTargetListUnavailable(validateModule(*low_ast, *module), range,
                                calls);
  }
}

/** Nonneutral list payload and existing member/scope/signature rules remain authoritative. */
TEST(ControlTargetListAvailability,
     PreservesPayloadIdentityAndIntegrityChecks) {
  for (const bool calls : {false, true}) {
    auto source = targetListSource(calls, "2.1", "sm_20");
    auto original = targetListOwned(source);
    ASSERT_TRUE(original);
    auto changed_source = source;
    changed_source.replace(changed_source.find("targets:"), 8, "changed:");
    PtxSyntaxParser changed_parser(changed_source);
    const auto changed = changed_parser.parseModule();
    ASSERT_TRUE(changed && changed.diagnostics.empty());
    const auto mismatch = validateModule(*changed, *original);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& error) {
      return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
    auto edited = *original;
    if (calls)
      edited.functions.back().call_target_sets.front().targets.clear();
    else
      edited.functions.back()
          .branch_target_sets.front()
          .targets.front()
          .symbol_id = edited.functions.back().symbol_id;
    EXPECT_FALSE(validateModule(edited));
    if (calls) {
      PtxSyntaxParser late_parser(
          ".version 2.1\n.target sm_20\n.entry kernel() { targets: "
          ".calltargets f; ret; }\n.func f();\n");
      const auto late = late_parser.parseModule();
      ASSERT_TRUE(late && late.diagnostics.empty());
      EXPECT_FALSE(resolveModuleOnly(*late));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
