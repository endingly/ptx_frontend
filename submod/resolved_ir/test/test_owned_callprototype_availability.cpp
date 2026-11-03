#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Real cataloged version/SM pairs isolate each minimum-only prototype feature. */
struct PrototypeFeature {
  /** Written suffix and diagnostic subject; empty suffix denotes the base role. */
  std::string_view suffix;
  std::string_view subject;
  /** Published introduction and immediately earlier selected version witness. */
  checker::PtxVersion version;
  checker::PtxVersion old_version;
  /** Real recognized sufficient and insufficient profiles, never guessed digits. */
  std::string_view target;
  std::string_view low_target;
};
constexpr std::array features{
    PrototypeFeature{"", ".callprototype", {2, 1}, {2, 0}, "sm_20", "sm_13"},
    PrototypeFeature{".noreturn",
                     ".noreturn",
                     {6, 4},
                     {6, 3},
                     "sm_30",
                     "sm_20"},
    PrototypeFeature{".abi_preserve 1",
                     ".abi_preserve",
                     {9, 0},
                     {8, 8},
                     "sm_80",
                     "sm_75"},
    PrototypeFeature{".abi_preserve_control 2",
                     ".abi_preserve_control",
                     {9, 0},
                     {8, 8},
                     "sm_80",
                     "sm_75"},
    PrototypeFeature{".abi_preserve 1 .abi_preserve_control 2",
                     ".abi_preserve",
                     {9, 0},
                     {8, 8},
                     "sm_80",
                     "sm_75"}};

/** Format a complete minimal source without an independent address-size floor. */
std::string prototypeSource(std::string_view suffix,
                            checker::PtxVersion version,
                            std::string_view target) {
  return ".version " + std::to_string(version.major) + "." +
         std::to_string(version.minor) + "\n.target " + std::string(target) +
         "\n.entry kernel() { p: .callprototype _ () " + std::string(suffix) +
         "; ret; }\n";
}

/** Update every retained region/cache coherently without changing any prototype. */
void retargetPrototype(ResolvedModule& module, checker::PtxVersion version,
                       std::string_view target) {
  for (auto& region : module.header.regions) {
    region.version = version;
    if (!region.target_options.empty())
      region.target_options.front() = target;
  }
  for (auto& function : module.functions) {
    function.source_version = version;
    function.source_target = target;
  }
}

/** Resolve transport with no surviving source/parser/AST allocation. */
std::optional<ResolvedModule> prototypeDraft(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast);
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveModuleOnly(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Find the retained feature's diagnostic extent without interpreting source history. */
SourceRange prototypeFeatureRange(
    const ResolvedCallPrototypeContract& prototype, std::string_view subject) {
  if (subject == ".abi_preserve" && prototype.abi_preserve)
    return prototype.abi_preserve->range;
  if (subject == ".abi_preserve_control" && prototype.abi_preserve_control)
    return prototype.abi_preserve_control->range;
  return prototype.range;
}

/** Require the intended located feature rule, not a context-cache or header failure. */
void expectPrototypeUnavailable(const checker::CheckResult& result,
                                SourceRange range, std::string_view subject) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.kind ==
               checker::CheckDiagnosticKind::UnsupportedAvailability &&
           error.range == range && error.message.starts_with(subject) &&
           error.message.find("requires PTX ISA >=") != std::string::npos;
  }));
  EXPECT_TRUE(std::ranges::all_of(result.error(), [](const auto& error) {
    return error.kind != checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

/** Current owned metadata must obey the same feature windows after source death. */
TEST(OwnedCallPrototypeAvailability, RechecksRealThresholdsAfterSourceDeath) {
  for (const auto& feature : features) {
    SCOPED_TRACE(feature.subject);
    std::optional<ResolvedModule> original;
    {
      PtxSyntaxParser parser(
          prototypeSource(feature.suffix, feature.version, feature.target));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved);
      original.emplace(std::move(*resolved));
    }
    ASSERT_TRUE(validateModule(*original));
    for (const bool version_change : {true, false}) {
      const auto version =
          version_change ? feature.old_version : feature.version;
      const auto target = version_change ? feature.target : feature.low_target;
      PtxSyntaxParser parser(prototypeSource(feature.suffix, version, target));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      EXPECT_FALSE(validateModule(*ast, *original));
      auto edited = *original;
      retargetPrototype(edited, version, target);
      const auto range = prototypeFeatureRange(
          edited.functions[0].call_prototypes[0], feature.subject);
      expectPrototypeUnavailable(validateModule(edited), range,
                                 feature.subject);
      const auto copy = edited;
      expectPrototypeUnavailable(validateModule(copy), range, feature.subject);
      ResolvedModule assigned;
      assigned = copy;
      expectPrototypeUnavailable(validateModule(assigned), range,
                                 feature.subject);
      auto function_copy = edited.functions[0];
      auto function_owner = edited;
      function_owner.functions[0] = function_copy;
      expectPrototypeUnavailable(validateModule(function_owner), range,
                                 feature.subject);
      ResolvedFunction function_assigned;
      function_assigned = function_copy;
      function_owner.functions[0] = std::move(function_assigned);
      expectPrototypeUnavailable(validateModule(function_owner), range,
                                 feature.subject);
      EXPECT_TRUE(validateModule(*original));
    }
  }
}
/** Supplied suffix requirements survive current field removal and missing version. */
TEST(OwnedCallPrototypeAvailability, KeepsSuppliedPartialSuffixRequirements) {
  for (const size_t index : {1u, 2u, 3u}) {
    const auto& feature = features[index];
    SCOPED_TRACE(feature.subject);
    PtxSyntaxParser original_parser(
        prototypeSource(feature.suffix, feature.version, feature.target));
    const auto original_ast = original_parser.parseModule();
    ASSERT_TRUE(original_ast);
    ASSERT_TRUE(original_ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*original_ast);
    ASSERT_TRUE(module);
    auto& prototype = module->functions[0].call_prototypes[0];
    prototype.is_noreturn = false;
    prototype.signature.is_noreturn = false;
    prototype.abi_preserve.reset();
    prototype.abi_preserve_control.reset();
    ASSERT_TRUE(validateModule(*module));
    auto supplied =
        prototypeSource(feature.suffix, feature.version, feature.low_target);
    supplied.erase(0, supplied.find('\n') + 1);
    PtxSyntaxParser replacement(supplied);
    const auto ast = replacement.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
    const auto& source_prototype =
        std::get<syntax_ast::AstCallPrototype>(function.body.front());
    const auto range = index == 1 ? source_prototype.noreturn_directive->range
                       : index == 2
                           ? source_prototype.abi_preserve->range
                           : source_prototype.abi_preserve_control->range;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      const auto result = validateModule(*ast, *module, policy);
      expectPrototypeUnavailable(result, range, feature.subject);
      if (policy == ModuleValidationPolicy::RequireCompleteContext)
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
          return error.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
    }
  }
}

/** Optional known facts are conjunctive minima, not a fabricated full target. */
TEST(OwnedCallPrototypeAvailability, ChecksEveryPartialContextPartition) {
  for (size_t index = 0; index < 4; ++index) {
    const auto& feature = features[index];
    for (unsigned mode = 0; mode < 7; ++mode) {
      SCOPED_TRACE(std::string(feature.subject) + "/" + std::to_string(mode));
      const bool has_version = mode == 0 || mode == 1 || mode == 5;
      const bool has_target = mode == 2 || mode == 3 || mode == 6;
      const bool feature_failure = mode == 0 || mode == 2;
      const auto version = mode == 0 ? feature.old_version : feature.version;
      const auto target = mode == 2 ? feature.low_target : feature.target;
      std::string source;
      if (has_version)
        source += ".version " + std::to_string(version.major) + "." +
                  std::to_string(version.minor) + "\n";
      if (has_target)
        source += ".target " + std::string(target) + "\n";
      source += ".entry kernel() { p: .callprototype _ () " +
                std::string(feature.suffix) + "; ret; }\n";
      // Build actual owned data from an admissible partial-context donor, then
      // change only the known failing fact. Bad source may reject before a draft.
      auto donor_source = source;
      if (mode == 0) {
        const auto old =
            std::to_string(version.major) + "." + std::to_string(version.minor);
        donor_source.replace(donor_source.find(old), old.size(),
                             std::to_string(feature.version.major) + "." +
                                 std::to_string(feature.version.minor));
      }
      if (mode == 2)
        donor_source.replace(donor_source.find(target), target.size(),
                             feature.target);
      auto module =
          prototypeDraft(prototypeSource("", feature.version, feature.target));
      ASSERT_TRUE(module);
      {
        auto donor = prototypeDraft(donor_source);
        ASSERT_TRUE(donor);
        ASSERT_TRUE(
            validateModule(*donor, ModuleValidationPolicy::AvailableContext));
        *module = *donor;
      }
      if (mode == 0) {
        for (auto& region : module->header.regions)
          region.version = version;
        for (auto& function : module->functions)
          function.source_version = version;
      }
      if (mode == 2) {
        for (auto& region : module->header.regions)
          if (!region.target_options.empty())
            region.target_options.front() = target;
        for (auto& function : module->functions)
          function.source_target = target;
      }
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto source_draft = resolveModuleOnly(*ast);
      if (!source_draft) {
        EXPECT_TRUE(feature_failure);
        EXPECT_FALSE(source_draft.error().empty());
      }
      const auto range = prototypeFeatureRange(
          module->functions[0].call_prototypes[0], feature.subject);
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        for (const auto& result : {validateModule(*module, policy),
                                   validateModule(*ast, *module, policy)}) {
          if (feature_failure)
            expectPrototypeUnavailable(result, range, feature.subject);
          else if (policy == ModuleValidationPolicy::AvailableContext)
            EXPECT_TRUE(result);
          else {
            ASSERT_FALSE(result);
            EXPECT_TRUE(
                std::ranges::any_of(result.error(), [](const auto& error) {
                  return error.kind ==
                         checker::CheckDiagnosticKind::MissingValidationContext;
                }));
            EXPECT_FALSE(
                std::ranges::any_of(result.error(), [](const auto& error) {
                  return error.message.find("on .callprototype requires") !=
                             std::string::npos ||
                         error.message.starts_with(".callprototype requires");
                }));
          }
        }
      }
      std::cout << "partial-prototype feature=" << index << " mode=" << mode
                << " source-clean=1 source-draft=" << bool(source_draft)
                << " source-owned=" << (source_draft ? "DRAFT" : "N_A")
                << " independent-donor-source-dead=1\n";
    }
  }
}

/** No supported suffix combination is mutually exclusive, including nested unused metadata. */
TEST(OwnedCallPrototypeAvailability, PreservesAllEightSuffixCombinations) {
  for (unsigned mask = 0; mask < 8; ++mask) {
    std::string suffix;
    if (mask & 1)
      suffix += ".noreturn ";
    if (mask & 2)
      suffix += ".abi_preserve 1 ";
    if (mask & 4)
      suffix += ".abi_preserve_control 2 ";
    auto source = prototypeSource(suffix, {9, 0}, "sm_80");
    if (mask & 1) {
      source.insert(source.find("p:"), "{ ");
      source.insert(source.find("ret;"), "} ");
    }
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto module = prototypeDraft(source);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
    const auto& prototype = module->functions[0].call_prototypes[0];
    EXPECT_EQ(prototype.is_noreturn, bool(mask & 1));
    EXPECT_EQ(prototype.signature.is_noreturn, bool(mask & 1));
    EXPECT_EQ(bool(prototype.abi_preserve), bool(mask & 2));
    EXPECT_EQ(bool(prototype.abi_preserve_control), bool(mask & 4));
    if (prototype.abi_preserve) {
      EXPECT_EQ(prototype.abi_preserve->count, 1u);
      EXPECT_FALSE(prototype.abi_preserve->control_registers);
    }
    if (prototype.abi_preserve_control) {
      EXPECT_EQ(prototype.abi_preserve_control->count, 2u);
      EXPECT_TRUE(prototype.abi_preserve_control->control_registers);
    }
  }
}

/** Current typed fields cannot be hidden by source that originally omitted them. */
TEST(OwnedCallPrototypeAvailability,
     RechecksCurrentFeaturePresenceWithOriginalSource) {
  const auto source = prototypeSource("", {6, 4}, "sm_30");
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  auto& prototype = module->functions[0].call_prototypes[0];
  prototype.abi_preserve =
      ResolvedAbiPreservationContract{1, false, prototype.range};
  expectPrototypeUnavailable(validateModule(*module), prototype.range,
                             ".abi_preserve");
  expectPrototypeUnavailable(validateModule(*ast, *module), prototype.range,
                             ".abi_preserve");
  retargetPrototype(*module, {9, 0}, "sm_80");
  EXPECT_TRUE(validateModule(*module));
  const auto modern = prototypeSource("", {9, 0}, "sm_80");
  PtxSyntaxParser modern_parser(modern);
  const auto modern_ast = modern_parser.parseModule();
  ASSERT_TRUE(modern_ast && modern_ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*modern_ast, *module));
  expectPrototypeUnavailable(validateModule(*ast, *module), prototype.range,
                             ".abi_preserve");
}

/** Only the function-selected region applies; unknown targets clear prior recognized facts. */
TEST(OwnedCallPrototypeAvailability,
     UsesSelectedRegionsAndResetsUnknownProfiles) {
  auto source = prototypeSource(".abi_preserve 1", {9, 0}, "sm_80");
  source +=
      ".target sm_80\n.entry later() { p: .callprototype _ () .abi_preserve 1; "
      "ret; }\n";
  auto module = prototypeDraft(source);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  ASSERT_EQ(module->functions.size(), 2u);
  const auto index = *module->functions[1].source_region;
  const auto first_index = module->functions[0].source_region;
  module->header.regions[index].target_options[0] = "sm_75";
  module->functions[1].source_target = "sm_75";
  const auto range =
      module->functions[1].call_prototypes[0].abi_preserve->range;
  const auto result = validateModule(*module);
  expectPrototypeUnavailable(result, range, ".abi_preserve");
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(module->functions[0].source_region, first_index);
  source.replace(source.rfind("sm_80"), 5, "sm_75");
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  expectPrototypeUnavailable(validateModule(*ast, *module), range,
                             ".abi_preserve");
  const auto unknown =
      ".version 9.0\n.target sm_75\n.entry early() { ret; }\n.target sm_19\n"
      ".entry late() { p: .callprototype _ () .abi_preserve 1; ret; }\n";
  auto unknown_module = prototypeDraft(unknown);
  ASSERT_TRUE(unknown_module);
  PtxSyntaxParser unknown_parser(unknown);
  const auto unknown_ast = unknown_parser.parseModule();
  ASSERT_TRUE(unknown_ast && unknown_ast.diagnostics.empty());
  for (const auto& checked :
       {validateModule(*unknown_module,
                       ModuleValidationPolicy::AvailableContext),
        validateModule(*unknown_ast, *unknown_module,
                       ModuleValidationPolicy::AvailableContext)}) {
    ASSERT_FALSE(checked);
    EXPECT_TRUE(std::ranges::any_of(checked.error(), [](const auto& error) {
      return error.kind == checker::CheckDiagnosticKind::UnknownTarget;
    }));
    EXPECT_FALSE(std::ranges::any_of(checked.error(), [](const auto& error) {
      return error.message.find("on .callprototype requires") !=
             std::string::npos;
    }));
  }
}

/** Typed indirect calls and cloned instructions keep valid signatures and metadata identities. */
TEST(OwnedCallPrototypeAvailability, PreservesIndirectAbiAndInstructionClones) {
  const auto source =
      ".version 9.0\n.target sm_80\n.entry kernel() { .reg .u64 fp; .reg .u32 "
      "r,x;\n"
      "p: .callprototype (.reg .u32 result) _ (.reg .u32 value) .abi_preserve "
      "1 .abi_preserve_control 2;\n"
      "mov.u64 fp, 0; mov.u32 x, 1; call (r), fp, (x), p; ret; }\n";
  auto module = prototypeDraft(source);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  ASSERT_EQ(module->functions[0].body.size(), 4u);
  const auto original_proto = module->functions[0].call_prototypes[0];
  module->functions[0].body[2] = module->functions[0].body[2]->clone();
  EXPECT_EQ(module->functions[0].call_prototypes[0].symbol_id,
            original_proto.symbol_id);
  EXPECT_EQ(module->functions[0].call_prototypes[0].scope_id,
            original_proto.scope_id);
  EXPECT_EQ(module->functions[0].call_prototypes[0].abi_preserve,
            original_proto.abi_preserve);
  EXPECT_TRUE(validateModule(*module));
  retargetPrototype(*module, {8, 8}, "sm_80");
  expectPrototypeUnavailable(validateModule(*module),
                             original_proto.abi_preserve->range,
                             ".abi_preserve");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
