#include <gtest/gtest.h>

#include <algorithm>
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
              references == 0 ? first.symbol_id : second.symbol_id);
    ++references;
  }
  EXPECT_EQ(references, 2u);
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
