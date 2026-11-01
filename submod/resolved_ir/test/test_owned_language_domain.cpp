#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Build a legal source whose raw language list is independent of instruction syntax. */
std::string languageSource(std::string_view values = "\"ptx\"") {
  return ".version 9.3\n.target sm_80\n.entry kernel() .language " +
         std::string(values) + " { .reg .u32 r; mov.u32 r, 1; ret; }\n";
}

/** Return an owned valid module after destroying its actual source and AST allocations. */
std::optional<ResolvedModule> languageOwned(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveAndValidateModule(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Require the actual current-value rule at its existing function-level extent. */
void expectLanguageDomain(const checker::CheckResult& result,
                          SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(
      result.error(), [&](const checker::CheckDiagnostic& error) {
        return error.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               error.range == range &&
               error.message ==
                   "Resolved .language requires a nonempty list of supported "
                   "raw tokens.";
      }));
}

/** Preserve explicit partial source context without requiring a strict complete module. */
std::optional<ResolvedModule> languageTransport(std::string source) {
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

/** Intrinsic current values must reject even with an independently held legal AST. */
TEST(OwnedLanguageDomain, RechecksInvalidCurrentWordsAndCardinality) {
  auto original = languageOwned(languageSource());
  ASSERT_TRUE(original);
  ASSERT_TRUE(validateModule(*original));
  PtxSyntaxParser separate_parser(languageSource());
  const auto legal_ast = separate_parser.parseModule();
  ASSERT_TRUE(legal_ast && legal_ast.diagnostics.empty());
  for (const std::vector<std::string> words :
       {std::vector<std::string>{"\"bogus\""}, {"11"}, {}}) {
    auto edited = *original;
    edited.functions[0].contract.language_values = words;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      expectLanguageDomain(validateModule(edited, policy),
                           edited.functions[0].range);
      expectLanguageDomain(validateModule(*legal_ast, edited, policy),
                           edited.functions[0].range);
      const auto copied = edited;
      expectLanguageDomain(validateModule(copied, policy),
                           edited.functions[0].range);
      ResolvedModule assigned;
      assigned = copied;
      expectLanguageDomain(validateModule(assigned, policy),
                           edited.functions[0].range);
      auto function_owner = copied;
      const auto function_copy = copied.functions[0];
      function_owner.functions[0] = function_copy;
      expectLanguageDomain(validateModule(function_owner, policy),
                           edited.functions[0].range);
      ResolvedFunction function_assigned;
      function_assigned = function_copy;
      function_owner.functions[0] = std::move(function_assigned);
      expectLanguageDomain(validateModule(function_owner, policy),
                           edited.functions[0].range);
      function_owner.functions[0].body[0] =
          function_owner.functions[0].body[0]->clone();
      expectLanguageDomain(validateModule(function_owner, policy),
                           edited.functions[0].range);
    }
    EXPECT_TRUE(validateModule(*original));
  }
}

/** Invalid source is not treated as an owned lifetime result when parsing/lowering rejects it. */
TEST(OwnedLanguageDomain, RejectsInvalidSourceBeforeOwnedMutation) {
  for (const auto value : {"\"bogus\"", "11", ""}) {
    PtxSyntaxParser parser(languageSource(value));
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      continue;
    EXPECT_FALSE(resolveModuleOnly(*ast));
  }
}

/** All official names, Reserved codes and radix aliases remain exact owned raw strings. */
TEST(OwnedLanguageDomain, RetainsTheCompleteDomainAndRawLists) {
  const std::array<std::string_view, 9> names{
      "unknown", "ptx",           "nvvm",    "cuda c++", "cuda c++ tile",
      "tile ir", "python-cutile", "fortran", "optix"};
  for (const auto name : names) {
    const auto word = "\"" + std::string(name) + "\"";
    auto module = languageOwned(languageSource(word));
    ASSERT_TRUE(module);
    EXPECT_EQ(*module->functions[0].contract.language_values,
              std::vector<std::string>{word});
    EXPECT_TRUE(validateModule(*module));
    const auto copy = *module;
    ResolvedModule assigned;
    assigned = copy;
    EXPECT_EQ(assigned.functions[0].contract.language_values,
              module->functions[0].contract.language_values);
    EXPECT_TRUE(validateModule(assigned));
  }
  /** Preserve the exact mixed-case strings, not just their decoded values. */
  const std::array<std::string_view, 9> mixed{
      "\"UnKnOwN\"",       "\"PtX\"",           "\"NvVm\"",
      "\"CuDa c++\"",      "\"CuDa c++ tIlE\"", "\"TiLe iR\"",
      "\"PyThOn-cUtIlE\"", "\"FoRtRaN\"",       "\"OpTiX\""};
  for (const auto word : mixed) {
    const auto source = languageSource(word);
    auto module = languageOwned(source);
    ASSERT_TRUE(module);
    EXPECT_EQ(*module->functions[0].contract.language_values,
              std::vector<std::string>{std::string(word)});
    EXPECT_TRUE(validateModule(*module));
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(validateModule(*ast, *module));
  }
  for (uint64_t code = 0; code <= 10; ++code) {
    auto module = languageOwned(languageSource(std::to_string(code)));
    ASSERT_TRUE(module);
    EXPECT_EQ(module->functions[0].contract.language_values->front(),
              std::to_string(code));
    EXPECT_TRUE(validateModule(*module));
  }
  for (const auto word : {"0x3U", "03u", "0b11U"}) {
    auto module = languageOwned(languageSource(word));
    ASSERT_TRUE(module);
    EXPECT_EQ(module->functions[0].contract.language_values->front(), word);
    EXPECT_TRUE(validateModule(*module));
  }
  const auto source = languageSource("\"PTX\", 0x3U, 1, 2, \"nvvm\", \"PTX\"");
  auto module = languageOwned(source);
  ASSERT_TRUE(module);
  const std::vector<std::string> expected{"\"PTX\"", "0x3U",     "1",
                                          "2",       "\"nvvm\"", "\"PTX\""};
  EXPECT_EQ(*module->functions[0].contract.language_values, expected);
  const auto original = *module;
  module->functions[0].contract.language_values =
      std::vector<std::string>{"4", "3"};
  PtxSyntaxParser original_parser(source);
  const auto ast = original_parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(validateModule(*ast, *module));
  module->functions[0].contract.language_values.reset();
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(validateModule(*ast, *module));
  EXPECT_EQ(*original.functions[0].contract.language_values, expected);
  EXPECT_TRUE(validateModule(original));
  auto device_source = languageSource("\"ptx\"");
  device_source.replace(device_source.find(".entry"), 6, ".func");
  auto device = languageOwned(device_source);
  ASSERT_TRUE(device);
  EXPECT_FALSE(device->functions[0].is_entry);
  EXPECT_TRUE(validateModule(*device));
}

/** Every invalid one-word partition is independently checked in current owned data. */
TEST(OwnedLanguageDomain, RejectsAllInvalidRawTokenPartitions) {
  const std::vector<std::string> invalid{"\"bogus\"",
                                         "\"\"",
                                         "\"Reserved\"",
                                         "\"3\"",
                                         "ptx",
                                         "11",
                                         "18446744073709551615U",
                                         "18446744073709551616U",
                                         "-1",
                                         "+3",
                                         "3.0",
                                         "1+2",
                                         "WARP_SZ",
                                         " 3",
                                         "3 ",
                                         "\" ptx\"",
                                         "\"cuda  c++\"",
                                         "\"python cutile\"",
                                         "\"\\x70tx\"",
                                         "\"cuda\\x20c++\"",
                                         "\"ptx",
                                         "\"ptx\"\"",
                                         "3,4"};
  auto original = languageOwned(languageSource());
  ASSERT_TRUE(original);
  PtxSyntaxParser legal_parser(languageSource());
  const auto legal_ast = legal_parser.parseModule();
  ASSERT_TRUE(legal_ast && legal_ast.diagnostics.empty());
  for (size_t index = 0; index < invalid.size(); ++index) {
    const auto& word = invalid[index];
    SCOPED_TRACE(word);
    EXPECT_FALSE(declaration_semantics::languageCode(word));
    auto module = *original;
    module.functions[0].contract.language_values =
        std::vector<std::string>{word};
    expectLanguageDomain(validateModule(module), module.functions[0].range);
    expectLanguageDomain(validateModule(*legal_ast, module),
                         module.functions[0].range);
    const auto copied = module;
    expectLanguageDomain(validateModule(copied), module.functions[0].range);
    PtxSyntaxParser parser(languageSource(word));
    const auto ast = parser.parseModule();
    const bool different_tokens = word == " 3" || word == "3 " || word == "3,4";
    if (different_tokens) {
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto source_module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(source_module);
      EXPECT_NE(*source_module->functions[0].contract.language_values,
                std::vector<std::string>{word});
      std::cout
          << "language-invalid row=" << index
          << " source=VALID_DIFFERENT_TOKENIZATION owned=DEAD_MUTANT_REJECT\n";
    } else if (!ast || !ast.diagnostics.empty()) {
      std::cout
          << "language-invalid row=" << index
          << " source=PARSE_REJECT source_owned=N_A owned=DEAD_MUTANT_REJECT\n";
    } else {
      const auto draft = resolveModuleOnly(*ast);
      EXPECT_FALSE(draft);
      std::cout << "language-invalid row=" << index
                << " source=RESOLUTION_REJECT source_owned=N_A "
                   "owned=DEAD_MUTANT_REJECT\n";
    }
    EXPECT_TRUE(validateModule(*original));
  }
  auto second = *original;
  second.functions[0].contract.language_values =
      std::vector<std::string>{"3", "11"};
  expectLanguageDomain(validateModule(second), second.functions[0].range);
}

/** Source identity remains raw/ordered even when decoded aliases or lists are valid. */
TEST(OwnedLanguageDomain, PreservesSourceIdentityWithoutCurrentPayloadHistory) {
  for (const bool reorder : {false, true}) {
    const auto source = languageSource(reorder ? "3,4" : "\"ptx\"");
    auto module = languageOwned(source);
    ASSERT_TRUE(module);
    PtxSyntaxParser original_parser(source);
    const auto original_ast = original_parser.parseModule();
    ASSERT_TRUE(original_ast && original_ast.diagnostics.empty());
    EXPECT_TRUE(validateModule(*original_ast, *module));
    const auto changed = languageSource(reorder ? "4,3" : "3");
    PtxSyntaxParser parser(changed);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    const auto checked = validateModule(*ast, *module);
    ASSERT_FALSE(checked);
    EXPECT_TRUE(std::ranges::any_of(
        checked.error(), [](const checker::CheckDiagnostic& error) {
          return error.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    module->functions[0].contract.language_values =
        std::vector<std::string>{"4", "3"};
    EXPECT_TRUE(validateModule(*module));
    EXPECT_TRUE(validateModule(*original_ast, *module));
  }
}

/** Intrinsic validity is unconditional; known version and missing context stay distinct. */
TEST(OwnedLanguageDomain, SeparatesDomainAvailabilityAndPartialContext) {
  for (const bool missing_version : {false, true}) {
    auto source = languageSource();
    const auto start = source.find(missing_version ? ".version" : ".target");
    source.erase(start, source.find('\n', start) - start + 1);
    auto module = languageTransport(source);
    ASSERT_TRUE(module);
    EXPECT_TRUE(
        validateModule(*module, ModuleValidationPolicy::AvailableContext));
    const auto strict = validateModule(*module);
    ASSERT_FALSE(strict);
    EXPECT_TRUE(std::ranges::any_of(
        strict.error(), [](const checker::CheckDiagnostic& error) {
          return error.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(validateModule(*ast, *module,
                               ModuleValidationPolicy::AvailableContext));
    for (const auto words :
         {std::vector<std::string>{"11"}, std::vector<std::string>{}}) {
      auto edited = *module;
      edited.functions[0].contract.language_values = words;
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        expectLanguageDomain(validateModule(edited, policy),
                             edited.functions[0].range);
        expectLanguageDomain(validateModule(*ast, edited, policy),
                             edited.functions[0].range);
      }
    }
    if (!missing_version) {
      for (auto& region : module->header.regions)
        region.version = checker::PtxVersion{9, 2};
      for (auto& function : module->functions)
        function.source_version = checker::PtxVersion{9, 2};
      const auto old =
          validateModule(*module, ModuleValidationPolicy::AvailableContext);
      ASSERT_FALSE(old);
      EXPECT_TRUE(std::ranges::any_of(
          old.error(), [&](const checker::CheckDiagnostic& error) {
            return error.kind ==
                       checker::CheckDiagnosticKind::UnsupportedAvailability &&
                   error.range == module->functions[0].range &&
                   error.message == ".language requires PTX ISA >= 9.3.";
          }));
      EXPECT_TRUE(validateModule(*ast, *module,
                                 ModuleValidationPolicy::AvailableContext));
      auto old_source = source;
      old_source.replace(old_source.find("9.3"), 3, "9.2");
      PtxSyntaxParser old_parser(old_source);
      const auto old_ast = old_parser.parseModule();
      ASSERT_TRUE(old_ast && old_ast.diagnostics.empty());
      const auto supplied_old = validateModule(
          *old_ast, *module, ModuleValidationPolicy::AvailableContext);
      ASSERT_FALSE(supplied_old);
      EXPECT_TRUE(std::ranges::any_of(
          supplied_old.error(), [](const checker::CheckDiagnostic& error) {
            return error.kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability;
          }));
    }
  }
  auto low_source = languageSource();
  low_source.replace(low_source.find("sm_80"), 5, "sm_20");
  auto low = languageOwned(low_source);
  ASSERT_TRUE(low);
  EXPECT_TRUE(validateModule(*low));
}

/** A new current presence cannot hide behind source that originally omitted the directive. */
TEST(OwnedLanguageDomain, RechecksCurrentPresenceUsingSuppliedVersion) {
  std::string source = ".version 9.2\n.entry kernel() { ret; }\n";
  auto module = languageTransport(source);
  ASSERT_TRUE(module);
  module->functions[0].contract.language_values = std::vector<std::string>{"3"};
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  for (const auto& result :
       {validateModule(*module, ModuleValidationPolicy::AvailableContext),
        validateModule(*ast, *module,
                       ModuleValidationPolicy::AvailableContext)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(
        result.error(), [](const checker::CheckDiagnostic& error) {
          return error.kind ==
                     checker::CheckDiagnosticKind::UnsupportedAvailability &&
                 error.message == ".language requires PTX ISA >= 9.3.";
        }));
  }
  source.replace(source.find("9.2"), 3, "9.3");
  PtxSyntaxParser newer(source);
  const auto new_ast = newer.parseModule();
  ASSERT_TRUE(new_ast && new_ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*new_ast, *module,
                             ModuleValidationPolicy::AvailableContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
