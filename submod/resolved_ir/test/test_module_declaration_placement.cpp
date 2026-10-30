#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a test module while retaining useful syntax diagnostics. */
std::optional<syntax_ast::AstModule> placementModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << (ast.diagnostics.empty()
                          ? "Module did not parse."
                          : ast.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Recognize only the ABI placement rule among unrelated integrity errors. */
bool placementMessage(std::string_view message) {
  return message.find("must be declared within a function") !=
         std::string_view::npos;
}

/** Count source occurrence diagnostics without relying on canonical identities. */
size_t placementCount(const syntax_ast::AstModule& ast) {
  const auto symbols = binding::bindSymbols(ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(ast, symbols.table);
  return std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
    return diagnostic.kind == declaration_semantics::DeclarationDiagnosticKind::
                                  InvalidDeclarationPlacement;
  });
}

/** Find a source-independent placement rule at the complete declaration range. */
bool ownedPlacement(const checker::CheckResult& result, SourceRange range) {
  return !result &&
         std::ranges::any_of(result.error(), [range](const auto& diagnostic) {
           return diagnostic.kind ==
                      checker::CheckDiagnosticKind::RuleViolation &&
                  diagnostic.range == range &&
                  placementMessage(diagnostic.message);
         });
}

/** Find the missing placement context at the affected declaration itself. */
bool missingPlacementContext(const checker::CheckResult& result,
                             SourceRange range) {
  return !result &&
         std::ranges::any_of(result.error(), [range](const auto& diagnostic) {
           return diagnostic.kind ==
                      checker::CheckDiagnosticKind::MissingValidationContext &&
                  diagnostic.range == range;
         });
}

/** Modern stack ABI declarations are invalid even when no instruction uses them. */
TEST(ModuleDeclarationPlacement, RejectsUnusedModernModuleRegistersAndLocals) {
  for (const std::string_view space : {".reg", ".local"}) {
    PtxSyntaxParser parser(".version 9.3\n.target sm_80\n" +
                           std::string(space) +
                           " .u32 unused; .entry kernel() { ret; }");
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    const auto diagnostics =
        declaration_semantics::checkDeclarations(*ast, bound.table);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
      return diagnostic.message.find("must be declared within a function") !=
             std::string::npos;
    }));
    EXPECT_FALSE(resolveModuleOnly(*ast));
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** The stack ABI boundary preserves prior PTX and recognized sm_1x targets. */
TEST(ModuleDeclarationPlacement, AppliesVersionAndSourceTargetBoundary) {
  for (const std::string_view space : {".reg", ".local"}) {
    for (const auto& [version, target, rejected] :
         {std::tuple{"1.4", "sm_10", false},
          {"1.5", "sm_13", false},
          {"2.3", "sm_20", false},
          {"3.0", "sm_13", false},
          {"3.0", "sm_20", true},
          {"9.3", "sm_80", true}}) {
      const std::string source =
          ".version " + std::string(version) + "\n.target " + target + "\n" +
          std::string(space) + " .u32 unused; .entry kernel() { ret; }";
      SCOPED_TRACE(source);
      const auto ast = placementModule(source);
      ASSERT_TRUE(ast);
      EXPECT_EQ(placementCount(*ast), rejected ? 1u : 0u);
      const auto resolved = resolveModuleOnly(*ast);
      EXPECT_EQ(bool(resolved), !rejected);
      const auto strict = resolveAndValidateModule(*ast);
      EXPECT_EQ(bool(strict), !rejected);
      if (resolved)
        EXPECT_TRUE(validateModule(*resolved));
    }
  }
}

/** Legal nested storage, register groups, vectors and register formals survive. */
TEST(ModuleDeclarationPlacement, PreservesFunctionAndFormalDeclarations) {
  const auto ast = placementModule(R"ptx(
.version 9.3
.target sm_80
.func (.reg .u32 result) device(.reg .u32 argument) {
  .reg .u32 scalar;
  .local .u32 local_value;
  { .reg .u32 group<2>; .reg .v2 .u32 vector; .local .u32 values[2]; }
  mov.u32 result, argument;
  ret;
}
.entry kernel(.param .u32 argument) { .reg .u32 r; .local .u32 value; ret; }
)ptx");
  ASSERT_TRUE(ast);
  EXPECT_EQ(placementCount(*ast), 0u);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
}

/** Every actual source occurrence is checked even when binding keeps another state. */
TEST(ModuleDeclarationPlacement, ChecksUsedAndConflictingOccurrences) {
  for (const std::string_view space : {".reg", ".local"}) {
    for (const bool bad_first : {false, true}) {
      const std::string bad = std::string(space) + " .u32 value; ";
      const std::string other = ".global .u32 value; ";
      const auto ast = placementModule(".version 9.3\n.target sm_80\n" +
                                       (bad_first ? bad + other : other + bad) +
                                       ".entry kernel() { ret; }");
      ASSERT_TRUE(ast);
      EXPECT_EQ(placementCount(*ast), 1u);
      EXPECT_FALSE(resolveModuleOnly(*ast));
    }
    const std::string body = space == ".reg"
                                 ? "mov.u32 value, 1;"
                                 : ".reg .u32 r; ld.local.u32 r, [value];";
    const auto ast =
        placementModule(".version 9.3\n.target sm_80\n" + std::string(space) +
                        " .u32 value; .entry kernel() { " + body + " ret; }");
    ASSERT_TRUE(ast);
    EXPECT_EQ(placementCount(*ast), 1u);
    EXPECT_FALSE(resolveModuleOnly(*ast));
  }
}

/** Placement follows target order rather than the first or final target profile. */
TEST(ModuleDeclarationPlacement, TracksEveryActiveTargetRegion) {
  for (const std::string_view space : {".reg", ".local"}) {
    for (const bool modern_first : {false, true}) {
      const std::string first = modern_first ? "sm_20" : "sm_13";
      const std::string second = modern_first ? "sm_13" : "sm_20";
      const std::string declarations =
          ".target " + first + "\n" + std::string(space) +
          " .u32 first;\n.target " + second + "\n" + std::string(space) +
          " .u32 second; .entry kernel() { ret; }";
      const auto modern = placementModule(".version 3.0\n" + declarations);
      ASSERT_TRUE(modern);
      EXPECT_EQ(placementCount(*modern), 1u);
      const auto legacy = placementModule(".version 2.3\n" + declarations);
      ASSERT_TRUE(legacy);
      auto module = resolveModuleOnly(*legacy);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(validateModule(*module));
      for (auto& region : module->header.regions)
        region.version = checker::PtxVersion{3, 0};
      for (auto& function : module->functions)
        function.source_version = checker::PtxVersion{3, 0};
      const auto& declaration = std::get<syntax_ast::AstVariableDeclaration>(
          modern->items[modern_first ? 2 : 4]);
      const auto checked = validateModule(*module);
      EXPECT_TRUE(
          ownedPlacement(checked, declaration.declarators.front().range));
      ASSERT_FALSE(checked);
      EXPECT_EQ(
          std::ranges::count_if(checked.error(),
                                [](const auto& diagnostic) {
                                  return placementMessage(diagnostic.message);
                                }),
          1);
    }
  }
}

/** Missing or unknown source facts defer placement only under available-context policy. */
TEST(ModuleDeclarationPlacement, DefersUnknownContextAndRequiresStrictContext) {
  for (const std::string_view space : {".reg", ".local"}) {
    for (const std::string_view prefix :
         {"", ".version 3.0\n", ".target sm_80\n",
          ".version 3.0\n.target sm_999\n",
          ".version 3.0\n.target sm_80\n.target sm_999\n"}) {
      const auto ast =
          placementModule(std::string(prefix) + std::string(space) +
                          " .u32 unused; .entry kernel() { ret; }");
      ASSERT_TRUE(ast);
      EXPECT_EQ(placementCount(*ast), 0u);
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      const auto& declaration = std::get<syntax_ast::AstVariableDeclaration>(
          ast->items[ast->items.size() - 2]);
      for (const auto& checked :
           {validateModule(*module, ModuleValidationPolicy::AvailableContext),
            validateModule(*ast, *module,
                           ModuleValidationPolicy::AvailableContext)}) {
        if (!checked)
          EXPECT_TRUE(
              std::ranges::none_of(checked.error(), [](const auto& diagnostic) {
                return placementMessage(diagnostic.message);
              }));
      }
      EXPECT_TRUE(missingPlacementContext(
          validateModule(*module), declaration.declarators.front().range));
      EXPECT_TRUE(missingPlacementContext(validateModule(*ast, *module),
                                          declaration.range));
    }
  }
}

/** An empty later target clears the prior recognized source placement context. */
TEST(ModuleDeclarationPlacement, ClearsContextAtEmptyTarget) {
  for (const std::string_view space : {".reg", ".local"}) {
    auto ast = placementModule(".version 3.0\n.target sm_20\n.target sm_20\n" +
                               std::string(space) +
                               " .u32 unused; .entry kernel() { ret; }");
    ASSERT_TRUE(ast);
    std::get<syntax_ast::AstTargetDirective>(ast->items[2]).targets.clear();
    EXPECT_EQ(placementCount(*ast), 0u);
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    const auto& declaration =
        std::get<syntax_ast::AstVariableDeclaration>(ast->items[3]);
    const auto available =
        validateModule(*module, ModuleValidationPolicy::AvailableContext);
    ASSERT_FALSE(available);
    EXPECT_TRUE(
        std::ranges::none_of(available.error(), [](const auto& diagnostic) {
          return placementMessage(diagnostic.message);
        }));
    EXPECT_TRUE(missingPlacementContext(validateModule(*module),
                                        declaration.declarators.front().range));
    EXPECT_TRUE(missingPlacementContext(validateModule(*ast, *module),
                                        declaration.range));
  }
}

/** Replacement source contexts are reapplied without requiring original AST lifetime. */
TEST(ModuleDeclarationPlacement, RechecksReplacementSourceAndOwnedLifetime) {
  for (const std::string_view space : {".reg", ".local"}) {
    std::optional<ResolvedModule> module;
    SourceRange expected;
    {
      const std::string suffix =
          std::string(space) + " .u32 unused; .entry kernel() { ret; }";
      const auto legacy =
          placementModule(".version 2.3\n.target sm_20\n" + suffix);
      const auto modern =
          placementModule(".version 3.0\n.target sm_20\n" + suffix);
      ASSERT_TRUE(legacy);
      ASSERT_TRUE(modern);
      auto resolved = resolveAndValidateModule(*legacy);
      ASSERT_TRUE(resolved) << resolved.error().front().message;
      module.emplace(std::move(*resolved));
      const auto& declaration =
          std::get<syntax_ast::AstVariableDeclaration>(modern->items[2]);
      expected = declaration.declarators.front().range;
      EXPECT_TRUE(
          ownedPlacement(validateModule(*modern, *module), declaration.range));
      EXPECT_TRUE(validateModule(*legacy, *module));
      for (auto& region : module->header.regions)
        region.version = checker::PtxVersion{3, 0};
      for (auto& function : module->functions)
        function.source_version = checker::PtxVersion{3, 0};
    }
    EXPECT_TRUE(ownedPlacement(validateModule(*module), expected));
    EXPECT_TRUE(ownedPlacement(
        validateModule(*module, ModuleValidationPolicy::AvailableContext),
        expected));
    if (space == ".reg")
      EXPECT_TRUE(module->storage_declarations.empty());
    else
      EXPECT_EQ(module->storage_declarations.size(), 1u);
  }
}

/** Storage record scope mutations retain their independent identity rejection. */
TEST(ModuleDeclarationPlacement, RejectsStorageRecordScopeTampering) {
  const auto ast = placementModule(
      ".version 9.3\n.target sm_80\n.entry kernel() { .local .u32 value; ret; "
      "}");
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  ASSERT_EQ(module->storage_declarations.size(), 1u);
  auto& record = module->storage_declarations.front();
  record.scope_id = module->symbols.moduleScope();
  record.owner_function.reset();
  EXPECT_FALSE(validateModule(*module));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
