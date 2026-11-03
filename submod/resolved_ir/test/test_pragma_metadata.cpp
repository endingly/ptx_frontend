#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Keep recognized SM13 constant and avoid the later explicit address-size directive. */
std::string pragmaSource(std::string_view version, unsigned placement) {
  std::string source = ".version " + std::string(version) + "\n.target sm_13\n";
  if (placement == 0)
    source += ".pragma \"opaque\";\n";
  source += ".entry k() ";
  if (placement == 1)
    source += ".pragma \"opaque\"; ";
  source += "{ ";
  if (placement == 2)
    source += "{ .pragma \"opaque\"; } ";
  if (placement == 3)
    source += ".pragma \"opaque\"; ";
  return source + "ret; }\n";
}

/** Legal2.0 all-placement controls distinguish an actual availability failure. */
TEST(PragmaMetadata, FreshMatchedIntroductionControls) {
  for (const unsigned placement : {0u, 1u, 2u, 3u}) {
    SCOPED_TRACE(placement);
    PtxSyntaxParser parser(pragmaSource("2.0", placement));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Opaque content does not exempt any placement from the generic introduction window. */
TEST(PragmaMetadata, RejectsSourceBeforeGenericIntroduction) {
  for (const unsigned placement : {0u, 1u, 2u, 3u}) {
    SCOPED_TRACE(placement);
    PtxSyntaxParser parser(pragmaSource("1.5", placement));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    auto draft = resolveModuleOnly(*ast);
    ASSERT_TRUE(draft);
    ASSERT_EQ(draft->pragmas.size(), 1);
    const auto result = validateModule(*draft);
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
      return error.kind ==
                 checker::CheckDiagnosticKind::UnsupportedAvailability &&
             error.range == draft->pragmas[0].range &&
             error.message == ".pragma requires PTX ISA >= 2.0.";
    }));
  }
}
/** Return a transport whose source, parser and AST allocations are no longer alive. */
std::optional<ResolvedModule> pragmaOwned(std::string source) {
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

/** All placements include actual prototype/definition scopes and nonempty metadata. */
std::string allPragmaSource() {
  return R"ptx(.version 2.0
.target sm_13
.pragma "module", "";
.func f();
.func f() { .pragma "definition"; ret; }
.entry k() .pragma "header"; {
.pragma "body";
{ .pragma "nested", "a\\b"; }
ret;
}
.pragma "after";
)ptx";
}

/** Compare owning public values without adding a public equality or clone API. */
void expectPragmasEqual(const std::vector<ResolvedPragma>& a,
                        const std::vector<ResolvedPragma>& b) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].placement, b[i].placement);
    EXPECT_EQ(a[i].scope_id, b[i].scope_id);
    EXPECT_EQ(a[i].range, b[i].range);
    ASSERT_EQ(a[i].strings.size(), b[i].strings.size());
    for (size_t j = 0; j < a[i].strings.size(); ++j) {
      EXPECT_EQ(a[i].strings[j].text, b[i].strings[j].text);
      EXPECT_EQ(a[i].strings[j].range, b[i].strings[j].range);
    }
  }
}

/** Require a precise intrinsic or feature diagnostic rather than arbitrary source mismatch. */
void expectPragmaError(const checker::CheckResult& result,
                       checker::CheckDiagnosticKind kind,
                       std::string_view message, SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.kind == kind && error.range == range &&
           error.message == message;
  }));
}

/** All ranges, raw leaves and occurrence associations survive owning value operations. */
TEST(PragmaMetadata, OwnsAllPlacementsAfterSourceDeathAndCopies) {
  auto module = pragmaOwned(allPragmaSource());
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  ASSERT_EQ(module->pragmas.size(), 6);
  const auto& p = module->pragmas;
  EXPECT_EQ(p[0].placement, ResolvedPragmaPlacement::Module);
  EXPECT_EQ(p[0].scope_id, module->symbols.moduleScope());
  EXPECT_EQ(p[0].strings[1].text, "\"\"");
  EXPECT_EQ(p[1].placement, ResolvedPragmaPlacement::Statement);
  EXPECT_EQ(p[1].scope_id, module->functions[1].declaration_scope);
  EXPECT_EQ(p[2].placement, ResolvedPragmaPlacement::EntryHeader);
  EXPECT_EQ(p[2].scope_id, module->functions[2].declaration_scope);
  EXPECT_EQ(p[3].scope_id, p[2].scope_id);
  EXPECT_NE(p[4].scope_id, p[3].scope_id);
  EXPECT_EQ(module->symbols.scope(p[4].scope_id).parent, p[3].scope_id);
  EXPECT_EQ(p[4].strings[1].text, R"("a\\b")");
  EXPECT_EQ(p[5].placement, ResolvedPragmaPlacement::Module);
  ASSERT_EQ(module->functions[0].symbol_id, module->functions[1].symbol_id);
  EXPECT_NE(module->functions[0].declaration_scope,
            module->functions[1].declaration_scope);
  const auto copied = *module;
  ResolvedModule assigned = copied;
  assigned.pragmas[0].strings[0].text = "\"prior data\"";
  assigned = copied;
  expectPragmasEqual(copied.pragmas, module->pragmas);
  expectPragmasEqual(assigned.pragmas, module->pragmas);
  ResolvedModule moved = std::move(assigned);
  expectPragmasEqual(moved.pragmas, copied.pragmas);
  EXPECT_TRUE(validateModule(moved));
  const auto function_copy = moved.functions[1];
  moved.functions[1] = function_copy;
  moved.functions[1].body[0] = moved.functions[1].body[0]->clone();
  expectPragmasEqual(moved.pragmas, copied.pragmas);
  EXPECT_TRUE(validateModule(moved));
  module->pragmas[0].strings[0].text = "\"changed\"";
  EXPECT_TRUE(validateModule(*module));
  EXPECT_EQ(copied.pragmas[0].strings[0].text, "\"module\"");
  std::ranges::reverse(module->pragmas);
  EXPECT_TRUE(validateModule(*module));
}

/** Opaque string contents do not create backend feature gates or normalization. */
TEST(PragmaMetadata, KeepsEveryAdoptedOpaqueStringPartition) {
  for (const auto words :
       {"\"unrecognized_backend_text\"", "\"\"", "\"module\", \"opaque\"",
        "\"opaque\", \"opaque\"", R"("a\\b")", "\"enable_smem_spilling\"",
        "\"mma_throughput\""}) {
    auto source = pragmaSource("2.0", 0);
    source.replace(source.find("\"opaque\""), 8, words);
    auto module = pragmaOwned(source);
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    std::string joined;
    for (const auto& leaf : module->pragmas[0].strings) {
      if (!joined.empty())
        joined += ", ";
      joined += leaf.text;
    }
    EXPECT_EQ(joined, words);
  }
}

/** Each known version failure remains independent of recognized target completeness. */
TEST(PragmaMetadata, AppliesTheGenericFloorToAllActualPlacements) {
  for (const unsigned placement : {0u, 1u, 2u, 3u}) {
    auto module = pragmaOwned(pragmaSource("2.0", placement));
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    for (auto& region : module->header.regions)
      region.version = checker::PtxVersion{1, 5};
    for (auto& function : module->functions)
      function.source_version = checker::PtxVersion{1, 5};
    const auto copy = *module;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      expectPragmaError(validateModule(*module, policy),
                        checker::CheckDiagnosticKind::UnsupportedAvailability,
                        ".pragma requires PTX ISA >= 2.0.",
                        module->pragmas[0].range);
      expectPragmaError(validateModule(copy, policy),
                        checker::CheckDiagnosticKind::UnsupportedAvailability,
                        ".pragma requires PTX ISA >= 2.0.",
                        copy.pragmas[0].range);
    }
    auto source = pragmaSource("2.0", placement);
    source.erase(source.find(".version"), source.find('\n') + 1);
    auto unknown = pragmaOwned(source);
    ASSERT_TRUE(unknown);
    EXPECT_TRUE(
        validateModule(*unknown, ModuleValidationPolicy::AvailableContext));
    const auto strict = validateModule(*unknown);
    ASSERT_FALSE(strict);
    EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& error) {
      return error.kind ==
             checker::CheckDiagnosticKind::MissingValidationContext;
    }));
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(validateModule(*ast, *unknown,
                               ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(validateModule(*ast, *unknown));
    auto no_target = pragmaSource("2.0", placement);
    const auto begin = no_target.find(".target");
    no_target.erase(begin, no_target.find('\n', begin) - begin + 1);
    auto partial = pragmaOwned(no_target);
    ASSERT_TRUE(partial);
    EXPECT_TRUE(
        validateModule(*partial, ModuleValidationPolicy::AvailableContext));
  }
}

/** Neutral replacement may remove pragmas and lower version without hiding malformed current data. */
TEST(PragmaMetadata,
     SeparatesCurrentIntrinsicDataFromNeutralReplacementVersion) {
  auto module = pragmaOwned(pragmaSource("2.0", 0));
  ASSERT_TRUE(module);
  PtxSyntaxParser plain_parser(
      ".version 1.5\n.target sm_13\n.entry k() { ret; }\n");
  const auto plain = plain_parser.parseModule();
  ASSERT_TRUE(plain && plain.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*plain));
  EXPECT_TRUE(validateModule(*plain, *module));
  PtxSyntaxParser old_parser(pragmaSource("1.5", 0));
  const auto old = old_parser.parseModule();
  ASSERT_TRUE(old && old.diagnostics.empty());
  expectPragmaError(validateModule(*old, *module),
                    checker::CheckDiagnosticKind::UnsupportedAvailability,
                    ".pragma requires PTX ISA >= 2.0.",
                    module->pragmas[0].range);
  auto changed_source = pragmaSource("2.0", 1);
  changed_source.replace(changed_source.find("\"opaque\""), 8,
                         "\"other\", \"list\"");
  PtxSyntaxParser changed_parser(changed_source);
  const auto changed = changed_parser.parseModule();
  ASSERT_TRUE(changed && changed.diagnostics.empty());
  EXPECT_TRUE(validateModule(*changed, *module));
  auto removed = *module;
  removed.pragmas.clear();
  EXPECT_TRUE(validateModule(removed));
  EXPECT_TRUE(validateModule(*changed, removed));
  expectPragmaError(validateModule(*old, removed),
                    checker::CheckDiagnosticKind::UnsupportedAvailability,
                    ".pragma requires PTX ISA >= 2.0.",
                    module->pragmas[0].range);
  module->pragmas[0].strings.clear();
  expectPragmaError(validateModule(*plain, *module),
                    checker::CheckDiagnosticKind::RuleViolation,
                    "Pragma metadata requires a nonempty string list.",
                    module->pragmas[0].range);
}

/** Public current-record mutations are rechecked independently of a valid supplied AST. */
TEST(PragmaMetadata, RejectsCurrentPlacementScopeAndLeafMutations) {
  const auto source = allPragmaSource();
  auto module = pragmaOwned(source);
  ASSERT_TRUE(module);
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  for (unsigned mode = 0; mode < 9; ++mode) {
    auto edited = *module;
    auto& record = edited.pragmas[3];
    std::string_view message;
    SourceRange range = record.range;
    if (mode == 0) {
      record.scope_id = edited.symbols.moduleScope();
    }
    if (mode == 1) {
      record.scope_id = binding::ScopeId{std::numeric_limits<uint32_t>::max()};
    }
    if (mode == 2) {
      record.placement = static_cast<ResolvedPragmaPlacement>(255);
    }
    if (mode == 3) {
      record.strings.clear();
      message = "Pragma metadata requires a nonempty string list.";
    }
    if (mode == 4) {
      record.strings[0].text = "unquoted";
      range = record.strings[0].range;
      message = "Pragma metadata requires quoted raw string leaves.";
    }
    if (mode == 5) {
      record.strings[0].range.end.line += 100;
      range = record.strings[0].range;
      message = "Pragma metadata has invalid string-list boundaries.";
    }
    if (mode == 6) {
      record.range = {};
      range = {};
      message = "Pragma metadata has invalid source boundaries.";
    }
    if (mode == 7) {
      auto& definition = edited.pragmas[1];
      definition.placement = ResolvedPragmaPlacement::EntryHeader;
      range = definition.range;
    }
    if (mode == 8) {
      auto& definition = edited.pragmas[1];
      definition.scope_id = edited.functions[0].declaration_scope;
      definition.range = edited.functions[0].range;
      definition.strings[0].range = definition.range;
      range = definition.range;
    }
    if (message.empty())
      message =
          "Pragma metadata has no valid placement and declaration occurrence.";
    expectPragmaError(validateModule(edited),
                      checker::CheckDiagnosticKind::RuleViolation, message,
                      range);
    expectPragmaError(validateModule(*ast, edited),
                      checker::CheckDiagnosticKind::RuleViolation, message,
                      range);
    const auto copy = edited;
    expectPragmaError(validateModule(copy),
                      checker::CheckDiagnosticKind::RuleViolation, message,
                      range);
  }
  auto reordered = *module;
  std::swap(reordered.pragmas[0].strings[0], reordered.pragmas[0].strings[1]);
  expectPragmaError(validateModule(reordered),
                    checker::CheckDiagnosticKind::RuleViolation,
                    "Pragma metadata has invalid string-list boundaries.",
                    reordered.pragmas[0].strings[1].range);
  EXPECT_TRUE(validateModule(*module));
}

/** Current and supplied roots independently contain metadata; unknown roots defer. */
TEST(PragmaMetadata, ChecksBothIndependentModuleRootBoundaries) {
  const auto source = pragmaSource("2.0", 0);
  auto module = pragmaOwned(source);
  ASSERT_TRUE(module);
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  const auto range = module->pragmas[0].range;
  auto narrow = module->range;
  narrow.end = range.start;
  module->range = narrow;
  expectPragmaError(
      validateModule(*module), checker::CheckDiagnosticKind::RuleViolation,
      "Pragma metadata lies outside its owning module boundaries.", range);
  expectPragmaError(
      validateModule(*ast, *module),
      checker::CheckDiagnosticKind::RuleViolation,
      "Pragma metadata lies outside its owning module boundaries.", range);
  module->range = ast->range;
  ast->range = narrow;
  expectPragmaError(
      validateModule(*ast, *module),
      checker::CheckDiagnosticKind::RuleViolation,
      "Pragma metadata lies outside its owning module boundaries.", range);
  ast->range = {};
  EXPECT_TRUE(validateModule(*ast, *module));
  module->range = {};
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(validateModule(*ast, *module));
}

/** Supplied structured AST payloads are checked even when retained current data is valid. */
TEST(PragmaMetadata, RejectsIndependentSuppliedPayloadAndDeviceHeader) {
  auto module = pragmaOwned(pragmaSource("2.0", 0));
  ASSERT_TRUE(module);
  PtxSyntaxParser parser(pragmaSource("2.0", 0));
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto& pragma = std::get<syntax_ast::AstPragma>(ast->items[2]);
  pragma.strings.clear();
  expectPragmaError(validateModule(*ast, *module),
                    checker::CheckDiagnosticKind::RuleViolation,
                    "Pragma metadata requires a nonempty string list.",
                    pragma.range);
  auto device = pragmaOwned(
      ".version 2.0\n.target sm_13\n.func f() { .pragma \"body\"; ret; }\n");
  ASSERT_TRUE(device);
  ASSERT_TRUE(validateModule(*device));
  PtxSyntaxParser device_parser(
      ".version 2.0\n.target sm_13\n.func f() { .pragma \"body\"; ret; }\n");
  auto device_ast = device_parser.parseModule();
  ASSERT_TRUE(device_ast && device_ast.diagnostics.empty());
  auto& function = std::get<syntax_ast::AstFunction>(device_ast->items[2]);
  const auto* body_pragma =
      std::get_if<syntax_ast::AstPragma>(&function.body[0]);
  ASSERT_TRUE(body_pragma);
  function.pragmas.push_back(*body_pragma);
  expectPragmaError(
      validateModule(*device_ast, *device),
      checker::CheckDiagnosticKind::RuleViolation,
      "Pragma metadata has no valid placement and declaration occurrence.",
      body_pragma->range);
  PtxSyntaxParser invalid_source(
      ".version 2.0\n.target sm_13\n.func f() .pragma \"header\"; { ret; }\n");
  const auto invalid = invalid_source.parseModule();
  EXPECT_FALSE(invalid && invalid.diagnostics.empty());
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
