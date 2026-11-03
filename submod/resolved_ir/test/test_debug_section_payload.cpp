#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <string>
#include <string_view>
#include <tuple>

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

/** Build complete source with independent prior .loc and typed section data. */
std::string function_name_source(std::string_view sections,
                                 std::string_view reference = "name") {
  return ".version 9.3\n.target sm_90\n.file 1 \"a.cu\"\n" +
         std::string(sections) +
         "\n.entry kernel(){.loc 1 2 0\n"
         ".loc 1 1 0, function_name " +
         std::string(reference) + ", inlined_at 1 2 0\nret;}";
}

/** Identify the intrinsic known-byte reason, independently of context diagnostics. */
bool has_string_error(
    const checker::CheckDiagnostics& errors, std::string_view reason = "",
    std::optional<SourceRange> expected_range = std::nullopt) {
  return std::any_of(errors.begin(), errors.end(), [&](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::RuleViolation &&
           error.message.starts_with("Debug function-name") &&
           error.message.find(reason) != std::string::npos &&
           error.range != SourceRange{} &&
           (!expected_range || error.range == *expected_range);
  });
}

/** Select the actual inline label or byte-offset range from this dataset's own table. */
std::optional<SourceRange> function_name_range(
    const binding::SymbolTable& symbols) {
  for (const auto& location : symbols.debugLocations()) {
    if (!location.inline_context)
      continue;
    const auto& context = *location.inline_context;
    return context.function_name_offset ? context.function_name_offset->range
                                        : context.function_name_label.range;
  }
  return std::nullopt;
}

/** Locate a current numeric leaf in an actual owner, without replaying its old syntax. */
syntax_ast::AstSectionInteger& function_name_leaf(ResolvedModule& module,
                                                  size_t index) {
  auto& data =
      std::get<syntax_ast::AstSectionData>(module.sections.front().payload[1]);
  return std::get<std::vector<syntax_ast::AstSectionInteger>>(data.value)
      .at(index);
}

/** The immutable original has no owner after its intended known termination failure. */
TEST(DebugSectionPayload, RejectsOriginalKnownNonterminatedFunctionName) {
  const std::string original = R"ptx(.version 9.3
.target sm_80
.file 1 "x.cu"
.section .debug_str {name:.b8 65}
.entry k(){.loc 1 2 0
.loc 1 1 0, function_name name, inlined_at 1 2 0
ret;}
)ptx";
  const auto ast = test_helpers::parseModule(original);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto bound = binding::bindSymbols(*ast);
  ASSERT_EQ(bound.table.debugLocations().size(), 2u);
  const auto range = bound.table.debugLocations()
                         .back()
                         .inline_context->function_name_label.range;
  const auto draft = resolveModuleOnly(*ast);
  ASSERT_FALSE(draft);
  EXPECT_TRUE(std::any_of(
      draft.error().begin(), draft.error().end(), [&](const auto& error) {
        return error.checker_kind ==
                   checker::CheckDiagnosticKind::RuleViolation &&
               error.range == range &&
               error.message.find("without a NUL") != std::string::npos;
      }));
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  // No source/owned/copy/clone/lifetime success is claimed for an absent owner.
  auto matched = section_owned(
      function_name_source(".section .debug_str {name:.b8 65,0;}"));
  ASSERT_TRUE(matched);
  section_lifetime(*matched);
}

/** ASCII bytes through the first explicit NUL are the complete local proof boundary. */
TEST(DebugSectionPayload, ChecksReferencedAsciiPrefixAndExplicitNul) {
  for (const auto& [payload, accepted] : {std::pair{"65,0", true},
                                          {"0", true},
                                          {"127,0", true},
                                          {"128,0", false},
                                          {"-1,0", false},
                                          {"0,255", true},
                                          {"65", false},
                                          {"65,66", false}}) {
    SCOPED_TRACE(payload);
    const auto source = function_name_source(".section .debug_str {name:.b8 " +
                                             std::string(payload) + ";}");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveModuleOnly(*ast);
    ASSERT_EQ(bool(result), accepted);
    EXPECT_EQ(bool(resolveAndValidateModule(*ast)), accepted);
    if (result) {
      EXPECT_TRUE(validateModule(*ast, *result));
      section_lifetime(*result);
    } else {
      EXPECT_TRUE(std::any_of(
          result.error().begin(), result.error().end(), [](const auto& error) {
            return error.checker_kind ==
                       checker::CheckDiagnosticKind::RuleViolation &&
                   error.message.starts_with("Debug function-name");
          }));
    }
  }
}

/** Offsets select bytes within the full logical section, not a label-bounded object. */
TEST(DebugSectionPayload, ChecksFunctionNameOffsetsAcrossOrderedFragments) {
  for (const auto& [sections, reference, accepted] :
       {std::tuple{".section .debug_str {name:.b8 65,0;}", "name + 0", true},
        {".section .debug_str {name:.b8 65,0;}", "name + 1", true},
        {".section .debug_str {name:.b8 65,0;}", ".debug_str + 1", true},
        {".section .debug_str {name:.b8 65,0;}", "name + 2", false},
        {".section .debug_str {name:.b8 65,66,0;}", "name + 1", true},
        {".section .debug_str {.b8 65; name:.b8 0;}",
         "name + 18446744073709551615U", false},
        {".section .debug_str {name:}", "name + 0", false},
        {".section .debug_str {name:.b8 65,0;}", "name + 18446744073709551615U",
         false},
        {".section .debug_str {name:.b8 0,65;}", "name + 1", false},
        {".section .debug_str {name:.b8 65;} .section .debug_str {.b8 0;}",
         "name", true},
        {".section .debug_str {name:.b8 65;} .section .debug_str {next:.b8 "
         "66;}",
         "name", false},
        {".section .debug_str {.b8 0;} .section .debug_str {name:.b8 65;}",
         "name", false},
        {".section .debug_str {name:.b8 65;} .section .debug_info {.b8 0;}",
         "name", false},
        {".section .debug_str {name:.b8 65; next:.b8 66,0;}", "name", true}}) {
    SCOPED_TRACE(std::string(sections) + reference);
    const auto ast =
        test_helpers::parseModule(function_name_source(sections, reference));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveModuleOnly(*ast);
    EXPECT_EQ(bool(result), accepted);
    if (result) {
      EXPECT_TRUE(validateModule(*result));
      EXPECT_TRUE(validateModule(*ast, *result));
    } else {
      EXPECT_TRUE(std::any_of(
          result.error().begin(), result.error().end(), [](const auto& error) {
            return error.checker_kind ==
                       checker::CheckDiagnosticKind::RuleViolation &&
                   error.message.starts_with("Debug function-name");
          }));
    }
  }
}

/** Known B16 ASCII-plus-zero needs no ordering; symbolic bytes retain qualification. */
TEST(DebugSectionPayload, DoesNotGuessRelocationOrMultibyteStringContents) {
  for (const auto sections :
       {".section .debug_str {name:.b16 65;}",
        ".section .debug_str {name:.b32 .debug_str;}",
        ".section .debug_str {name:.b32 .debug_str; .b8 255;}",
        ".section .debug_str {name:.b8 0; .b32 .debug_str;}"}) {
    const auto ast = test_helpers::parseModule(function_name_source(sections));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveModuleOnly(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    EXPECT_TRUE(validateModule(*result));
    EXPECT_TRUE(validateModule(*ast, *result));
    // Success for opaque bytes is not a linked-content or whole-DWARF certificate.
  }
  const auto ast = test_helpers::parseModule(function_name_source(
      ".section .debug_str {name:.b32 .debug_str;}", "name + 4"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveModuleOnly(*ast));
}

/** Current and rebound sections are checked independently after real source/AST death. */
TEST(DebugSectionPayload,
     RechecksFunctionNameCurrentAndSuppliedDatasetsWithoutContext) {
  const auto good_source =
      function_name_source(".section .debug_str {name:.b8 65,0;}");
  auto owner = section_owned(good_source);
  ASSERT_TRUE(owner);
  ASSERT_FALSE(owner->sections.empty());
  ASSERT_FALSE(owner->symbols.debugLocations().empty());
  const auto current_range = function_name_range(owner->symbols);
  ASSERT_TRUE(current_range);
  const auto good_ast = test_helpers::parseModule(good_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(good_ast);
  function_name_leaf(*owner, 0).magnitude.text = "66";
  EXPECT_TRUE(validateModule(*owner));
  EXPECT_TRUE(validateModule(*good_ast, *owner));
  auto different = test_helpers::parseModule(
      function_name_source(".section .debug_str {name:.b8 67,0;}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(different);
  EXPECT_TRUE(validateModule(*different, *owner));
  function_name_leaf(*owner, 1).magnitude.text = "65";
  const auto current = validateModule(*owner);
  ASSERT_FALSE(current);
  EXPECT_TRUE(
      has_string_error(current.error(), "without a NUL", *current_range));
  const auto current_with_source = validateModule(*good_ast, *owner);
  ASSERT_FALSE(current_with_source);
  EXPECT_TRUE(has_string_error(current_with_source.error(), "without a NUL",
                               *current_range));
  section_lifetime(*owner, false);
  function_name_leaf(*owner, 1).magnitude.text = "0";
  auto bad_ast = test_helpers::parseModule(
      function_name_source(".section .debug_str {name:.b8 65;}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(bad_ast);
  const auto supplied_bound = binding::bindSymbols(*bad_ast);
  const auto supplied_range = function_name_range(supplied_bound.table);
  ASSERT_TRUE(supplied_range);
  const auto supplied = validateModule(*bad_ast, *owner);
  ASSERT_FALSE(supplied);
  EXPECT_TRUE(
      has_string_error(supplied.error(), "without a NUL", *supplied_range));
  EXPECT_TRUE(validateModule(*owner));
  const auto sections = owner->sections;
  owner->sections.clear();
  EXPECT_FALSE(validateModule(*good_ast, *owner));
  owner->sections = sections;
  auto no_sections = test_helpers::parseModule(function_name_source(""));
  ASSERT_MODULE_PARSE_SUCCEEDS(no_sections);
  EXPECT_FALSE(validateModule(*no_sections, *owner));
  for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                            ModuleValidationPolicy::AvailableContext}) {
    auto incomplete = *owner;
    for (auto& region : incomplete.header.regions) {
      region.version.reset();
      region.target_options.clear();
    }
    for (auto& function : incomplete.functions) {
      function.source_version.reset();
      function.source_target.reset();
    }
    function_name_leaf(incomplete, 1).magnitude.text = "65";
    const auto incomplete_range = function_name_range(incomplete.symbols);
    ASSERT_TRUE(incomplete_range);
    const auto errors = validateModule(incomplete, policy);
    ASSERT_FALSE(errors);
    EXPECT_TRUE(
        has_string_error(errors.error(), "without a NUL", *incomplete_range));
    const auto source_errors = validateModule(*good_ast, incomplete, policy);
    ASSERT_FALSE(source_errors);
    EXPECT_TRUE(has_string_error(source_errors.error(), "without a NUL",
                                 *incomplete_range));
  }
}

/** General byte-count facts decide whole words and all provable selected suffixes. */
TEST(DebugSectionPayload, UsesOrderIndependentWiderWordFacts) {
  /** Typed source extent and proof expectation; no host byte-order oracle is used. */
  struct WordCase {
    /** Original width-valid data production. */
    std::string_view payload;
    /** Byte offset inside the same typed word/list. */
    uint64_t offset;
    /** Public validation result; qualified cases are not validity certificates. */
    bool accepted;
    /** Required known failure reason, empty for accepted/qualified controls. */
    std::string_view reason;
  };
  constexpr std::array cases{
      WordCase{".b16 0x4141", 0, false, "without a NUL"},
      WordCase{".b16 0x4141", 1, false, "without a NUL"},
      WordCase{".b16 0x4142", 0, false, "without a NUL"},
      WordCase{".b16 0x4142", 1, false, "without a NUL"},
      WordCase{".b32 0x41414141", 0, false, "without a NUL"},
      WordCase{".b32 0x41414141", 3, false, "without a NUL"},
      WordCase{".b32 0x41424344", 3, false, "without a NUL"},
      WordCase{".b64 0x4141414141414141", 0, false, "without a NUL"},
      WordCase{".b64 0x4141414141414141", 7, false, "without a NUL"},
      WordCase{".b16 0", 0, true, ""},
      WordCase{".b16 -0", 1, true, ""},
      WordCase{".b32 +0", 0, true, ""},
      WordCase{".b32 0", 3, true, ""},
      WordCase{".b64 0", 0, true, ""},
      WordCase{".b64 0", 7, true, ""},
      WordCase{".b16 0x0041", 0, true, ""},
      WordCase{".b32 0x00000041", 1, true, ""},
      WordCase{".b16 -1", 0, false, "non-ASCII"},
      WordCase{".b16 -1", 1, false, "non-ASCII"},
      WordCase{".b32 -1", 0, false, "non-ASCII"},
      WordCase{".b32 -1", 3, false, "non-ASCII"},
      WordCase{".b64 -1", 0, false, "non-ASCII"},
      WordCase{".b64 -1", 7, false, "non-ASCII"},
      WordCase{".b64 18446744073709551615", 0, false, "non-ASCII"},
      WordCase{".b32 0x80804141", 1, false, "non-ASCII"},
      WordCase{".b16 0x8041", 0, false, "non-ASCII"},
      WordCase{".b16 0x8041", 1, false, "every possible byte ordering"},
      WordCase{".b16 0x4141,0", 0, true, ""},
      WordCase{".b16 0x4141,0", 1, true, ""},
      WordCase{".b16 0x4141,0", 2, true, ""},
      WordCase{".b16 0x4141,0", 3, true, ""},
      WordCase{".b16 0x4141,0", 4, false, "byte bounds"},
      WordCase{".b16 0x0041", 1, true, ""},
      WordCase{".b32 0x41420000", 2, true, ""},
      WordCase{".b16 0x0080", 0, true, ""},
      WordCase{".b64 -9223372036854775808", 0, true, ""}};
  for (const auto& item : cases) {
    SCOPED_TRACE(std::string(item.payload) + "/" + std::to_string(item.offset));
    const auto source = function_name_source(
        ".section .debug_str {name:" + std::string(item.payload) + ";}",
        "name + " + std::to_string(item.offset));
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto bound = binding::bindSymbols(*ast);
    const auto range = bound.table.debugLocations()
                           .back()
                           .inline_context->function_name_offset->range;
    const auto result = resolveModuleOnly(*ast);
    EXPECT_EQ(bool(result), item.accepted);
    EXPECT_EQ(bool(resolveAndValidateModule(*ast)), item.accepted);
    if (result) {
      EXPECT_TRUE(validateModule(*ast, *result));
      section_lifetime(*result);
    } else {
      EXPECT_TRUE(std::any_of(
          result.error().begin(), result.error().end(), [&](const auto& error) {
            return error.checker_kind ==
                       checker::CheckDiagnosticKind::RuleViolation &&
                   error.range == range &&
                   error.message.find(item.reason) != std::string::npos;
          }));
    }
  }
  // Mixed zero/high bytes and arbitrary zero-containing suffixes above remain
  // qualified; accepted results do not select an exact serialized string.
}

/** Possible ASCII continuations compose with later known words and ordered fragments. */
TEST(DebugSectionPayload,
     ComposesWiderSuffixFactsWithoutChoosingSerialization) {
  for (const auto& [sections, reference, accepted] :
       {std::tuple{".section .debug_str {name:.b16 0x4142;} .section "
                   ".debug_str {.b8 0;}",
                   "name", true},
        {".section .debug_str {name:.b16 0x4142;} .section .debug_str "
         "{next:.b8 66;}",
         "name + 1", false},
        {".section .debug_str {name:.b16 0x0041; .b8 0;}", "name + 1", true},
        {".section .debug_str {name:.b32 0x41420000;} .section .debug_str {.b8 "
         "0;}",
         "name + 2", true},
        {".section .debug_str {name:.b16 0x4141;} .section .debug_info {.b8 "
         "0;}",
         "name", false},
        {".section .debug_str {name:.b16 -1; .b8 0;}", "name", false},
        {".section .debug_str {name:.b8 0; .b16 -1; .b32 .debug_str;}", "name",
         true},
        {".section .debug_str {name:.b32 .debug_str; .b64 0;}", "name", true},
        {".section .debug_str {name:.b32 .debug_str; .b64 0;}", "name + 4",
         true},
        {".section .debug_str {name:.b16 0x8041; .b8 0;}", "name + 1", true}}) {
    SCOPED_TRACE(std::string(sections) + reference);
    const auto ast =
        test_helpers::parseModule(function_name_source(sections, reference));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveModuleOnly(*ast);
    EXPECT_EQ(bool(result), accepted);
    if (result) {
      EXPECT_TRUE(validateModule(*result));
      EXPECT_TRUE(validateModule(*ast, *result));
    }
  }
  // The symbolic-prefix-at-zero and high-byte suffix controls are not-proven, unlike the ASCII-only
  // ambiguous suffix followed by a known zero, whose active alternatives all terminate.
}

/** Wider current words are recomputed independently of healthy or bad supplied datasets. */
TEST(DebugSectionPayload, RechecksWiderCurrentDatasetsAfterSourceDeath) {
  const auto good_source =
      function_name_source(".section .debug_str {name:.b16 0x4141,0;}");
  auto owner = section_owned(good_source);
  ASSERT_TRUE(owner);
  ASSERT_FALSE(owner->sections.empty());
  ASSERT_FALSE(owner->symbols.debugLocations().empty());
  const auto current_range = function_name_range(owner->symbols);
  ASSERT_TRUE(current_range);
  const auto healthy = test_helpers::parseModule(good_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(healthy);
  function_name_leaf(*owner, 0).magnitude.text = "0x4243";
  EXPECT_TRUE(validateModule(*owner));
  EXPECT_TRUE(validateModule(*healthy, *owner));
  const auto different = test_helpers::parseModule(
      function_name_source(".section .debug_str {name:.b16 0x4445,0;}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(different);
  EXPECT_TRUE(validateModule(*different, *owner));
  function_name_leaf(*owner, 1).magnitude.text = "0x4242";
  auto bad = validateModule(*owner);
  ASSERT_FALSE(bad);
  EXPECT_TRUE(has_string_error(bad.error(), "without a NUL", *current_range));
  auto supplied = validateModule(*healthy, *owner);
  ASSERT_FALSE(supplied);
  EXPECT_TRUE(
      has_string_error(supplied.error(), "without a NUL", *current_range));
  section_lifetime(*owner, false);
  function_name_leaf(*owner, 1).magnitude.text = "0";
  const auto bad_source = test_helpers::parseModule(
      function_name_source(".section .debug_str {name:.b16 0x4546,0x4748;}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(bad_source);
  const auto supplied_bound = binding::bindSymbols(*bad_source);
  const auto supplied_range = function_name_range(supplied_bound.table);
  ASSERT_TRUE(supplied_range);
  auto rejected = validateModule(*bad_source, *owner);
  ASSERT_FALSE(rejected);
  EXPECT_TRUE(
      has_string_error(rejected.error(), "without a NUL", *supplied_range));
  EXPECT_TRUE(validateModule(*owner));
  for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                            ModuleValidationPolicy::AvailableContext}) {
    auto incomplete = *owner;
    for (auto& region : incomplete.header.regions) {
      region.version.reset();
      region.target_options.clear();
    }
    for (auto& function : incomplete.functions) {
      function.source_version.reset();
      function.source_target.reset();
    }
    function_name_leaf(incomplete, 1).magnitude.text = "0x4242";
    const auto incomplete_range = function_name_range(incomplete.symbols);
    ASSERT_TRUE(incomplete_range);
    const auto current = validateModule(incomplete, policy);
    ASSERT_FALSE(current);
    EXPECT_TRUE(
        has_string_error(current.error(), "without a NUL", *incomplete_range));
    const auto with_source = validateModule(*healthy, incomplete, policy);
    ASSERT_FALSE(with_source);
    EXPECT_TRUE(has_string_error(with_source.error(), "without a NUL",
                                 *incomplete_range));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
