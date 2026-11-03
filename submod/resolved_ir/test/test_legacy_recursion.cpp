#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "../src/ptx_call_graph.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Match the frozen self-call witnesses without an unrelated address-size or call-metadata floor. */
std::string recursionSource(std::string_view version, std::string_view target,
                            bool cycle = true, bool entry = true) {
  std::string source;
  if (!version.empty())
    source += ".version " + std::string(version) + "\n";
  if (!target.empty())
    source += ".target " + std::string(target) + "\n";
  source += cycle ? ".func f(){call f; ret;}\n" : ".func f(){ret;}\n";
  if (entry)
    source += ".entry k(){ret;}\n";
  return source;
}

/** Return owning instructions and bindings after the producing source/parser/AST dies. */
std::optional<ResolvedModule> recursionOwned(std::string source) {
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

/** Retarget every applicable region/cache while preserving actual call identities and ranges. */
void retargetRecursion(ResolvedModule& module, checker::PtxVersion version,
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

/** Modern recursion and same-legacy acyclic controls establish valid source contexts. */
TEST(LegacyRecursion, FreshModernAndAcyclicControls) {
  for (const auto& source : {recursionSource("2.0", "sm_20"),
                             recursionSource("1.3", "sm_13", false)}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Unused local definitions and current source-dead copies still expose self recursion. */
TEST(LegacyRecursion, RejectsLegacySelfAndUnusedSourceAndOwnedCopies) {
  for (const bool entry : {false, true}) {
    PtxSyntaxParser parser(recursionSource("1.3", "sm_13", true, entry));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  auto module = recursionOwned(recursionSource("2.0", "sm_20"));
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  retargetRecursion(*module, {1, 3}, "sm_13");
  const auto copied = *module;
  ResolvedModule assigned;
  assigned = copied;
  for (auto& function : assigned.functions)
    for (auto& instruction : function.body)
      instruction = instruction->clone();
  EXPECT_FALSE(validateModule(*module));
  EXPECT_FALSE(validateModule(copied));
  EXPECT_FALSE(validateModule(assigned));
}
/** Find the actual definition occurrence instead of a canonical prototype's first scope. */
size_t recursionDefinition(const ResolvedModule& module,
                           std::string_view name = "f") {
  for (size_t i = 0; i < module.functions.size(); ++i)
    if (!module.functions[i].is_prototype && module.functions[i].name == name)
      return i;
  ADD_FAILURE() << "Missing actual definition";
  return 0;
}

/** Preserve useful failure evidence without interpreting a generic rejection as recursion proof. */
std::string recursionMessages(const checker::CheckResult& result) {
  std::string messages;
  if (!result)
    for (const auto& error : result.error())
      messages += std::to_string(error.range.start.line) + ":" +
                  std::to_string(error.range.start.column) + " " +
                  error.message + "\n";
  return messages;
}

/** Require a located recursive relation rather than header, signature, or source-association failure. */
void expectRecursionUnavailable(const checker::CheckResult& result,
                                SourceRange range, bool possible = false) {
  ASSERT_FALSE(result);
  const std::string message =
      std::string(possible ? "Statically possible recursion"
                           : "Syntactic recursion") +
      " requires PTX ISA >= 2.0 and SM >= 20.";
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.kind ==
               checker::CheckDiagnosticKind::UnsupportedAvailability &&
           error.range == range && error.message == message;
  })) << recursionMessages(result);
  EXPECT_TRUE(std::ranges::none_of(result.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

/** Isolate each known minimum with real admissible headers, including partial context. */
TEST(LegacyRecursion, ChecksIndependentContextMinimaInBothValidators) {
  /** Legal header combinations distinguish missing facts from independently failing minima. */
  struct Context {
    /** Empty text omits its corresponding directive; sm999 remains an unknown catalog lookup. */
    std::string_view version, target;
    /** A known failed recursion prerequisite is not deferred by the other unknown fact. */
    bool unavailable;
  };
  const Context cases[] = {{"1.3", "sm_13", true},  {"2.0", "sm_20", false},
                           {"2.0", "sm_13", true},  {"1.3", "", true},
                           {"2.0", "", false},      {"", "sm_13", true},
                           {"", "sm_20", false},    {"", "", false},
                           {"1.3", "sm_999", true}, {"2.0", "sm_999", false}};
  auto modern = recursionOwned(recursionSource("2.0", "sm_20"));
  ASSERT_TRUE(modern);
  for (const auto& context : cases) {
    SCOPED_TRACE(std::string(context.version) + "/" +
                 std::string(context.target));
    const auto source = recursionSource(context.version, context.target);
    auto draft = recursionOwned(
        source);  // ModuleOnly is a real transport, not a recursion acceptance claim.
    ASSERT_TRUE(draft);
    const auto graph = detail::analyze_call_cycles(*draft);
    ASSERT_TRUE(graph.diagnostics.empty());
    const auto index = recursionDefinition(*draft);
    ASSERT_EQ(graph.recursive_calls[index].size(), 1);
    const auto range = draft->functions[index].instruction_ranges[0];
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      const auto owned = validateModule(*draft, policy);
      const auto supplied = validateModule(*ast, *draft, policy);
      const auto retargeted = validateModule(*ast, *modern, policy);
      const checker::CheckResult* results[] = {&owned, &supplied, &retargeted};
      for (size_t i = 0; i < 3; ++i) {
        const auto& result = *results[i];
        if (context.unavailable)
          expectRecursionUnavailable(
              result,
              i == 2 ? modern->functions[0].instruction_ranges[0] : range);
        else if (context.target == "sm_999") {
          ASSERT_FALSE(result);
          EXPECT_TRUE(
              std::ranges::any_of(result.error(), [](const auto& error) {
                return error.kind ==
                       checker::CheckDiagnosticKind::UnknownTarget;
              }));
        } else if (policy == ModuleValidationPolicy::AvailableContext ||
                   (!context.version.empty() && !context.target.empty()))
          EXPECT_TRUE(result);
        else {
          ASSERT_FALSE(result);
          EXPECT_TRUE(
              std::ranges::any_of(result.error(), [](const auto& error) {
                return error.kind ==
                       checker::CheckDiagnosticKind::MissingValidationContext;
              }));
        }
      }
    }
    if (!context.target.empty() && context.target != "sm_999" &&
        !context.version.empty()) {
      auto acyclic = recursionOwned(
          recursionSource(context.version, context.target, false));
      ASSERT_TRUE(acyclic);
      EXPECT_TRUE(validateModule(*acyclic));
    }
  }
}

/** Compatible prototypes share identities but only actual definitions contribute cycle edges. */
TEST(LegacyRecursion, ChecksUnusedMutualPredicatedAndRegionCycles) {
  const std::string body = ".func f(); .func g(); .func f(){call g;ret;} ";
  for (const auto version : {"1.3", "2.0"}) {
    const auto source =
        ".version " + std::string(version) + "\n.target " +
        (std::string_view(version) == "1.3" ? "sm_13" : "sm_20") + "\n" + body +
        ".func g(){call f;ret;} .entry k(){ret;}\n";
    auto module = recursionOwned(source);
    ASSERT_TRUE(module);
    const auto graph = detail::analyze_call_cycles(*module);
    ASSERT_TRUE(graph.diagnostics.empty());
    const auto f = recursionDefinition(*module, "f"),
               g = recursionDefinition(*module, "g");
    EXPECT_EQ(module->functions[0].symbol_id, module->functions[f].symbol_id);
    EXPECT_NE(module->functions[0].declaration_scope,
              module->functions[f].declaration_scope);
    ASSERT_EQ(graph.recursive_calls[f].size(), 1);
    ASSERT_EQ(graph.recursive_calls[g].size(), 1);
    if (std::string_view(version) == "1.3") {
      expectRecursionUnavailable(validateModule(*module),
                                 graph.recursive_calls[f][0].range);
      expectRecursionUnavailable(validateModule(*module),
                                 graph.recursive_calls[g][0].range);
    } else
      EXPECT_TRUE(validateModule(*module));
  }
  auto predicated = recursionOwned(
      ".version 1.3\n.target sm_13\n.func f(){.reg .pred %p; @%p call f; "
      "ret;}\n");
  ASSERT_TRUE(predicated);
  expectRecursionUnavailable(validateModule(*predicated),
                             predicated->functions[0].instruction_ranges[0]);
  const std::string regions = ".version 2.0\n.target sm_20\n" + body +
                              ".target sm_13\n.func g(){call f;ret;}\n";
  auto selected = recursionOwned(regions);
  ASSERT_TRUE(selected);
  const auto g = recursionDefinition(*selected, "g");
  expectRecursionUnavailable(validateModule(*selected),
                             selected->functions[g].instruction_ranges[0]);
  PtxSyntaxParser parser(regions);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  expectRecursionUnavailable(validateModule(*ast, *selected),
                             selected->functions[g].instruction_ranges[0]);
  retargetRecursion(*selected, {2, 0}, "sm_20");
  EXPECT_TRUE(validateModule(*selected));
  expectRecursionUnavailable(validateModule(*ast, *selected),
                             selected->functions[g].instruction_ranges[0]);
}

/** External, prototype-only and signature-only calls remain open instead of gaining fabricated edges. */
TEST(LegacyRecursion, PreservesOpenWorldBoundariesWithoutClosedWorldClaims) {
  for (const auto declaration : {".func g();", ".extern .func g();"}) {
    auto module =
        recursionOwned(".version 1.3\n.target sm_13\n" +
                       std::string(declaration) + ".func f(){call g;ret;}\n");
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
    const auto graph = detail::analyze_call_cycles(*module);
    EXPECT_TRUE(graph.diagnostics.empty());
    EXPECT_TRUE(graph.has_open_boundaries);
    EXPECT_TRUE(
        std::ranges::all_of(graph.recursive_calls,
                            [](const auto& calls) { return calls.empty(); }));
  }
  auto dynamic = recursionOwned(
      ".version 9.3\n.target sm_80\n.func f(){.reg .u64 %fp; p:.callprototype "
      "_ (); call %fp,p;ret;}\n");
  ASSERT_TRUE(dynamic);
  EXPECT_TRUE(validateModule(*dynamic));
  const auto graph = detail::analyze_call_cycles(*dynamic);
  EXPECT_TRUE(graph.diagnostics.empty());
  EXPECT_TRUE(graph.has_open_boundaries);
  EXPECT_TRUE(graph.recursive_calls[0].empty());
}

/** The original acyclic AST cannot mask an actual coherent current direct target edit. */
TEST(LegacyRecursion, RecomputesCurrentCyclesAcrossSourceAndValueOperations) {
  const std::string source =
      ".version 1.3\n.target sm_13\n.func g(){ret;} .func f(){call g;ret;}\n";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto original = resolveAndValidateModule(*ast);
  ASSERT_TRUE(original);
  auto edited = *original;
  const auto index = recursionDefinition(edited);
  auto& call = dynamic_cast<CallDirect&>(*edited.functions[index].body[0]);
  ASSERT_TRUE(call.target_direct_call_target);
  call.target_direct_call_target->value.symbol_id =
      edited.functions[index].symbol_id;
  call.target_direct_call_target->value.spelling = "f";
  const auto range = edited.functions[index].instruction_ranges[0];
  expectRecursionUnavailable(validateModule(edited), range);
  expectRecursionUnavailable(validateModule(*ast, edited), range);
  const auto copy = edited;
  ResolvedModule assigned = *original;
  assigned = copy;
  const auto function_copy = copy.functions[index];
  assigned.functions[index] = function_copy;
  for (auto& instruction : assigned.functions[index].body)
    instruction = instruction->clone();
  original = std::unexpected(ModuleResolveDiagnostics{});
  expectRecursionUnavailable(validateModule(assigned), range);
  auto changed = source;
  changed.replace(changed.find("call g"), 6, "call f");
  PtxSyntaxParser changed_parser(changed);
  const auto changed_ast = changed_parser.parseModule();
  ASSERT_TRUE(changed_ast && changed_ast.diagnostics.empty());
  const auto mismatch = validateModule(*changed_ast, edited);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  auto invalid = copy;
  auto& invalid_call =
      dynamic_cast<CallDirect&>(*invalid.functions[index].body[0]);
  invalid_call.target_direct_call_target->value.symbol_id =
      binding::SymbolId{UINT32_MAX};
  for (const auto& result :
       {validateModule(invalid), validateModule(*ast, invalid)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
      return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
    EXPECT_TRUE(std::ranges::none_of(result.error(), [](const auto& error) {
      return error.message.find("recursion requires") != std::string::npos;
    }));
  }
}

/** Modern finite declarations supply private graph units where their own windows preclude legacy source isolation. */
TEST(LegacyRecursion, QualifiesConsumedFiniteCandidatesAndUnusedMetadata) {
  for (const bool consumed : {false, true}) {
    const std::string source =
        ".version 9.3\n.target sm_80\n.func f(); .func f(){.reg .u64 %fp; "
        "targets:.calltargets f; " +
        std::string(consumed ? "call %fp,targets; " : "") + "ret;}\n";
    auto module = recursionOwned(source);
    ASSERT_TRUE(module);
    const auto baseline = validateModule(*module);
    ASSERT_TRUE(baseline) << recursionMessages(baseline);
    const auto graph = detail::analyze_call_cycles(*module);
    ASSERT_TRUE(graph.diagnostics.empty());
    const auto index = recursionDefinition(*module);
    EXPECT_EQ(graph.recursive_calls[index].size(), consumed ? 1 : 0);
    if (consumed) {
      EXPECT_TRUE(graph.recursive_calls[index][0].possible);
      retargetRecursion(*module, {2, 0}, "sm_13");
      // Owned context mutation co-fails indirect/metadata floors; only the private
      // graph unit above isolates the candidate relation, not a legacy source run.
      expectRecursionUnavailable(validateModule(*module),
                                 graph.recursive_calls[index][0].range, true);
    }
  }
  auto mixed = recursionOwned(
      ".version 9.3\n.target sm_80\n.func f(); .func g(); .func f(){call "
      "g;ret;} .func g(){.reg .u64 %fp; targets:.calltargets f;call "
      "%fp,targets;ret;}\n");
  ASSERT_TRUE(mixed);
  const auto mixed_valid = validateModule(*mixed);
  ASSERT_TRUE(mixed_valid) << recursionMessages(mixed_valid);
  const auto mixed_graph = detail::analyze_call_cycles(*mixed);
  ASSERT_TRUE(mixed_graph.diagnostics.empty());
  for (const auto name : {"f", "g"}) {
    const auto index = recursionDefinition(*mixed, name);
    ASSERT_EQ(mixed_graph.recursive_calls[index].size(), 1);
    EXPECT_TRUE(mixed_graph.recursive_calls[index][0].possible);
  }
  auto acyclic = recursionOwned(
      ".version 9.3\n.target sm_80\n.func g(){ret;} .func f(){.reg .u64 %fp; "
      "targets:.calltargets g;call %fp,targets;ret;}\n");
  ASSERT_TRUE(acyclic);
  EXPECT_TRUE(validateModule(*acyclic));
  const auto graph = detail::analyze_call_cycles(*acyclic);
  ASSERT_TRUE(graph.diagnostics.empty());
  EXPECT_TRUE(std::ranges::all_of(
      graph.recursive_calls, [](const auto& calls) { return calls.empty(); }));
}

/** Explicit consumed slots and aliases use canonical IDs; sparse zeros never become function edges. */
TEST(LegacyRecursion, JoinsConsumedTablesAndAliasesWithoutInventingEdges) {
  for (const bool consumed : {false, true}) {
    const std::string source =
        ".version 9.3\n.target sm_80\n.func f(); .global .u64 T[3]={f}; .func "
        "f(){.reg .u64 %fp; " +
        std::string(consumed ? "call %fp,T; " : "") + "ret;}\n";
    auto module = recursionOwned(source);
    ASSERT_TRUE(module);
    const auto baseline = validateModule(*module);
    ASSERT_TRUE(baseline) << recursionMessages(baseline);
    const auto graph = detail::analyze_call_cycles(*module);
    ASSERT_TRUE(graph.diagnostics.empty());
    const auto index = recursionDefinition(*module);
    ASSERT_EQ(graph.recursive_calls[index].size(), consumed ? 1 : 0);
    if (consumed) {
      ASSERT_EQ(module->call_tables.size(), 1);
      EXPECT_EQ(module->call_tables[0].extent, 3);
      EXPECT_EQ(module->call_tables[0].slots.size(), 1);
      EXPECT_TRUE(graph.recursive_calls[index][0].possible);
      retargetRecursion(*module, {2, 0}, "sm_13");
      expectRecursionUnavailable(validateModule(*module),
                                 graph.recursive_calls[index][0].range, true);
    } else
      EXPECT_TRUE(module->call_tables.empty());
  }
  auto alias = recursionOwned(
      ".version 9.3\n.target sm_80\n.func alias_fn(); .func g(); .func "
      "f(){call g;ret;} .alias alias_fn,f; .func g(){call alias_fn;ret;}\n");
  ASSERT_TRUE(alias);
  ASSERT_TRUE(validateModule(*alias));
  const auto graph = detail::analyze_call_cycles(*alias);
  ASSERT_TRUE(graph.diagnostics.empty());
  const auto f = recursionDefinition(*alias, "f"),
             g = recursionDefinition(*alias, "g");
  ASSERT_EQ(graph.recursive_calls[f].size(), 1);
  ASSERT_EQ(graph.recursive_calls[g].size(), 1);
  EXPECT_FALSE(graph.recursive_calls[g][0].possible);
  retargetRecursion(*alias, {2, 0}, "sm_13");
  // Alias source availability independently prevents a legal legacy source pair.
  expectRecursionUnavailable(validateModule(*alias),
                             graph.recursive_calls[g][0].range);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
