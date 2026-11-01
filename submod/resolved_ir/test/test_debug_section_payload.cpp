#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <string>
#include <string_view>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build complete modern source without adding target-debug presence policy. */
std::string section_source(std::string_view payload) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n.section .debug_info "
         "{" +
         std::string(payload) + "}\n.entry kernel(){ret;}";
}
/** Legal extra section entries shift executable source lines without changing its identity. */
std::string extended_section_source() {
  return section_source(
      ".b8 255\n.b16 12\n.b32 .debug_abbrev\n.b64 18446744073709551615");
}
/** Resolve section source after its parser, AST and input storage are destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> section_owned(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}
TEST(DebugSectionPayload, FreshValidBoundaryControls) {
  for (const auto payload : {".b8 255", ".b8 -128"}) {
    SCOPED_TRACE(payload);
    const auto ast = test_helpers::parseModule(section_source(payload));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    ASSERT_TRUE(resolveAndValidateModule(*ast));
    auto owned = section_owned(section_source(payload));
    ASSERT_TRUE(owned);
    EXPECT_TRUE(validateModule(*owned));
    ResolvedModule copied(*owned), assigned;
    assigned = copied;
    EXPECT_TRUE(validateModule(copied));
    EXPECT_TRUE(validateModule(assigned));
  }
}
TEST(DebugSectionPayload, RejectsOriginalRangeAndUnknownProduction) {
  const auto range_ast = test_helpers::parseModule(section_source(".b8 256"));
  ASSERT_MODULE_PARSE_SUCCEEDS(range_ast);
  EXPECT_FALSE(resolveModuleOnly(*range_ast));
  EXPECT_FALSE(resolveAndValidateModule(*range_ast));
  const auto unknown = test_helpers::parseModule(section_source(".unknown 0"));
  EXPECT_FALSE(unknown.diagnostics.empty());
}
/** Recheck source-dead payloads through every module/function value-owner path. */
void section_lifetime(const ResolvedModule& module, bool valid = true) {
  EXPECT_EQ(bool(validateModule(module)), valid);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  if (!copied.functions.empty()) {
    ResolvedFunction function(copied.functions.front()), function_assigned;
    function_assigned = function;
    assigned.functions.front() = function_assigned;
    if (!function.body.empty())
      assigned.functions.front().body.front() = function.body.front()->clone();
  }
  EXPECT_EQ(bool(validateModule(copied)), valid);
  EXPECT_EQ(bool(validateModule(assigned)), valid);
  ResolvedModule moved(std::move(assigned));
  ResolvedModule move_assigned;
  move_assigned = std::move(moved);
  EXPECT_EQ(bool(validateModule(move_assigned)), valid);
  std::swap(copied, move_assigned);
  EXPECT_EQ(bool(validateModule(copied)), valid);
}
/** Borrow the first integer leaf without retaining a temporary payload view. */
syntax_ast::AstSectionInteger& first_integer(ResolvedModule& module) {
  auto& data = std::get<syntax_ast::AstSectionData>(
      module.sections.front().payload.front());
  return std::get<std::vector<syntax_ast::AstSectionInteger>>(data.value)
      .front();
}
TEST(DebugSectionPayload, CoversAllOriginalIntegerBoundariesWithoutTruncation) {
  /** One original mathematical interval boundary, independent of encoded bits. */
  struct Boundary {
    /** Nominal data width in bits. */
    unsigned width;
    /** Original signed source spelling. */
    std::string_view text;
    /** Whether the original value is inside the declared interval. */
    bool valid;
  };
  constexpr std::array cases{Boundary{8, "-129", false},
                             Boundary{8, "-128", true},
                             Boundary{8, "-1", true},
                             Boundary{8, "0", true},
                             Boundary{8, "255", true},
                             Boundary{8, "256", false},
                             Boundary{16, "-32769", false},
                             Boundary{16, "-32768", true},
                             Boundary{16, "-1", true},
                             Boundary{16, "0", true},
                             Boundary{16, "65535", true},
                             Boundary{16, "65536", false},
                             Boundary{32, "-2147483649", false},
                             Boundary{32, "-2147483648", true},
                             Boundary{32, "-1", true},
                             Boundary{32, "0", true},
                             Boundary{32, "4294967295", true},
                             Boundary{32, "4294967296", false},
                             Boundary{64, "-9223372036854775809", false},
                             Boundary{64, "-9223372036854775808", true},
                             Boundary{64, "-1", true},
                             Boundary{64, "0", true},
                             Boundary{64, "18446744073709551615", true},
                             Boundary{64, "18446744073709551616", false}};
  for (const auto& item : cases) {
    SCOPED_TRACE(std::to_string(item.width) + ":" + std::string(item.text));
    const auto ast = test_helpers::parseModule(section_source(
        ".b" + std::to_string(item.width) + " " + std::string(item.text)));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_EQ(bool(resolveAndValidateModule(*ast)), item.valid);
    auto owned = section_owned(section_source(
        ".b" + std::to_string(item.width) + " " + std::string(item.text)));
    EXPECT_EQ(bool(owned), item.valid);
    if (owned) {
      const auto& leaf = first_integer(*owned);
      EXPECT_EQ((leaf.sign ? leaf.sign->text : "") + leaf.magnitude.text,
                item.text);
      section_lifetime(*owned);
    }
    if (!item.valid) {
      const auto control =
          std::ranges::find_if(cases, [&](const auto& candidate) {
            return candidate.width == item.width && candidate.valid &&
                   candidate.text.starts_with('-') ==
                       item.text.starts_with('-') &&
                   candidate.text.size() == item.text.size();
          });
      ASSERT_NE(control, cases.end());
      auto mutated =
          section_owned(section_source(".b" + std::to_string(item.width) + " " +
                                       std::string(control->text)));
      ASSERT_TRUE(mutated);
      ASSERT_TRUE(validateModule(*mutated));
      auto& leaf = first_integer(*mutated);
      const auto original_range = leaf.range;
      leaf.magnitude.text =
          item.text.substr(item.text.starts_with('-') ? 1 : 0);
      const auto invalid = validateModule(*mutated);
      ASSERT_FALSE(invalid);
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
            return diagnostic.range == original_range &&
                   diagnostic.message.find("integer") != std::string::npos;
          }));
      section_lifetime(*mutated, false);
      const auto supplied = test_helpers::parseModule(
          section_source(".b" + std::to_string(item.width) + " " +
                         std::string(control->text)));
      ASSERT_MODULE_PARSE_SUCCEEDS(supplied);
      ASSERT_TRUE(resolveAndValidateModule(*supplied));
      const auto invalid_with_source = validateModule(*supplied, *mutated);
      ASSERT_FALSE(invalid_with_source);
      EXPECT_TRUE(std::ranges::any_of(
          invalid_with_source.error(), [&](const auto& diagnostic) {
            return diagnostic.range == original_range &&
                   diagnostic.message.find("integer") != std::string::npos;
          }));
    }
  }
  for (const auto payload :
       {".b8 0xffU,0377,0b11111111", ".b64 18446744073709551615U", ".b8 -0U"}) {
    auto owned = section_owned(section_source(payload));
    ASSERT_TRUE(owned);
    section_lifetime(*owned);
  }
  for (const auto payload : {".b16 -65535", ".b8 -129U", ".b8 256U"}) {
    const auto ast = test_helpers::parseModule(section_source(payload));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
TEST(DebugSectionPayload,
     RejectsOnlyUnsupportedStructuralProductionsLosslessly) {
  for (const auto payload :
       {".unknown 0", ".b8 \"abc\"", ".ascii \"abc\"", ".b8 {0}", ".b8 1,,2",
        ".b8 1,", ".b32 1,label", ".b32 label+other", ".b32 label*4",
        ".b8 label", ".b16 label"}) {
    SCOPED_TRACE(payload);
    const auto source = section_source(payload);
    PtxCstParser parser(source);
    const auto cst = parser.parseModule();
    ASSERT_TRUE(cst);
    EXPECT_FALSE(cst.diagnostics.empty());
    EXPECT_EQ(cst->sourceText(), source);
    ASSERT_TRUE(std::holds_alternative<syntax_cst::CstSectionDirective>(
        cst->module()->items[3]));
    const auto& section =
        std::get<syntax_cst::CstSectionDirective>(cst->module()->items[3]);
    EXPECT_FALSE(section.payload_tokens.empty());
  }
}
TEST(DebugSectionPayload, ChecksSignedAddendEndpointsAndBothWrittenSigns) {
  for (const auto payload :
       {".b32 .debug_info+2147483647", ".b32 .debug_info+-2147483648",
        ".b32 .debug_info-2147483648", ".b32 .debug_info--2147483647",
        ".b64 .debug_info+9223372036854775807",
        ".b64 .debug_info+-9223372036854775808",
        ".b64 .debug_info-9223372036854775808",
        ".b64 .debug_info--9223372036854775807", ".b32 unresolved_external",
        ".b64 .debug_str"}) {
    SCOPED_TRACE(payload);
    auto owned = section_owned(section_source(payload));
    ASSERT_TRUE(owned);
    section_lifetime(*owned);
  }
  for (const auto payload :
       {".b32 .debug_info+2147483648", ".b32 .debug_info+-2147483649",
        ".b32 .debug_info--2147483648", ".b64 .debug_info+9223372036854775808",
        ".b64 .debug_info+-9223372036854775809",
        ".b64 .debug_info--9223372036854775808",
        ".b64 .debug_info+18446744073709551616"}) {
    SCOPED_TRACE(payload);
    const auto ast = test_helpers::parseModule(section_source(payload));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
TEST(DebugSectionPayload, JoinsForwardLabelsAcrossSameNameFragmentsOnly) {
  const std::string header = ".version 9.3\n.target sm_90\n.address_size 64\n";
  const auto valid =
      header +
      ".section .debug_info {.b32 end-begin begin: .b8 0}"
      ".section .debug_str {name: .b8 110,0}"
      ".section .debug_info {end: .b64 .debug_info-begin .b64 .debug_str}"
      ".file 1 \"a.cu\"\n.entry kernel(){.loc 1 1 0 .loc 1 1 0, function_name "
      "name, "
      "inlined_at 1 1 0 ret;}";
  auto owned = section_owned(valid);
  ASSERT_TRUE(owned);
  ASSERT_EQ(owned->sections.size(), 3u);
  section_lifetime(*owned);
  EXPECT_EQ(owned->sections[0].name.text, owned->sections[2].name.text);
  EXPECT_EQ(std::get<syntax_ast::AstSectionLabelDefinition>(
                owned->sections[1].payload.front())
                .name.text,
            "name");
  for (const auto sections :
       {".section a {x: .b8 0}.section b {y: .b32 x-y}",
        ".section a {.b32 unknown-other}", ".section a {x:}.section a {x:}",
        ".section a {x:}.section b {x: .b32 x-x}"}) {
    SCOPED_TRACE(sections);
    const auto ast = test_helpers::parseModule(header + sections);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  auto& difference = std::get<syntax_ast::AstSectionDifference>(
      std::get<syntax_ast::AstSectionData>(owned->sections[0].payload[0])
          .value);
  const auto saved = difference.left.text;
  difference.left.text = "name";
  section_lifetime(*owned, false);
  difference.left.text = saved;
  section_lifetime(*owned);
  owned->sections[1].payload.clear();
  section_lifetime(*owned, false);
}
TEST(DebugSectionPayload, RechecksCurrentAndReplacementDataIndependently) {
  const auto original = test_helpers::parseModule(section_source(".b8 255"));
  ASSERT_MODULE_PARSE_SUCCEEDS(original);
  auto owned = section_owned(section_source(".b8 255"));
  ASSERT_TRUE(owned);
  auto replacement = test_helpers::parseModule(section_source(".b8 254"));
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  EXPECT_TRUE(validateModule(*replacement, *owned));
  auto extended = test_helpers::parseModule(extended_section_source());
  ASSERT_MODULE_PARSE_SUCCEEDS(extended);
  ASSERT_TRUE(resolveAndValidateModule(*extended));
  EXPECT_TRUE(validateModule(*extended, *owned));

  first_integer(*owned).magnitude.text = "256";
  EXPECT_FALSE(validateModule(*replacement, *owned));
  section_lifetime(*owned, false);
  first_integer(*owned).magnitude.text = "254";
  EXPECT_TRUE(validateModule(*original, *owned));
  section_lifetime(*owned);
  auto& replacement_leaf =
      std::get<std::vector<syntax_ast::AstSectionInteger>>(
          std::get<syntax_ast::AstSectionData>(
              std::get<syntax_ast::AstSectionDirective>(replacement->items[3])
                  .payload.front())
              .value)
          .front();
  replacement_leaf.magnitude.text = "256";
  EXPECT_FALSE(validateModule(*replacement, *owned));
  const auto saved = first_integer(*owned);
  first_integer(*owned).sign = syntax_ast::AstSyntax{"?", saved.range};
  section_lifetime(*owned, false);
  first_integer(*owned) = saved;
  first_integer(*owned).magnitude.range.end.column = 0;
  section_lifetime(*owned, false);
  first_integer(*owned) = saved;
  auto& data = std::get<syntax_ast::AstSectionData>(
      owned->sections.front().payload.front());
  data.width = static_cast<syntax_ast::AstSectionWidth>(7);
  section_lifetime(*owned, false);
  data.width = syntax_ast::AstSectionWidth::B8;
  section_lifetime(*owned);
}
TEST(DebugSectionPayload,
     AppliesEachKnownVersionGateWithoutInventingMissingContext) {
  /** Legal source-header pairs isolating one feature threshold. */
  struct Gate {
    std::string_view below, at, sm, payload;
  };
  constexpr std::array cases{Gate{"1.5", "2.0", "sm_13", ".b8 0"},
                             Gate{"3.1", "3.2", "sm_20", ".b32 .debug_info+1"},
                             Gate{"5.0", "6.0", "sm_20", ".b16 0"},
                             Gate{"7.1", "7.2", "sm_70", "label: .b8 0"},
                             Gate{"7.4", "7.5", "sm_70", "a: b: .b32 a-b"},
                             Gate{"7.4", "7.5", "sm_70", ".b8 -1"}};
  for (const auto& item : cases) {
    for (const bool allowed : {false, true}) {
      const std::string source =
          ".version " + std::string(allowed ? item.at : item.below) +
          "\n.target " + std::string(item.sm) + "\n.section .debug_info {" +
          std::string(item.payload) + "}";
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto result = resolveAndValidateModule(*ast);
      EXPECT_EQ(bool(result), allowed) << source;
      if (!result)
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
          return d.checker_kind ==
                 checker::CheckDiagnosticKind::UnsupportedPtxVersion;
        }));
      const auto control = test_helpers::parseModule(
          ".version " + std::string(allowed ? item.at : item.below) +
          "\n.target " + std::string(item.sm));
      ASSERT_MODULE_PARSE_SUCCEEDS(control);
      EXPECT_TRUE(resolveAndValidateModule(*control));
    }
  }
  const auto zero = test_helpers::parseModule(
      ".version 7.4\n.target sm_70\n.section .debug_info {.b8 -0}");
  ASSERT_MODULE_PARSE_SUCCEEDS(zero);
  EXPECT_TRUE(resolveAndValidateModule(*zero));
  const auto missing =
      test_helpers::parseModule(".section .debug_info {.b8 -1}");
  ASSERT_MODULE_PARSE_SUCCEEDS(missing);
  auto owned = resolveModuleOnly(*missing);
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(validateModule(*missing, *owned,
                             ModuleValidationPolicy::AvailableContext));
  const auto owned_strict =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  const auto source_strict = validateModule(
      *missing, *owned, ModuleValidationPolicy::RequireCompleteContext);
  for (const auto* result : {&owned_strict, &source_strict}) {
    ASSERT_FALSE(*result);
    EXPECT_TRUE(
        std::ranges::any_of(result->error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
    EXPECT_FALSE(
        std::ranges::any_of(result->error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::UnsupportedPtxVersion;
        }));
  }
}
/** Positive ranges stay local; wholly unknown section ranges retain their deferral. */
TEST(DebugSectionPayload, SeparatesCurrentAndReplacementOuterRangeOwnership) {
  const auto source = section_source(".b8 255");
  auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto owned = section_owned(source);
  ASSERT_TRUE(owned);
  auto extended = test_helpers::parseModule(extended_section_source());
  ASSERT_MODULE_PARSE_SUCCEEDS(extended);
  ASSERT_TRUE(resolveAndValidateModule(*extended));
  EXPECT_TRUE(validateModule(*extended, *owned));
  const auto saved = owned->sections.front().range;
  owned->sections.front().range.end = owned->range.end;
  ++owned->sections.front().range.end.column;
  const auto invalid = validateModule(*extended, *owned);
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
    return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
           diagnostic.message.find("outside its owning module") !=
               std::string::npos;
  }));
  section_lifetime(*owned, false);
  owned->sections.front().range = saved;
  auto& replacement = std::get<syntax_ast::AstSectionDirective>(ast->items[3]);
  replacement.range.end = ast->range.end;
  ++replacement.range.end.column;
  auto owned_extended = section_owned(extended_section_source());
  ASSERT_TRUE(owned_extended);
  const auto invalid_replacement = validateModule(*ast, *owned_extended);
  ASSERT_FALSE(invalid_replacement);
  EXPECT_TRUE(std::ranges::any_of(
      invalid_replacement.error(), [](const auto& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
               diagnostic.message.find("outside its owning module") !=
                   std::string::npos;
      }));
  EXPECT_TRUE(validateModule(*owned_extended));
  replacement.range = saved;
  auto& section = owned->sections.front();
  section.range = {};
  section.name.range = {};
  auto& data = std::get<syntax_ast::AstSectionData>(section.payload.front());
  data.range = {};
  data.type.range = {};
  auto& leaf =
      std::get<std::vector<syntax_ast::AstSectionInteger>>(data.value).front();
  leaf.range = {};
  leaf.magnitude.range = {};
  section_lifetime(*owned);
  EXPECT_TRUE(validateModule(*ast, *owned));
  auto ordered = section_owned(
      ".version 9.3\n.target sm_90\n.address_size 64\n.section a {.b8 "
      "0}.section b {.b8 1}.entry kernel(){ret;}");
  ASSERT_TRUE(ordered);
  ResolvedDebugSection unknown;
  unknown.name.text = "unknown";
  ordered->sections.insert(ordered->sections.begin() + 1, unknown);
  EXPECT_TRUE(validateModule(*ordered));
  std::swap(ordered->sections.front(), ordered->sections.back());
  const auto reordered = validateModule(*ordered);
  ASSERT_FALSE(reordered);
  EXPECT_TRUE(
      std::ranges::any_of(reordered.error(), [](const auto& diagnostic) {
        return diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
               diagnostic.message.find("fragment order") != std::string::npos;
      }));
  section_lifetime(*ordered, false);
}
TEST(DebugSectionPayload,
     RechecksVersionRetargetingAndReplacementLabelBindings) {
  const std::string source =
      ".version 9.3\n.target sm_70\n.section .debug_info {.b8 -1}";
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto owned = section_owned(source);
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  for (auto& region : owned->header.regions)
    region.version = checker::PtxVersion{7, 4};
  const auto retargeted = validateModule(*owned);
  ASSERT_FALSE(retargeted);
  EXPECT_TRUE(std::ranges::any_of(retargeted.error(), [](const auto& d) {
    return d.kind == checker::CheckDiagnosticKind::UnsupportedPtxVersion;
  }));
  for (auto& region : owned->header.regions)
    region.version = checker::PtxVersion{9, 3};
  EXPECT_TRUE(validateModule(*owned));
  const auto lower = test_helpers::parseModule(
      ".version 7.4\n.target sm_70\n.section .debug_info {.b8 1}");
  ASSERT_MODULE_PARSE_SUCCEEDS(lower);
  EXPECT_TRUE(resolveAndValidateModule(*lower));
  EXPECT_FALSE(validateModule(*lower, *owned));
  const std::string debug =
      ".version 9.3\n.target sm_90\n.file 1 \"a.cu\"\n.global .b8 name;"
      ".section .debug_str {name: .b8 0}.entry kernel(){.loc 1 1 0 .loc 1 1 0, "
      "function_name name, inlined_at 1 1 0 ret;}";
  auto labelled = section_owned(debug);
  ASSERT_TRUE(labelled);
  section_lifetime(*labelled);
  auto replacement = test_helpers::parseModule(debug);
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  auto& label = std::get<syntax_ast::AstSectionLabelDefinition>(
      std::get<syntax_ast::AstSectionDirective>(replacement->items[4])
          .payload.front());
  label.name.text = "gone";
  EXPECT_FALSE(validateModule(*replacement, *labelled));
  std::get<syntax_ast::AstSectionLabelDefinition>(
      labelled->sections.front().payload.front())
      .name.text = "gone";
  section_lifetime(*labelled, false);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
