#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Isolate global storage, unused prototype and definition linkage without other floors. */
std::string weakSource(unsigned role, std::string_view version,
                       std::string_view target = "sm_20", bool weak = true) {
  std::string source;
  if (!version.empty())
    source += ".version " + std::string(version) + "\n";
  if (!target.empty())
    source += ".target " + std::string(target) + "\n";
  if (weak)
    source += ".weak ";
  source += role == 0   ? ".global .u32 x;\n"
            : role == 1 ? ".func f();\n"
                        : ".func f() { ret; }\n";
  return source;
}

/** Return successful owning data only after producer source/parser/AST destruction. */
std::optional<ResolvedModule> weakOwned(std::string source) {
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

/** Change all version facts coherently without changing the weak payload or target. */
void retargetWeak(ResolvedModule& module,
                  std::optional<checker::PtxVersion> version) {
  for (auto& region : module.header.regions)
    region.version = version;
  for (auto& function : module.functions)
    function.source_version = version;
}

/** Legal published controls establish every role independently of a load/call consumer. */
TEST(WeakLinkageAvailability, FreshStrictRoleControls) {
  for (unsigned role = 0; role < 3; ++role) {
    PtxSyntaxParser parser(weakSource(role, "3.1"));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
    EXPECT_EQ(module->symbols.symbols().front().linkage,
              binding::SymbolLinkage::Weak);
    if (role == 0)
      EXPECT_EQ(module->storage_declarations.front().linkage,
                binding::SymbolLinkage::Weak);
    else
      EXPECT_EQ(module->functions.front().contract.linkage,
                binding::SymbolLinkage::Weak);
  }
}

/** Source and actual AST-dead owning values obey the same introduction window. */
TEST(WeakLinkageAvailability, RejectsSourceAndOwnedBelowIntroduction) {
  for (unsigned role = 0; role < 3; ++role) {
    SCOPED_TRACE(role);
    PtxSyntaxParser parser(weakSource(role, "3.0"));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    auto module = weakOwned(weakSource(role, "3.1"));
    ASSERT_TRUE(module);
    ASSERT_TRUE(validateModule(*module));
    retargetWeak(*module, checker::PtxVersion{3, 0});
    EXPECT_FALSE(validateModule(*module));
    const auto copied = *module;
    ResolvedModule assigned;
    assigned = copied;
    EXPECT_FALSE(validateModule(copied));
    EXPECT_FALSE(validateModule(assigned));
  }
}
/** Require the intended version failure while preserving any separate integrity diagnostics. */
void expectWeakUnavailable(const checker::CheckResult& result,
                           SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
    return error.kind ==
               checker::CheckDiagnosticKind::UnsupportedAvailability &&
           error.range == range &&
           error.message.starts_with(".weak requires PTX ISA >= 3.1");
  }));
}

/** Locate a current owned fact without introducing a public token-range cache. */
SourceRange weakRange(const ResolvedModule& module, unsigned role) {
  return role == 0 ? module.storage_declarations.front().range
                   : module.functions.front().range;
}

/** Known version failures survive absent/unknown targets; all targets impose no SM floor. */
TEST(WeakLinkageAvailability,
     ChecksOptionalContextAndIndependentSuppliedVersion) {
  for (unsigned role = 0; role < 3; ++role)
    for (const auto version : {"3.0", "3.1", ""})
      for (const auto target : {"sm_20", "sm_13", "", "sm_999"}) {
        SCOPED_TRACE(std::to_string(role) + "/" + version + "/" + target);
        auto module = weakOwned(weakSource(
            role, std::string_view(version) == "3.0" ? "3.1" : version,
            target));
        ASSERT_TRUE(module);
        retargetWeak(*module,
                     std::string_view(version).empty()
                         ? std::optional<checker::PtxVersion>{}
                         : std::optional{std::string_view(version) == "3.0"
                                             ? checker::PtxVersion{3, 0}
                                             : checker::PtxVersion{3, 1}});
        PtxSyntaxParser parser(weakSource(role, version, target));
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        const auto range = weakRange(*module, role);
        const bool old = std::string_view(version) == "3.0";
        const bool unknown = std::string_view(target) == "sm_999";
        const bool missing = std::string_view(version).empty() ||
                             std::string_view(target).empty();
        if (old)
          EXPECT_FALSE(resolveModuleOnly(
              *ast));  // Source-owned stage is N_A after semantic rejection.
        for (const auto policy :
             {ModuleValidationPolicy::AvailableContext,
              ModuleValidationPolicy::RequireCompleteContext}) {
          const auto owned = validateModule(*module, policy);
          const auto supplied = validateModule(*ast, *module, policy);
          for (const auto* result : {&owned, &supplied}) {
            if (old) {
              expectWeakUnavailable(*result, range);
              EXPECT_TRUE(
                  std::ranges::none_of(result->error(), [](const auto& error) {
                    return error.kind ==
                           checker::CheckDiagnosticKind::ModuleSourceMismatch;
                  }));
            } else if (unknown && result == &owned &&
                       std::string_view(version).empty() &&
                       policy == ModuleValidationPolicy::AvailableContext) {
              EXPECT_TRUE(*result);
            } else if (unknown) {
              ASSERT_FALSE(*result);
              EXPECT_TRUE(
                  std::ranges::any_of(result->error(), [&](const auto& error) {
                    return error.kind ==
                               checker::CheckDiagnosticKind::UnknownTarget ||
                           (result == &owned &&
                            std::string_view(version).empty() &&
                            error.kind == checker::CheckDiagnosticKind::
                                              MissingValidationContext);
                  }));
            } else if (policy == ModuleValidationPolicy::AvailableContext ||
                       !missing)
              EXPECT_TRUE(*result);
            else {
              ASSERT_FALSE(*result);
              EXPECT_TRUE(std::ranges::any_of(result->error(), [](const auto&
                                                                      error) {
                return error.kind ==
                       checker::CheckDiagnosticKind::MissingValidationContext;
              }));
            }
            if (!old && !*result)
              EXPECT_TRUE(
                  std::ranges::none_of(result->error(), [](const auto& error) {
                    return error.message.starts_with(".weak requires");
                  }));
          }
        }
      }
}

/** Canonical first-weak linkage is sufficient only for successfully checked compatible occurrences. */
TEST(WeakLinkageAvailability, RetainsCompatiblePrototypeDefinitionOccurrences) {
  auto original = weakOwned(
      ".version 3.1\n.target sm_20\n.weak .func f();\n.weak .func f();\n.weak "
      ".func f() { ret; }\n");
  ASSERT_TRUE(original);
  ASSERT_TRUE(validateModule(*original));
  ASSERT_EQ(original->functions.size(), 3);
  const auto symbol = original->functions.front().symbol_id;
  for (const auto& function : original->functions) {
    EXPECT_EQ(function.symbol_id, symbol);
    EXPECT_EQ(function.contract.linkage, binding::SymbolLinkage::Weak);
  }
  EXPECT_EQ(original->symbols.symbol(symbol).linkage,
            binding::SymbolLinkage::Weak);
  auto copied = *original;
  ResolvedModule assigned;
  assigned = copied;
  auto moved = std::move(assigned);
  for (size_t i = 0; i < moved.functions.size(); ++i) {
    const auto function_copy = copied.functions[i];
    moved.functions[i] = function_copy;
    for (auto& instruction : moved.functions[i].body)
      instruction = instruction->clone();
  }
  EXPECT_TRUE(validateModule(*original));
  original.reset();
  retargetWeak(moved, checker::PtxVersion{3, 0});
  for (const auto& function : moved.functions)
    expectWeakUnavailable(validateModule(moved), function.range);
  EXPECT_TRUE(validateModule(copied));
}

/** The source check visits later weak qualifiers even when binding kept an earlier nonweak symbol. */
TEST(WeakLinkageAvailability,
     PreservesMixedRedeclarationRejectionAndChecksEverySourceOccurrence) {
  for (const auto version : {"3.0", "3.1"})
    for (const auto body : {".func f(); .weak .func f() { ret; }",
                            ".weak .func f(); .func f() { ret; }",
                            ".global .u32 x; .weak .global .u32 x;",
                            ".weak .global .u32 x; .global .u32 x;"}) {
      PtxSyntaxParser parser(".version " + std::string(version) +
                             "\n.target sm_20\n" + body);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto binding = binding::bindSymbols(*ast);
      ASSERT_FALSE(binding.table.symbols().empty());
      EXPECT_EQ(binding.table.symbols().front().linkage,
                std::string_view(body).starts_with(".weak")
                    ? binding::SymbolLinkage::Weak
                    : binding::SymbolLinkage::None);
      const auto diagnostics =
          declaration_semantics::checkDeclarations(*ast, binding.table);
      EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& error) {
        return error.kind == declaration_semantics::DeclarationDiagnosticKind::
                                 IncompatibleRedeclaration;
      }));
      EXPECT_EQ(
          std::ranges::any_of(
              diagnostics,
              [](const auto& error) {
                return error.kind ==
                           declaration_semantics::DeclarationDiagnosticKind::
                               UnsupportedDirectivePtxVersion &&
                       error.message.starts_with(
                           ".weak requires PTX ISA >= 3.1");
              }),
          std::string_view(version) == "3.0");
      EXPECT_FALSE(resolveModuleOnly(*ast));
    }
}

/** A nonweak canonical cache cannot mask any current storage or function weak fact. */
TEST(WeakLinkageAvailability,
     RechecksCurrentFactsAgainstOriginalNonweakSource) {
  for (unsigned role = 0; role < 3; ++role) {
    PtxSyntaxParser parser(weakSource(role, "3.0", "sm_20", false));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    EXPECT_EQ(module->symbols.symbols().front().linkage,
              binding::SymbolLinkage::None);
    if (role == 0)
      module->storage_declarations.front().linkage =
          binding::SymbolLinkage::Weak;
    else
      module->functions.front().contract.linkage = binding::SymbolLinkage::Weak;
    const auto range = weakRange(*module, role);
    expectWeakUnavailable(validateModule(*module), range);
    expectWeakUnavailable(validateModule(*ast, *module), range);
    // Storage linkage equality remains an independent existing integrity rule.
  }
}

/** Whole-table public rebinding supplies a weak canonical symbol without changing retained contracts. */
TEST(WeakLinkageAvailability,
     ChecksCurrentSymbolAndIndependentSuppliedQualifiers) {
  for (unsigned role = 0; role < 3; ++role) {
    auto source = weakSource(role, "3.1");
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    PtxSyntaxParser plain_parser(source);
    auto plain_ast = plain_parser.parseModule();
    ASSERT_TRUE(plain_ast && plain_ast.diagnostics.empty());
    // Public AST editing preserves exact owner extents while changing typed linkage.
    for (auto& item : plain_ast->items) {
      if (auto* declaration =
              std::get_if<syntax_ast::AstVariableDeclaration>(&item))
        declaration->qualifiers.clear();
      else if (auto* function = std::get_if<syntax_ast::AstFunction>(&item))
        function->qualifiers.clear();
    }
    const auto rebound = binding::bindSymbols(*plain_ast);
    module->symbols = rebound.table;
    if (role == 0)
      module->storage_declarations.front().linkage =
          binding::SymbolLinkage::None;
    else
      module->functions.front().contract.linkage = binding::SymbolLinkage::None;
    retargetWeak(*module, checker::PtxVersion{3, 0});
    EXPECT_TRUE(validateModule(*module));
    auto old = source;
    old.replace(old.find("3.1"), 3, "3.0");
    PtxSyntaxParser old_parser(old);
    const auto old_ast = old_parser.parseModule();
    ASSERT_TRUE(old_ast && old_ast.diagnostics.empty());
    const auto supplied = validateModule(*old_ast, *module);
    ASSERT_FALSE(supplied);
    EXPECT_TRUE(std::ranges::any_of(supplied.error(), [](const auto& error) {
      return error.kind ==
                 checker::CheckDiagnosticKind::UnsupportedAvailability &&
             error.message.starts_with(".weak requires PTX ISA >= 3.1");
    }));
    const auto weak_binding = binding::bindSymbols(*ast);
    module->symbols = weak_binding.table;
    const auto range = role == 0
                           ? module->symbols.symbols().front().declaration_range
                           : module->functions.front().range;
    expectWeakUnavailable(validateModule(*module), range);
    expectWeakUnavailable(validateModule(*old_ast, *module), range);
    const auto mismatch = validateModule(*plain_ast, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& error) {
      return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  }
}

/** Function-symbol and function-contract facts use selected versions rather than first-region fallback. */
TEST(WeakLinkageAvailability,
     UsesSelectedFunctionVersionAndCoherentAllRegionRetarget) {
  const std::string source =
      ".version 3.1\n.target sm_20\n.func ordinary();\n.target sm_20\n.weak "
      ".func f() { ret; }\n";
  auto module = weakOwned(source);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  const auto weak_index = *module->functions.back().source_region;
  EXPECT_NE(weak_index, *module->functions.front().source_region);
  // Existing module integrity requires all regions to share one PTX version.
  retargetWeak(*module, checker::PtxVersion{3, 0});
  expectWeakUnavailable(validateModule(*module),
                        module->functions.back().range);
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*ast, *module));
}

/** Other legal linkage kinds and their existing eligibility remain independent. */
TEST(WeakLinkageAvailability, PreservesOtherLinkageControls) {
  for (const auto source :
       {".version 3.0\n.target sm_20\n.global .u32 x; .visible .global .u32 y; "
        ".extern .global .u32 z; .func a(); .visible .func b(); .extern .func "
        "c();\n",
        ".version 5.0\n.target sm_20\n.common .global .u32 x;\n"}) {
    auto module = weakOwned(source);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
}
/** Equal-width module shells preserve geometry while replacing neutral file metadata with a donor. */
std::string orphanSource(std::string_view version, std::string_view target,
                         bool donor, bool multiple_regions = false) {
  std::string source;
  if (!version.empty())
    source += ".version " + std::string(version) + "\n";
  if (!target.empty())
    source += ".target " + std::string(target) + "\n";
  source += ".func ordinary();\n";
  if (multiple_regions)
    source += ".target sm_13\n";
  return source +
         (donor ? ".weak .func orphan();\n" : ".file 1 \"abcdefghij\";\n");
}

/** Public whole-table donors retain owner geometry and leave source identity untouched. */
std::optional<ResolvedModule> orphanOwned(std::string_view version,
                                          std::string_view target, bool weak,
                                          bool multiple_regions = false) {
  PtxSyntaxParser parser(
      orphanSource(version, target, false, multiple_regions));
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveModuleOnly(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  PtxSyntaxParser donor_parser(orphanSource(version == "3.0" ? "3.1" : version,
                                            target, true, multiple_regions));
  auto donor_ast = donor_parser.parseModule();
  EXPECT_TRUE(donor_ast && donor_ast.diagnostics.empty());
  if (!donor_ast || !donor_ast.diagnostics.empty())
    return std::nullopt;
  EXPECT_EQ(module->range, donor_ast->range);
  const auto weak_binding = binding::bindSymbols(*donor_ast);
  std::get<syntax_ast::AstFunction>(donor_ast->items.back()).qualifiers.clear();
  const auto strong_binding = binding::bindSymbols(*donor_ast);
  EXPECT_TRUE(weak_binding.diagnostics.empty());
  EXPECT_TRUE(strong_binding.diagnostics.empty());
  EXPECT_EQ(weak_binding.table.scopes().size(),
            strong_binding.table.scopes().size());
  for (size_t i = 0; i < weak_binding.table.scopes().size(); ++i) {
    const auto& a = weak_binding.table.scopes()[i];
    const auto& b = strong_binding.table.scopes()[i];
    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.range, b.range);
    EXPECT_EQ(a.parent, b.parent);
    EXPECT_EQ(a.owner, b.owner);
    EXPECT_EQ(a.kind, b.kind);
  }
  module->symbols = weak ? weak_binding.table : strong_binding.table;
  EXPECT_EQ(module->functions.size(), 1);
  EXPECT_EQ(module->symbols.symbols().size(), 2);
  EXPECT_EQ(module->symbols.symbols().back().name, "orphan");
  EXPECT_EQ(module->symbols.symbols().back().linkage,
            weak ? binding::SymbolLinkage::Weak : binding::SymbolLinkage::None);
  return std::move(*module);
}

/** All current Weak symbols are checked even without an occurrence; source context replaces owned context. */
TEST(WeakLinkageAvailability, ChecksMatchedOrphanSymbolsAndSuppliedRetargets) {
  PtxSyntaxParser legal_parser(orphanSource("3.1", "sm_20", true));
  const auto legal = legal_parser.parseModule();
  ASSERT_TRUE(legal && legal.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*legal));
  for (const auto version : {"3.0", "3.1"})
    for (const bool weak : {false, true}) {
      auto module = orphanOwned(version, "sm_20", weak);
      ASSERT_TRUE(module);
      const auto range = module->symbols.symbols().back().declaration_range;
      PtxSyntaxParser parser(orphanSource(version, "sm_20", false));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      PtxSyntaxParser replacement_parser(orphanSource(
          std::string_view(version) == "3.0" ? "3.1" : "3.0", "sm_20", false));
      const auto replacement = replacement_parser.parseModule();
      ASSERT_TRUE(replacement && replacement.diagnostics.empty());
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        const auto owned = validateModule(*module, policy);
        const auto supplied = validateModule(*ast, *module, policy);
        if (weak && std::string_view(version) == "3.0") {
          expectWeakUnavailable(owned, range);
          expectWeakUnavailable(supplied, range);
        } else {
          EXPECT_TRUE(owned);
          EXPECT_TRUE(supplied);
        }
        const auto retargeted = validateModule(*replacement, *module, policy);
        if (weak && std::string_view(version) == "3.1")
          expectWeakUnavailable(retargeted, range);
        else
          EXPECT_TRUE(retargeted);
      }
    }
}

/** Uniform optional versions and multiple targets retain their existing policies. */
TEST(WeakLinkageAvailability, KeepsOrphanPartialAndUniformRegionPolicies) {
  for (const auto version : {"3.0", "3.1", ""})
    for (const auto target : {"", "sm_20", "sm_999"}) {
      auto module = orphanOwned(version, target, true);
      ASSERT_TRUE(module);
      const auto range = module->symbols.symbols().back().declaration_range;
      PtxSyntaxParser parser(orphanSource(version, target, false));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        const auto owned = validateModule(*module, policy);
        const auto supplied = validateModule(*ast, *module, policy);
        for (const auto* result : {&owned, &supplied}) {
          if (std::string_view(version) == "3.0")
            expectWeakUnavailable(*result, range);
          else if (std::string_view(target) == "sm_999" &&
                   !(result == &owned && std::string_view(version).empty() &&
                     policy == ModuleValidationPolicy::AvailableContext)) {
            ASSERT_FALSE(*result);
            EXPECT_TRUE(std::ranges::any_of(result->error(), [](const auto&
                                                                    error) {
              return error.kind ==
                         checker::CheckDiagnosticKind::UnknownTarget ||
                     error.kind ==
                         checker::CheckDiagnosticKind::MissingValidationContext;
            }));
          } else if (policy == ModuleValidationPolicy::AvailableContext ||
                     (!std::string_view(version).empty() &&
                      !std::string_view(target).empty()))
            EXPECT_TRUE(*result);
          else {
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
  auto multiple = orphanOwned("3.1", "sm_20", true, true);
  ASSERT_TRUE(multiple);
  ASSERT_TRUE(validateModule(*multiple));
  ASSERT_GT(multiple->header.regions.size(), 2);
  for (const bool missing : {false, true}) {
    auto mixed = *multiple;
    mixed.header.regions.back().version =
        missing ? std::optional<checker::PtxVersion>{}
                : std::optional{checker::PtxVersion{3, 0}};
    if (missing)
      mixed.header.regions.back().version_provenance =
          SourceConfigurationProvenance::Missing;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      const auto result = validateModule(mixed, policy);
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
        return error.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               error.message ==
                   "Resolved target regions disagree on module PTX version.";
      }));
    }
  }
  retargetWeak(*multiple, checker::PtxVersion{3, 0});
  expectWeakUnavailable(validateModule(*multiple),
                        multiple->symbols.symbols().back().declaration_range);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
