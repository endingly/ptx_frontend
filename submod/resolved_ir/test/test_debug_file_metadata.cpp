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

constexpr std::string_view header = ".version 9.3\n.target sm_80\n";

/** Enumerate repeat spellings without treating metadata equality as identity. */
std::vector<std::string> repeatedFiles() {
  return {".file 1 \"a.cu\"\n.file 1 \"b.cu\"\n",
          ".file 1 \"a.cu\"\n.file 1 \"a.cu\"\n",
          ".file 1 \"a.cu\", 7, 8\n.file 1 \"a.cu\", 7, 8\n",
          ".file 1 \"a.cu\", 7, 8\n.file 1 \"a.cu\", 9, 8\n",
          ".file 1 \"a.cu\", 7, 8\n.file 1 \"a.cu\", 7, 9\n",
          ".file 1 \"a.cu\"\n.file 1 \"a.cu\", 0, 0\n",
          ".file 01U \"a.cu\"\n.file 0x1u \"a.cu\"\n",
          ".file 0b1 \"a.cu\"\n.file 1 \"a.cu\"\n",
          ".file 1 \"a.cu\"\n.file 1 \"a\\056cu\"\n"};
}

/** Reusing a file index must fail even if no instruction references it. */
TEST(DebugFileMetadata, RejectsConflictingUnusedFileIndex) {
  PtxSyntaxParser parser(std::string(header) +
                         ".file 1 \"a.cu\"\n.file 1 \"b.cu\"\n");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  EXPECT_FALSE(resolveAndValidateModule(*ast));
}

/** Every repeated canonical index diagnoses the current and first index tokens. */
TEST(DebugFileMetadata, RejectsEveryRepeatWithProvenance) {
  for (const auto& files : repeatedFiles()) {
    SCOPED_TRACE(files);
    PtxSyntaxParser parser(std::string(header) + files);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    ASSERT_EQ(bound.diagnostics.size(), 1u);
    const auto& first = std::get<syntax_ast::AstFileDirective>(ast->items[2]);
    const auto& second = std::get<syntax_ast::AstFileDirective>(ast->items[3]);
    EXPECT_EQ(bound.diagnostics[0].kind,
              binding::BindDiagnosticKind::DuplicateSymbol);
    EXPECT_EQ(bound.diagnostics[0].range, second.file_index.range);
    EXPECT_EQ(bound.diagnostics[0].previous_range, first.file_index.range);
    ASSERT_EQ(bound.table.debugFiles().size(), 2u);
    EXPECT_EQ(bound.table.debugFiles()[0].symbol_id,
              bound.table.debugFiles()[1].symbol_id);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Distinct indices own their exact payloads and keep basic/inline references joined. */
TEST(DebugFileMetadata, RetainsPayloadAndReferencesAfterSourceDestruction) {
  std::optional<ResolvedModule> module;
  {
    const std::string source =
        std::string(header) + R"ptx(.file 0x1U "same\\name.cu", 07U, 0x10
.file 02 "same\\name.cu"
.section .debug_str { name: .b8 0; };
.entry k() {
.loc 0x2U 1 0
.loc 1 1 0, function_name name, inlined_at 0x2U 1 0
ret;
}
)ptx";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    module.emplace(std::move(*result));
  }
  ASSERT_EQ(module->symbols.debugFiles().size(), 2u);
  const auto& first = module->symbols.debugFiles()[0];
  const auto& second = module->symbols.debugFiles()[1];
  EXPECT_EQ(first.index, 1u);
  EXPECT_EQ(first.source_index.text, "0x1U");
  EXPECT_EQ(first.filename.text, R"("same\\name.cu")");
  ASSERT_TRUE(first.timestamp);
  ASSERT_TRUE(first.file_size);
  EXPECT_EQ(first.timestamp->text, "07U");
  EXPECT_EQ(first.file_size->text, "0x10");
  EXPECT_EQ(first.range.start.line, 3);
  EXPECT_EQ(first.source_index.range.start.column, 7);
  EXPECT_EQ(first.filename.range.start.column, 12);
  EXPECT_EQ(second.index, 2u);
  EXPECT_EQ(first.filename.text, second.filename.text);
  EXPECT_FALSE(second.timestamp);
  EXPECT_FALSE(second.file_size);
  size_t references = 0;
  for (const auto& reference : module->symbols.references()) {
    if (reference.kind != binding::ReferenceKind::DebugFile)
      continue;
    ASSERT_TRUE(reference.target);
    EXPECT_EQ(reference.target->symbol,
              references == 1 ? first.symbol_id : second.symbol_id);
    ++references;
  }
  EXPECT_EQ(references, 3u);
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  const ResolvedModule copy = *module;
  EXPECT_TRUE(validateModule(copy));
}

/** A retained invalid table cannot become valid by discarding binding diagnostics. */
TEST(DebugFileMetadata, RechecksDuplicateTablesInBothValidationOverloads) {
  for (const auto& files : repeatedFiles()) {
    for (const bool referenced : {false, true}) {
      SCOPED_TRACE(files);
      const std::string body =
          referenced ? ".entry k() { .loc 1 1 0 ret; }\n" : "";
      PtxSyntaxParser valid(std::string(header) + ".file 1 \"a.cu\"\n" + body);
      const auto valid_ast = valid.parseModule();
      ASSERT_TRUE(valid_ast);
      auto module = resolveAndValidateModule(*valid_ast);
      ASSERT_TRUE(module);
      PtxSyntaxParser replacement(std::string(header) + files + body);
      const auto ast = replacement.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      module->symbols = binding::bindSymbols(*ast).table;
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        EXPECT_FALSE(validateModule(*module, policy));
        EXPECT_FALSE(validateModule(*ast, *module, policy));
      }
    }
  }
}

/** Legal replacement filenames and file indices remain independent debug metadata. */
TEST(DebugFileMetadata, KeepsValidReplacementMetadataNeutral) {
  PtxSyntaxParser original(
      std::string(header) +
      ".file 1 \"a.cu\"\n.entry k() { .loc 1 1 0 ret; }\n");
  const auto original_ast = original.parseModule();
  ASSERT_TRUE(original_ast);
  const auto module = resolveAndValidateModule(*original_ast);
  ASSERT_TRUE(module);
  PtxSyntaxParser replacement(
      std::string(header) +
      ".file 2 \"b.cu\", 0, 0\n.entry k() { .loc 2 2 0 ret; }\n");
  const auto replacement_ast = replacement.parseModule();
  ASSERT_TRUE(replacement_ast);
  EXPECT_TRUE(validateModule(*replacement_ast, *module));
  EXPECT_TRUE(validateModule(*replacement_ast, *module,
                             ModuleValidationPolicy::RequireCompleteContext));
}

/** Constructed source cannot smuggle malformed retained payload boundaries. */
TEST(DebugFileMetadata, RejectsMalformedRetainedPayloadRanges) {
  for (const bool zero_position : {false, true}) {
    PtxSyntaxParser parser(std::string(header) + ".file 1 \"a.cu\", 7, 8\n");
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& file = std::get<syntax_ast::AstFileDirective>(ast->items.back());
    file.filename.range =
        zero_position ? SourceRange{{0, 0}, {0, 6}} : file.file_index.range;
    module->symbols = binding::bindSymbols(*ast).table;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      EXPECT_FALSE(validateModule(*module, policy));
      EXPECT_FALSE(validateModule(*ast, *module, policy));
    }
  }
}

/** Explicit numeric metadata uses its presence, including zeros, to select PTX 3.2. */
TEST(DebugFileMetadata, EnforcesNumericMetadataVersionInBothValidationRoutes) {
  for (const std::string_view version : {"3.1", "3.2"}) {
    for (const std::string_view suffix : {"", ", 7, 8", ", 0, 0"}) {
      const std::string source = ".version " + std::string(version) +
                                 "\n.target sm_20\n.file 1 \"a.cu\"" +
                                 std::string(suffix) + "\n";
      SCOPED_TRACE(source);
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      const auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      const bool accepted = version == "3.2" || suffix.empty();
      const auto& file = module->symbols.debugFiles().front();
      for (const auto policy :
           {ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::RequireCompleteContext}) {
        EXPECT_EQ(bool(resolveAndValidateModule(*ast)), accepted);
        for (const auto& result : {validateModule(*module, policy),
                                   validateModule(*ast, *module, policy)}) {
          EXPECT_EQ(bool(result), accepted);
          if (!result) {
            ASSERT_EQ(result.error().size(), 1u);
            EXPECT_EQ(result.error().front().kind,
                      checker::CheckDiagnosticKind::UnsupportedAvailability);
            EXPECT_EQ(result.error().front().range, file.range);
            EXPECT_EQ(result.error().front().message,
                      ".file timestamp and file size require PTX ISA >= 3.2.");
          }
        }
      }
    }
  }
}

/** A known old version fails without a target, while an absent version defers. */
TEST(DebugFileMetadata, UsesVersionWithoutRequiringTargetContext) {
  for (const std::string_view prefix : {".version 3.1\n", ""}) {
    PtxSyntaxParser parser(std::string(prefix) + ".file 1 \"a.cu\", 0, 0\n");
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    const auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module);
    EXPECT_EQ(bool(resolveModule(*ast)), prefix.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    for (const auto& result :
         {validateModule(*module, ModuleValidationPolicy::AvailableContext),
          validateModule(*ast, *module,
                         ModuleValidationPolicy::AvailableContext)}) {
      EXPECT_EQ(bool(result), prefix.empty());
      if (!result) {
        ASSERT_EQ(result.error().size(), 1u);
        EXPECT_EQ(result.error().front().kind,
                  checker::CheckDiagnosticKind::UnsupportedAvailability);
      }
    }
    EXPECT_FALSE(validateModule(*module));
    EXPECT_FALSE(validateModule(*ast, *module));
  }
}

/** Unknown targets retain their diagnostics and cannot suppress the version gate. */
TEST(DebugFileMetadata, RejectsOldMetadataAlongsideUnknownTarget) {
  PtxSyntaxParser parser(
      ".version 3.1\n.target sm_unknown\n.file 1 \"a.cu\", 0, 0\n");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module);
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    for (const auto& result : {validateModule(*module, policy),
                               validateModule(*ast, *module, policy)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                       checker::CheckDiagnosticKind::UnsupportedAvailability &&
                   diagnostic.message ==
                       ".file timestamp and file size require PTX ISA >= 3.2.";
          }));
    }
  }
}

/** Replacement source supplies both the debug payload and the version gate. */
TEST(DebugFileMetadata, RebindsReplacementPayloadAndVersionTogether) {
  for (const std::string_view retained :
       {".version 3.1\n.target sm_20\n.file 1 \"a.cu\"\n",
        ".version 3.2\n.target sm_20\n.file 1 \"a.cu\", 0, 0\n"}) {
    PtxSyntaxParser original{std::string(retained) +
                             ".entry k() { .loc 1 1 0 ret; }\n"};
    const auto original_ast = original.parseModule();
    ASSERT_TRUE(original_ast);
    const auto module = resolveAndValidateModule(*original_ast);
    ASSERT_TRUE(module);
    for (const std::string_view version : {"3.1", "3.2"}) {
      for (const std::string_view suffix : {"", ", 0, 0"}) {
        PtxSyntaxParser replacement(".version " + std::string(version) +
                                    "\n.target sm_20\n.file 1 \"a.cu\"" +
                                    std::string(suffix) +
                                    "\n.entry k() { .loc 1 1 0 ret; }\n");
        const auto ast = replacement.parseModule();
        ASSERT_TRUE(ast);
        for (const auto policy :
             {ModuleValidationPolicy::AvailableContext,
              ModuleValidationPolicy::RequireCompleteContext})
          EXPECT_EQ(bool(validateModule(*ast, *module, policy)),
                    version == "3.2" || suffix.empty());
      }
    }
  }
}

/** Owned payloads remain gated after source destruction and coherent mutation. */
TEST(DebugFileMetadata, RechecksOwnedMetadataAfterSourceDestruction) {
  std::optional<ResolvedModule> module;
  {
    PtxSyntaxParser parser(
        ".version 3.2\n.target sm_20\n.file 1 \"a.cu\", 0, 0\n.entry k() { "
        "ret; }\n");
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result);
    module.emplace(std::move(*result));
  }
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext})
    EXPECT_TRUE(validateModule(*module, policy));
  for (auto& region : module->header.regions)
    region.version = checker::PtxVersion{3, 1};
  for (auto& function : module->functions)
    function.source_version = checker::PtxVersion{3, 1};
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    const auto result = validateModule(*module, policy);
    ASSERT_FALSE(result);
    ASSERT_EQ(result.error().size(), 1u);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }
}

/** Replacing the entire symbol table cannot bypass the owned version gate. */
TEST(DebugFileMetadata, RechecksCoherentSymbolTableReplacement) {
  PtxSyntaxParser parser(".version 3.1\n.target sm_20\n.file 1 \"a.cu\"\n");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  const auto plain = module->symbols;
  {
    PtxSyntaxParser replacement(
        ".version 3.1\n.target sm_20\n.file 1 \"a.cu\", 0, 0\n");
    const auto replacement_ast = replacement.parseModule();
    ASSERT_TRUE(replacement_ast);
    module->symbols = binding::bindSymbols(*replacement_ast).table;
  }
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    const auto result = validateModule(*module, policy);
    ASSERT_FALSE(result);
    ASSERT_EQ(result.error().size(), 1u);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }
  module->symbols = plain;
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Missing or unrelated declarations cannot satisfy a retained file reference. */
TEST(DebugFileMetadata, RejectsUnresolvedBasicAndInlineReferences) {
  for (const std::string_view location :
       {".loc 2 1 0", ".loc 1 1 0, function_name name, inlined_at 2 1 0"}) {
    PtxSyntaxParser parser(std::string(header) +
                           ".file 1 \"a.cu\"\n.section .debug_str { name: .b8 "
                           "0; };\n.entry k() { " +
                           std::string(location) + " ret; }\n");
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Preserve a single raw size leaf without adding any instruction availability dependency. */
std::string fileSizeSource(
    std::string_view size,
    std::string_view prefix = ".version 9.3\n.target sm_90\n") {
  return std::string(prefix) + ".file 1 \"a.cu\", 0, " + std::string(size) +
         "\n.entry k() { .loc 1 1 0 ret; }\n";
}

/** Return a complete draft after its source, parser and AST have been destroyed. */
std::optional<ResolvedModule> fileSizeOwned(std::string source) {
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

/** Require the actual size-domain diagnostic rather than an unrelated payload failure. */
void expectFileSizeReject(const checker::CheckResult& result,
                          SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(
      result.error(), [&](const checker::CheckDiagnostic& diagnostic) {
        return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               diagnostic.range == range &&
               diagnostic.message ==
                   ".file file size must be a single unsigned 64-bit integer.";
      }));
}

/** Public AST edits and a complete rebind return an independently owning current table. */
std::optional<ResolvedModule> fileSizeReboundOwned(
    std::string_view word,
    std::string_view prefix = ".version 9.3\n.target sm_90\n") {
  PtxSyntaxParser parser(fileSizeSource("18446744073709551615", prefix));
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveModuleOnly(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  auto file = std::ranges::find_if(ast->items, [](const auto& item) {
    return std::holds_alternative<syntax_ast::AstFileDirective>(item);
  });
  if (file == ast->items.end())
    return std::nullopt;
  auto& declaration = std::get<syntax_ast::AstFileDirective>(*file);
  declaration.file_size->text = std::string(word);
  declaration.file_size->range.end.column =
      declaration.file_size->range.start.column + word.size();
  declaration.range.end = declaration.file_size->range.end;
  auto bound = binding::bindSymbols(*ast);
  EXPECT_TRUE(bound.diagnostics.empty());
  if (!bound.diagnostics.empty())
    return std::nullopt;
  module->symbols = std::move(bound.table);
  return std::move(*module);
}

/** Full-width metadata is an unsigned leaf, with overflow rejected without truncation. */
TEST(DebugFileMetadata, ChecksFullUnsignedSizeBoundary) {
  auto maximum = fileSizeOwned(fileSizeSource("18446744073709551615"));
  ASSERT_TRUE(maximum);
  ASSERT_TRUE(validateModule(*maximum));
  EXPECT_EQ(maximum->symbols.debugFiles()[0].file_size->text,
            "18446744073709551615");
  PtxSyntaxParser parser(fileSizeSource("18446744073709551616"));
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  auto overflow = fileSizeOwned(fileSizeSource("18446744073709551616"));
  ASSERT_TRUE(overflow);
  EXPECT_EQ(overflow->symbols.debugFiles()[0].file_size->text,
            "18446744073709551616");
  expectFileSizeReject(validateModule(*overflow),
                       overflow->symbols.debugFiles()[0].file_size->range);
  const auto copied = *overflow;
  expectFileSizeReject(validateModule(copied),
                       overflow->symbols.debugFiles()[0].file_size->range);
  ResolvedModule assigned;
  assigned = copied;
  expectFileSizeReject(validateModule(assigned),
                       overflow->symbols.debugFiles()[0].file_size->range);
  assigned.functions[0].body[0] = assigned.functions[0].body[0]->clone();
  expectFileSizeReject(validateModule(assigned),
                       overflow->symbols.debugFiles()[0].file_size->range);
  EXPECT_TRUE(validateModule(*maximum));
}

/** Public AST rebinding checks both datasets without a mutable debug-file accessor. */
TEST(DebugFileMetadata, RechecksCurrentAndSuppliedFileSizeIndependently) {
  auto legal = fileSizeOwned(fileSizeSource("18446744073709551615"));
  ASSERT_TRUE(legal);
  ASSERT_TRUE(validateModule(*legal));
  PtxSyntaxParser parser(fileSizeSource("18446744073709551615"));
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  // A separately parsed legal tree must not mask the current owning table.
  PtxSyntaxParser good_parser(fileSizeSource("18446744073709551615"));
  const auto good_ast = good_parser.parseModule();
  ASSERT_TRUE(good_ast && good_ast.diagnostics.empty());
  auto& file = std::get<syntax_ast::AstFileDirective>(ast->items[2]);
  ASSERT_TRUE(file.file_size);
  file.file_size->text = "18446744073709551616";
  auto rebound = binding::bindSymbols(*ast);
  ASSERT_TRUE(rebound.diagnostics.empty());
  auto bad_current = *legal;
  bad_current.symbols = std::move(rebound.table);
  EXPECT_EQ(bad_current.symbols.debugFiles()[0].file_size->range,
            file.file_size->range);
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    expectFileSizeReject(validateModule(bad_current, policy),
                         file.file_size->range);
    expectFileSizeReject(validateModule(*good_ast, bad_current, policy),
                         file.file_size->range);
    expectFileSizeReject(validateModule(*ast, *legal, policy),
                         file.file_size->range);
  }
  EXPECT_TRUE(validateModule(*legal));
}

/** One finite metadata word and its independently adopted capacity expectation. */
struct FileSizeCase {
  /** Exact single-token source spelling, retained without normalization. */
  std::string_view word;
  /** Whether the complete unsigned magnitude fits64bits. */
  bool accepted;
};

/** All adopted endpoints, radices and suffixes share the same intrinsic capacity boundary. */
TEST(DebugFileMetadata, EnumeratesUnsignedSizeWordsWithoutNarrowing) {
  const std::array<FileSizeCase, 60> cases{{
      {"0", true},
      {"0U", true},
      {"0u", true},
      {"00", true},
      {"00U", true},
      {"00u", true},
      {"0b0", true},
      {"0b0U", true},
      {"0b0u", true},
      {"0x0", true},
      {"0x0U", true},
      {"0x0u", true},
      {"9223372036854775807", true},
      {"9223372036854775807U", true},
      {"9223372036854775807u", true},
      {"0777777777777777777777", true},
      {"0777777777777777777777U", true},
      {"0777777777777777777777u", true},
      {"0b111111111111111111111111111111111111111111111111111111111111111",
       true},
      {"0b111111111111111111111111111111111111111111111111111111111111111U",
       true},
      {"0b111111111111111111111111111111111111111111111111111111111111111u",
       true},
      {"0x7fffffffffffffff", true},
      {"0x7fffffffffffffffU", true},
      {"0x7fffffffffffffffu", true},
      {"9223372036854775808", true},
      {"9223372036854775808U", true},
      {"9223372036854775808u", true},
      {"01000000000000000000000", true},
      {"01000000000000000000000U", true},
      {"01000000000000000000000u", true},
      {"0b1000000000000000000000000000000000000000000000000000000000000000",
       true},
      {"0b1000000000000000000000000000000000000000000000000000000000000000U",
       true},
      {"0b1000000000000000000000000000000000000000000000000000000000000000u",
       true},
      {"0x8000000000000000", true},
      {"0x8000000000000000U", true},
      {"0x8000000000000000u", true},
      {"18446744073709551615", true},
      {"18446744073709551615U", true},
      {"18446744073709551615u", true},
      {"01777777777777777777777", true},
      {"01777777777777777777777U", true},
      {"01777777777777777777777u", true},
      {"0b1111111111111111111111111111111111111111111111111111111111111111",
       true},
      {"0b1111111111111111111111111111111111111111111111111111111111111111U",
       true},
      {"0b1111111111111111111111111111111111111111111111111111111111111111u",
       true},
      {"0xffffffffffffffff", true},
      {"0xffffffffffffffffU", true},
      {"0xffffffffffffffffu", true},
      {"18446744073709551616", false},
      {"18446744073709551616U", false},
      {"18446744073709551616u", false},
      {"02000000000000000000000", false},
      {"02000000000000000000000U", false},
      {"02000000000000000000000u", false},
      {"0b10000000000000000000000000000000000000000000000000000000000000000",
       false},
      {"0b10000000000000000000000000000000000000000000000000000000000000000U",
       false},
      {"0b10000000000000000000000000000000000000000000000000000000000000000u",
       false},
      {"0x10000000000000000", false},
      {"0x10000000000000000U", false},
      {"0x10000000000000000u", false},
  }};
  for (const auto& item : cases) {
    SCOPED_TRACE(item.word);
    PtxSyntaxParser parser(fileSizeSource(item.word));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto& file = std::get<syntax_ast::AstFileDirective>(ast->items[2]);
    const auto strict = resolveAndValidateModule(*ast);
    EXPECT_EQ(bool(strict), item.accepted);
    auto owned = fileSizeOwned(fileSizeSource(item.word));
    ASSERT_TRUE(owned);
    ASSERT_TRUE(owned->symbols.debugFiles()[0].file_size);
    EXPECT_EQ(owned->symbols.debugFiles()[0].file_size->text, item.word);
    EXPECT_EQ(owned->symbols.debugFiles()[0].file_size->range,
              file.file_size->range);
    auto rebound = fileSizeReboundOwned(item.word);
    ASSERT_TRUE(rebound);
    EXPECT_EQ(rebound->symbols.debugFiles()[0].file_size->text, item.word);
    const auto copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    const auto table_copy = assigned.symbols;
    assigned.symbols = table_copy;
    EXPECT_EQ(assigned.symbols.debugFiles()[0].file_size->text, item.word);
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      if (item.accepted) {
        EXPECT_TRUE(validateModule(*owned, policy));
        EXPECT_TRUE(validateModule(*ast, *owned, policy));
        EXPECT_TRUE(validateModule(*rebound, policy));
        EXPECT_TRUE(validateModule(assigned, policy));
      } else {
        expectFileSizeReject(validateModule(*owned, policy),
                             file.file_size->range);
        expectFileSizeReject(validateModule(*ast, *owned, policy),
                             file.file_size->range);
        expectFileSizeReject(validateModule(*rebound, policy),
                             rebound->symbols.debugFiles()[0].file_size->range);
        expectFileSizeReject(validateModule(assigned, policy),
                             file.file_size->range);
      }
    }
  }
}

/** Invalid lexical roles use public AST construction independently of source parser rejection. */
TEST(DebugFileMetadata, RejectsInvalidSizeLeavesThroughPublicRebinding) {
  for (const auto word : {"-1", "+1", "1.0", "1+1", "WARP_SZ", "1Ujunk"}) {
    SCOPED_TRACE(word);
    PtxSyntaxParser parser(fileSizeSource(word));
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty()) {
      std::cout << "size-invalid " << word
                << " source=PARSE_REJECT source_owned=N_A\n";
    } else {
      const auto strict = resolveAndValidateModule(*ast);
      EXPECT_FALSE(strict);
      std::cout << "size-invalid " << word << " source=VALIDATION_REJECT\n";
    }
    auto module = fileSizeReboundOwned(word);
    ASSERT_TRUE(module);
    const auto range = module->symbols.debugFiles()[0].file_size->range;
    const auto copied = *module;
    ResolvedModule assigned;
    assigned = copied;
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      expectFileSizeReject(validateModule(*module, policy), range);
      expectFileSizeReject(validateModule(copied, policy), range);
      expectFileSizeReject(validateModule(assigned, policy), range);
    }
  }
}

/** Intrinsic capacity is independent of context and of another valid debug dataset. */
TEST(DebugFileMetadata, SeparatesSizeDomainContextAndNeutralReplacement) {
  for (const auto prefix :
       {"", ".version 3.1\n", ".version 3.2\n.target sm_20\n"}) {
    auto bad = fileSizeReboundOwned("18446744073709551616", prefix);
    ASSERT_TRUE(bad);
    const auto range = bad->symbols.debugFiles()[0].file_size->range;
    PtxSyntaxParser parser(fileSizeSource("0", prefix));
    const auto good_ast = parser.parseModule();
    ASSERT_TRUE(good_ast && good_ast.diagnostics.empty());
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      expectFileSizeReject(validateModule(*bad, policy), range);
      expectFileSizeReject(validateModule(*good_ast, *bad, policy), range);
    }
  }
  auto maximum = fileSizeOwned(fileSizeSource("18446744073709551615"));
  ASSERT_TRUE(maximum);
  ASSERT_TRUE(validateModule(*maximum));
  auto different_source = fileSizeSource("0");
  different_source.replace(different_source.find("a.cu"), 4, "b.cu");
  PtxSyntaxParser different_parser(different_source);
  const auto different_ast = different_parser.parseModule();
  ASSERT_TRUE(different_ast && different_ast.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*different_ast));
  EXPECT_TRUE(validateModule(*different_ast, *maximum));
  EXPECT_TRUE(validateModule(*maximum));
  const auto original = *maximum;
  auto changed = fileSizeReboundOwned("0");
  ASSERT_TRUE(changed);
  maximum->symbols = changed->symbols;
  EXPECT_TRUE(validateModule(*maximum));
  EXPECT_EQ(original.symbols.debugFiles()[0].file_size->text,
            "18446744073709551615");
  EXPECT_TRUE(validateModule(original));
}

/** Structural source-range and optional-pair invariants remain distinct from capacity. */
TEST(DebugFileMetadata, RetainsSizeRangeAndPairingControls) {
  for (const bool missing_size : {false, true}) {
    std::optional<ResolvedModule> module;
    {
      PtxSyntaxParser parser(fileSizeSource("0"));
      auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto resolved = resolveModuleOnly(*ast);
      ASSERT_TRUE(resolved);
      auto& file = std::get<syntax_ast::AstFileDirective>(ast->items[2]);
      if (missing_size)
        file.file_size.reset();
      else
        file.file_size->range.start = {0, 0};
      auto rebound = binding::bindSymbols(*ast);
      ASSERT_TRUE(rebound.diagnostics.empty());
      resolved->symbols = std::move(rebound.table);
      module.emplace(std::move(*resolved));
    }
    const auto result = validateModule(*module);
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
      return error.message ==
             "Owned debug file payload has inconsistent source boundaries.";
    }));
    EXPECT_FALSE(std::ranges::any_of(result.error(), [](const auto& error) {
      return error.message ==
             ".file file size must be a single unsigned 64-bit integer.";
    }));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
