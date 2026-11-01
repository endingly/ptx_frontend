#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build modern matched source; section bytes are structural witnesses, not decoded DWARF. */
std::string debug_source(bool debug, std::string_view sections = {}) {
  return ".version 9.3\n.target sm_90" + std::string(debug ? ", debug" : "") +
         "\n.address_size 64\n" + std::string(sections) +
         "\n.entry kernel(){ret;}";
}
/** The known paired witness establishes source structure, not universal recognition. */
constexpr std::string_view debug_pair =
    ".section .debug_abbrev {.b8 0,0}\n.section .debug_info {.b32 0}";
/** Return a draft after all original source/parser/AST storage has been destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> debug_owned(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}
/** Validate independent owning module/function copies and instruction clones. */
void debug_lifetime(const ResolvedModule& module, bool expected) {
  EXPECT_EQ(bool(validateModule(module)), expected);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  ResolvedFunction function(copied.functions.front()), function_assigned;
  function_assigned = function;
  assigned.functions.front() = function_assigned;
  assigned.functions.front().body.front() = function.body.front()->clone();
  EXPECT_EQ(bool(validateModule(copied)), expected);
  EXPECT_EQ(bool(validateModule(assigned)), expected);
}
TEST(TargetDebugPresence, FreshMatchedControlsRemainStructurallyValid) {
  for (const bool debug : {false, true}) {
    const auto source = debug_source(debug, debug ? debug_pair : "");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto owned = debug_owned(source);
    ASSERT_TRUE(owned);
    debug_lifetime(*owned, true);
  }
}
TEST(TargetDebugPresence, RejectsDefiniteAbsenceAfterActualAstDeath) {
  const auto ast = test_helpers::parseModule(debug_source(true));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  auto owned = debug_owned(debug_source(true));
  ASSERT_TRUE(owned);
  EXPECT_FALSE(validateModule(*ast, *owned));
  debug_lifetime(*owned, false);
}
TEST(TargetDebugPresence, NeitherDatasetCanHideTheOtherDefiniteAbsence) {
  auto absent = debug_owned(debug_source(true));
  auto structural = debug_owned(debug_source(true, debug_pair));
  ASSERT_TRUE(absent);
  ASSERT_TRUE(structural);
  const auto supplied_structural =
      test_helpers::parseModule(debug_source(true, debug_pair));
  const auto supplied_absent = test_helpers::parseModule(debug_source(true));
  ASSERT_MODULE_PARSE_SUCCEEDS(supplied_structural);
  ASSERT_MODULE_PARSE_SUCCEEDS(supplied_absent);
  EXPECT_FALSE(validateModule(*supplied_structural, *absent));
  EXPECT_FALSE(validateModule(*supplied_absent, *structural));
}
/** Count the specific absence violation, independently of version/context/grammar failures. */
size_t absence_count(const checker::CheckResult& result) {
  if (result)
    return 0;
  return std::ranges::count_if(result.error(), [](const auto& diagnostic) {
    return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
           diagnostic.message.find("this dataset contains no sections") !=
               std::string::npos;
  });
}
TEST(TargetDebugPresence,
     KeepsRecognitionBoundaryExplicitAcrossStructuralRows) {
  /** These are syntax partitions, not a DWARF recognition algorithm. */
  struct Row {
    std::string_view name;
    std::string_view sections;
    /** Only zero records establish determinate absence. */
    bool zero;
  };
  constexpr std::array rows{
      Row{"zero", "", true},
      Row{"arbitrary", ".section arbitrary {.b8 0}", false},
      Row{"empty_debug_info", ".section .debug_info {}", false},
      Row{"label_only", ".section .debug_info {label:}", false},
      Row{"typed_debug_info", ".section .debug_info {.b8 0}", false},
      Row{"typed_debug_str", ".section .debug_str {name: .b8 0}", false},
      Row{"typed_debug_line", ".section .debug_line {.b8 0}", false},
      Row{"paired_witness", debug_pair, false}};
  for (const bool debug : {false, true})
    for (const auto& row : rows) {
      SCOPED_TRACE(std::string(row.name) + " debug=" + std::to_string(debug));
      const auto ast =
          test_helpers::parseModule(debug_source(debug, row.sections));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto owned = debug_owned(debug_source(debug, row.sections));
      ASSERT_TRUE(owned);
      const bool structural_valid = !(debug && row.zero);
      EXPECT_EQ(bool(resolveAndValidateModule(*ast)), structural_valid);
      const auto current = validateModule(*owned);
      EXPECT_EQ(bool(current), structural_valid);
      EXPECT_EQ(absence_count(current), debug && row.zero ? 1u : 0u);
      EXPECT_EQ(bool(validateModule(*ast, *owned)), structural_valid);
      debug_lifetime(*owned, structural_valid);
      std::cout << "BOUNDARY row=" << row.name << " debug=" << debug
                << " absence="
                << (debug && row.zero ? "DETERMINATE_FAIL"
                                      : "NO_DETERMINATE_ABSENCE")
                << " recognition="
                << (debug && !row.zero ? "REFERENCE_BOUNDARY_NOT_CERTIFIED"
                                       : "NOT_APPLICABLE")
                << '\n';
    }
}
TEST(TargetDebugPresence,
     RejectsCurrentOptionAndSectionEditsWithoutSourceHistoryJoins) {
  const auto off = test_helpers::parseModule(debug_source(false));
  ASSERT_MODULE_PARSE_SUCCEEDS(off);
  auto owned = debug_owned(debug_source(false));
  ASSERT_TRUE(owned);
  debug_lifetime(*owned, true);
  auto& options = owned->header.regions.back().target_options;
  options.push_back("debug");
  EXPECT_EQ(absence_count(validateModule(*owned)), 1u);
  EXPECT_EQ(absence_count(validateModule(*off, *owned)), 1u);
  debug_lifetime(*owned, false);
  options.pop_back();
  debug_lifetime(*owned, true);
  auto with_sections = debug_owned(debug_source(true, debug_pair));
  ASSERT_TRUE(with_sections);
  const auto retained = with_sections->sections;
  with_sections->sections.clear();
  EXPECT_EQ(absence_count(validateModule(*with_sections)), 1u);
  debug_lifetime(*with_sections, false);
  const auto supplied =
      test_helpers::parseModule(debug_source(true, debug_pair));
  ASSERT_MODULE_PARSE_SUCCEEDS(supplied);
  EXPECT_EQ(absence_count(validateModule(*supplied, *with_sections)), 1u);
  with_sections->sections = retained;
  EXPECT_TRUE(validateModule(*supplied, *with_sections));
  debug_lifetime(*with_sections, true);
}
TEST(TargetDebugPresence,
     PreservesTypedPayloadFailuresAndNeutralValidReplacements) {
  const auto valid = test_helpers::parseModule(
      debug_source(true, ".section .debug_info {.b8 255}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(valid);
  auto owned =
      debug_owned(debug_source(true, ".section .debug_info {.b8 255}"));
  ASSERT_TRUE(owned);
  const auto different =
      test_helpers::parseModule(debug_source(true, debug_pair));
  ASSERT_MODULE_PARSE_SUCCEEDS(different);
  EXPECT_TRUE(validateModule(*different, *owned));
  auto& leaf = std::get<std::vector<syntax_ast::AstSectionInteger>>(
                   std::get<syntax_ast::AstSectionData>(
                       owned->sections.front().payload.front())
                       .value)
                   .front();
  leaf.magnitude.text = "256";
  EXPECT_FALSE(validateModule(*different, *owned));
  debug_lifetime(*owned, false);
  leaf.magnitude.text = "255";
  debug_lifetime(*owned, true);
  auto invalid = test_helpers::parseModule(
      debug_source(true, ".section .debug_info {.b8 256}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
  EXPECT_FALSE(resolveModuleOnly(*invalid));
  EXPECT_FALSE(validateModule(*invalid, *owned));
  const auto bad_grammar = test_helpers::parseModule(
      debug_source(true, ".section .debug_info {.unknown 0}"));
  EXPECT_FALSE(bad_grammar.diagnostics.empty());
}
TEST(TargetDebugPresence,
     KeepsVersionAndMissingContextSeparateFromKnownAbsence) {
  for (const auto version : {"2.3", "3.0"}) {
    const std::string source = ".version " + std::string(version) +
                               "\n.target sm_20, debug\n.address_size "
                               "64\n.section .debug_info {.b8 0}";
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owned = debug_owned(source);
    ASSERT_TRUE(owned);
    const auto result = validateModule(*owned);
    EXPECT_EQ(bool(result), std::string_view(version) == "3.0");
    EXPECT_EQ(absence_count(result), 0u);
    if (!result)
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::UnsupportedAvailability;
      }));
  }
  const auto missing = test_helpers::parseModule(".target sm_90, debug\n");
  ASSERT_MODULE_PARSE_SUCCEEDS(missing);
  auto owned = resolveModuleOnly(*missing);
  ASSERT_TRUE(owned);
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    EXPECT_EQ(absence_count(validateModule(*owned, policy)), 1u);
    EXPECT_EQ(absence_count(validateModule(*missing, *owned, policy)), 2u);
  }
  const auto result =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
    return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
  }));
}
TEST(TargetDebugPresence,
     UsesWholeFileSectionsAcrossLaterAndRepeatedTargetRegions) {
  for (const auto targets :
       {".target sm_90, debug\n.func first(){ret;}\n.target sm_90\n",
        ".target sm_90\n.func first(){ret;}\n.target sm_90, debug\n",
        ".target sm_90, debug\n.func first(){ret;}\n.target sm_90, debug\n"})
    for (const bool before : {false, true}) {
      const std::string header =
          ".version 9.3\n.target sm_90\n.address_size 64\n";
      const auto source =
          header + (before ? std::string(debug_pair) : "") + "\n" + targets +
          (before ? "" : std::string(debug_pair)) + "\n.entry kernel(){ret;}";
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
      auto owned = debug_owned(source);
      ASSERT_TRUE(owned);
      EXPECT_TRUE(validateModule(*ast, *owned));
      // Both functions are independent of the file-wide metadata requirement.
      EXPECT_TRUE(validateModule(*owned));
      owned->sections.clear();
      EXPECT_EQ(absence_count(validateModule(*owned)), 1u);
    }
}
TEST(TargetDebugPresence,
     FileLocationAndRetainedDebugSymbolsDoNotSupplySections) {
  const auto basic = test_helpers::parseModule(
      ".version 9.3\n.target sm_90, debug\n.file 1 \"a.cu\"\n.entry "
      "kernel(){.loc 1 1 0 ret;}");
  ASSERT_MODULE_PARSE_SUCCEEDS(basic);
  auto bare = resolveModuleOnly(*basic);
  ASSERT_TRUE(bare);
  EXPECT_EQ(absence_count(validateModule(*bare)), 1u);
  const std::string source =
      ".version 9.3\n.target sm_90, debug\n.file 1 \"a.cu\"\n.section "
      ".debug_str {name: .b8 0}\n.entry kernel(){.loc 1 1 0, function_name "
      "name, inlined_at 1 1 0 ret;}";
  auto named = debug_owned(source);
  ASSERT_TRUE(named);
  EXPECT_TRUE(validateModule(*named));
  named->sections.clear();
  EXPECT_EQ(absence_count(validateModule(*named)), 1u);
  EXPECT_FALSE(named->symbols.symbols().empty());
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
