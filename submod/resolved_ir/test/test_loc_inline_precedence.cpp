#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Build complete source without introducing a target-debug presence obligation. */
std::string locationSource(std::string_view body) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n.file 1 \"a.cu\"\n"
         ".section .debug_str { name: .b8 0; }\n.entry k() {\n" +
         std::string(body) + "\nret;\n}\n";
}

/** Matching prior bases pass, while future, absent and self-only targets fail. */
TEST(LocInlinePrecedence, RequiresAnAlreadySeenBase) {
  for (const auto& [body, accepted] :
       {std::pair{
            ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0",
            true},
        {".loc 1 1 0, function_name name, inlined_at 1 2 0\n.loc 1 2 0", false},
        {".loc 1 1 0, function_name name, inlined_at 1 2 0\n.loc 1 3 0", false},
        {".loc 1 2 0, function_name name, inlined_at 1 2 0", false}}) {
    SCOPED_TRACE(body);
    std::optional<ResolvedModule> owned;
    {
      PtxSyntaxParser parser(locationSource(body));
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft);
      EXPECT_EQ(bool(resolveAndValidateModule(*ast)), accepted);
      EXPECT_EQ(bool(validateModule(*ast, *draft)), accepted);
      owned.emplace(std::move(*draft));
    }
    EXPECT_EQ(bool(validateModule(*owned)), accepted);
    const ResolvedModule copy = *owned;
    EXPECT_EQ(bool(validateModule(copy)), accepted);
    ResolvedModule assigned;
    assigned = copy;
    EXPECT_EQ(bool(validateModule(assigned)), accepted);
  }
}

/** Parse and resolve transport while ensuring every returned module outlives its AST. */
std::optional<ResolvedModule> locationDraft(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast);
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto draft = resolveModuleOnly(*ast);
  EXPECT_TRUE(draft);
  if (!draft)
    return std::nullopt;
  return std::move(*draft);
}

/** Enumerate mutable parsed directives without changing their written boundaries. */
std::vector<syntax_ast::AstLocDirective*> locationNodes(
    syntax_ast::AstModule& ast) {
  std::vector<syntax_ast::AstLocDirective*> nodes;
  for (auto& item : ast.items)
    if (auto* function = std::get_if<syntax_ast::AstFunction>(&item))
      for (auto& statement : function->body)
        if (auto* location =
                std::get_if<syntax_ast::AstLocDirective>(&statement))
          nodes.push_back(location);
  return nodes;
}

/** Every lexical nesting and duplicate case uses base events rather than target definitions. */
TEST(LocInlinePrecedence, TraversesWrittenModuleOrder) {
  const std::string p = ".loc 1 2 0\n";
  const std::string i = ".loc 1 1 0, function_name name, inlined_at 1 2 0\n";
  const std::string c = ".loc 1 3 0, function_name name, inlined_at 1 1 0\n";
  for (const auto& [body, accepted] : std::vector<std::pair<std::string, bool>>{
           {p + i, true},
           {p + p + i, true},
           {i + p + p, false},
           {p + i + c, true},
           {i + i, false},
           {p + "{" + i + "}\n", true},
           {"{" + p + "}\n" + i, true},
           {"{" + p + "}\n{" + i + "}\n", true},
           {i + "{" + p + "}\n", false},
           {p + ".loc 1 2 0, function_name name, inlined_at 1 2 0\n", true},
           {".loc 1 99 88\n.loc 1 1 0, function_name name, inlined_at 1 99 "
            "88\n",
            true},
           {p + ".loc 1 1 0, function_name name, inlined_at 1 3 0\n", false},
           {p + ".loc 1 1 0, function_name name, inlined_at 1 2 1\n", false},
           {p + ".loc 1 1 0, function_name name + 0, inlined_at 1 2 0\n",
            true}}) {
    SCOPED_TRACE(body);
    auto module = locationDraft(locationSource(body));
    ASSERT_TRUE(module);
    const auto result = validateModule(*module);
    EXPECT_EQ(bool(result), accepted);
    if (!result)
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
        return error.message.starts_with("inlined_at requires a preceding");
      }));
  }
  for (const bool earlier : {true, false}) {
    for (const bool prototype : {true, false}) {
      auto source = locationSource(earlier ? p : i);
      source += prototype ? ".func h();\n" : ".target sm_90\n";
      source += ".func g() {\n" + (earlier ? i : p) + "ret;\n}\n";
      auto module = locationDraft(source);
      ASSERT_TRUE(module);
      EXPECT_EQ(bool(validateModule(*module)), earlier);
    }
  }
  // A compatible prototype shares its SymbolId with the later definition but
  // does not replace that definition's lexical occurrence scope.
  auto source = locationSource(p + i);
  const auto entry = source.find(".entry k()");
  source.insert(entry, ".func f();\n");
  source += ".func f() { .loc 1 3 0 ret; }\n";
  auto module = locationDraft(source);
  ASSERT_TRUE(module);
  EXPECT_TRUE(validateModule(*module));
}

/** Numeric identity includes all uint64 magnitudes, radices and explicit zero offsets. */
TEST(LocInlinePrecedence, PreservesFullWidthNumericLeaves) {
  for (const std::string value : {"0", "4294967296", "18446744073709551615U"}) {
    for (unsigned field = 0; field < 7; ++field) {
      SCOPED_TRACE(value + "/" + std::to_string(field));
      std::array<std::string, 3> candidate{"1", "2", "0"};
      std::array<std::string, 3> current{"1", "1", "0"};
      if (field < 3)
        current[field] = value;
      else if (field < 6)
        candidate[field - 3] = value;
      const auto triple = [](const std::array<std::string, 3>& values) {
        return values[0] + " " + values[1] + " " + values[2];
      };
      std::string source = locationSource(
          ".loc " + triple(candidate) + "\n.loc " + triple(current) +
          ", function_name name" + (field == 6 ? " + " + value : "") +
          ", inlined_at " + triple(candidate));
      if ((field == 0 || field == 3) && value != "1")
        source.insert(source.find(".section"),
                      ".file " + value + " \"other.cu\"\n");
      if (field == 6 && value != "0") {
        PtxSyntaxParser parser(source);
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        const auto bound = binding::bindSymbols(*ast);
        ASSERT_EQ(bound.table.debugLocations().size(), 2u);
        const auto& offset = *bound.table.debugLocations()
                                  .back()
                                  .inline_context->function_name_offset;
        EXPECT_EQ(offset.text, value);
        const auto decoded = base::parseIntegerMagnitude(offset.text);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, value == "4294967296"
                                ? uint64_t{1} << 32
                                : std::numeric_limits<uint64_t>::max());
        const auto rejected = resolveModuleOnly(*ast);
        ASSERT_FALSE(rejected);
        EXPECT_TRUE(std::any_of(
            rejected.error().begin(), rejected.error().end(),
            [&](const auto& error) {
              return error.checker_kind ==
                         checker::CheckDiagnosticKind::RuleViolation &&
                     error.range == offset.range &&
                     error.message.find("byte bounds") != std::string::npos;
            }));
        EXPECT_FALSE(resolveAndValidateModule(*ast));
        // Full U64 leaves remain valid; the selected finite section byte is absent.
        continue;
      }
      auto module = locationDraft(source);
      ASSERT_TRUE(module);
      ASSERT_EQ(module->symbols.debugLocations().size(), 2u);
      EXPECT_TRUE(validateModule(*module));
      const ResolvedModule copy = *module;
      ResolvedModule assigned;
      assigned = copy;
      EXPECT_TRUE(validateModule(assigned));
      const auto& record = assigned.symbols.debugLocations().back();
      EXPECT_GT(record.range.start.line, 0);
      EXPECT_EQ(record.inline_context->function_name_offset.has_value(),
                field == 6);
    }
  }
  auto source = locationSource(
      ".loc 0x10U 020 0B10000u\n"
      ".loc 16 1 0, function_name name, inlined_at 16 16 16");
  source.replace(source.find(".file 1 "), 8, ".file 16 ");
  auto module = locationDraft(source);
  ASSERT_TRUE(module);
  EXPECT_TRUE(validateModule(*module));
  auto wrong_file = locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 2 2 0");
  wrong_file.insert(wrong_file.find(".section"), ".file 2 \"b.cu\"\n");
  auto wrong = locationDraft(wrong_file);
  ASSERT_TRUE(wrong);
  EXPECT_FALSE(validateModule(*wrong));
}

/** Rebinding public AST values permits current changes without mutable table accessors. */
TEST(LocInlinePrecedence, RechecksConstructedNumericAndRangeEdits) {
  for (unsigned field = 0; field < 7; ++field) {
    for (const std::string bad :
         {"18446744073709551616U", "-1", "1.0", "1+0", "0xG"}) {
      PtxSyntaxParser parser(locationSource(
          ".loc 1 2 0\n.loc 1 1 0, function_name name + 0, inlined_at 1 2 0"));
      auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module);
      auto nodes = locationNodes(*ast);
      auto& context = *nodes.back()->inline_context;
      std::array<syntax_ast::AstSyntax*, 7> leaves{
          &nodes.back()->file_index,      &nodes.back()->line_number,
          &nodes.back()->column_position, &context.file_index,
          &context.line_number,           &context.column_position,
          &*context.function_name_offset};
      leaves[field]->text = bad;
      module->symbols = binding::bindSymbols(*ast).table;
      EXPECT_FALSE(validateModule(*module));
      EXPECT_FALSE(validateModule(*ast, *module));
    }
  }
  for (const unsigned malformed : {0u, 1u, 2u, 3u}) {
    PtxSyntaxParser parser(locationSource(
        ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"));
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto nodes = locationNodes(*ast);
    if (malformed == 0)
      nodes.back()->line_number.range = {{999, 1}, {999, 2}};
    if (malformed == 1)
      nodes.back()->inline_context->range.end.column = 0;
    if (malformed == 2)
      std::swap(nodes.back()->file_index.range,
                nodes.back()->column_position.range);
    if (malformed == 3)
      nodes.back()->range = {{1, 1}, {1, 2}};
    module->symbols = binding::bindSymbols(*ast).table;
    EXPECT_FALSE(validateModule(*module));
    EXPECT_FALSE(validateModule(*ast, *module));
  }
}

/** Stored event order controls validation even when physical ranges remain attached. */
TEST(LocInlinePrecedence, UsesCurrentOrderAndIndependentCopies) {
  PtxSyntaxParser parser(locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"));
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  const auto original = *module;
  auto nodes = locationNodes(*ast);
  std::swap(*nodes[0], *nodes[1]);
  module->symbols = binding::bindSymbols(*ast).table;
  EXPECT_FALSE(validateModule(*module));
  EXPECT_FALSE(validateModule(*ast, original));
  EXPECT_FALSE(validateModule(*ast, *module));
  EXPECT_TRUE(validateModule(original));
  std::swap(*nodes[0], *nodes[1]);
  module->symbols = binding::bindSymbols(*ast).table;
  EXPECT_TRUE(validateModule(*module));
  nodes[1]->inline_context->line_number.text = "9";
  auto copy = original;
  copy.symbols = binding::bindSymbols(*ast).table;
  EXPECT_FALSE(validateModule(copy));
  EXPECT_FALSE(validateModule(*ast, original));
  EXPECT_TRUE(validateModule(original));
  ResolvedModule assigned;
  assigned = original;
  ResolvedModule moved = std::move(assigned);
  EXPECT_TRUE(validateModule(moved));
}

/** Both datasets validate independently; executable identity remains location-neutral. */
TEST(LocInlinePrecedence, KeepsReplacementDataIndependent) {
  auto module = locationDraft(locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"));
  ASSERT_TRUE(module);
  for (const bool valid : {true, false}) {
    PtxSyntaxParser parser(locationSource(
        valid ? ".loc 1 8 0\n.loc 1 9 0, function_name name, inlined_at 1 8 "
                "0\n.loc 1 7 0"
              : ".loc 1 8 0, function_name name, inlined_at 1 8 0"));
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_EQ(bool(validateModule(*ast, *module)), valid);
  }
  PtxSyntaxParser valid_parser(locationSource(".loc 1 7 0"));
  const auto valid_ast = valid_parser.parseModule();
  ASSERT_TRUE(valid_ast);
  PtxSyntaxParser invalid_parser(locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 9 0"));
  const auto invalid_ast = invalid_parser.parseModule();
  ASSERT_TRUE(invalid_ast);
  module->symbols = binding::bindSymbols(*invalid_ast).table;
  EXPECT_FALSE(validateModule(*valid_ast, *module));
  EXPECT_FALSE(validateModule(*invalid_ast, *module));
}

/** Version thresholds are independent of target architectures and intrinsic precedence. */
TEST(LocInlinePrecedence, SeparatesAvailabilityFromIntrinsicFailures) {
  for (const std::string version : {"7.1", "7.2"}) {
    for (const bool inline_form : {true, false}) {
      auto source =
          locationSource(inline_form ? ".loc 1 2 0\n.loc 1 1 0, function_name "
                                       ".debug_str, inlined_at 1 2 0"
                                     : ".loc 1 2 0");
      source.replace(source.find("9.3"), 3, version);
      source.replace(source.find("sm_90"), 5, "sm_80");
      source.replace(source.find("name: "), 6, "");
      auto module = locationDraft(source);
      ASSERT_TRUE(module);
      const auto result = validateModule(*module);
      EXPECT_EQ(bool(result), !inline_form || version == "7.2");
      if (!result)
        EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
          return error.message ==
                 ".loc inline attributes require PTX ISA >= 7.2.";
        }));
    }
  }
  for (const bool valid : {true, false}) {
    auto source = locationSource(
        valid ? ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"
              : ".loc 1 1 0, function_name name, inlined_at 1 2 0");
    source.erase(0, source.find('\n') + 1);
    auto module = locationDraft(source);
    ASSERT_TRUE(module);
    EXPECT_EQ(
        bool(validateModule(*module, ModuleValidationPolicy::AvailableContext)),
        valid);
    EXPECT_FALSE(validateModule(
        *module, ModuleValidationPolicy::RequireCompleteContext));
  }
}

/** Public observation is immutable; independent table values own the location strings. */
static_assert(
    std::is_same_v<
        decltype(std::declval<const binding::SymbolTable&>().debugLocations()),
        std::span<const binding::DebugLocationDeclaration>>);

/** Edited tables survive source death; current coherent values have no history veto. */
TEST(LocInlinePrecedence, RetainsEditedTablesAfterSourceDestruction) {
  for (const unsigned mode : {0u, 1u, 2u, 3u}) {
    const bool coherent = mode == 0;
    SCOPED_TRACE(mode);
    std::optional<ResolvedModule> edited;
    std::optional<ResolvedModule> original;
    {
      PtxSyntaxParser parser(locationSource(
          ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"));
      auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module);
      original.emplace(*module);
      auto nodes = locationNodes(*ast);
      nodes[1]->inline_context->line_number.text = "9";
      if (coherent)
        nodes[0]->line_number.text = "9";
      if (mode == 2)
        nodes[1]->inline_context->line_number.text = "18446744073709551616U";
      if (mode == 3) {
        nodes[1]->inline_context->line_number.text = "2";
        nodes[1]->inline_context->line_number.range = {{999, 1}, {999, 2}};
      }
      module->symbols = binding::bindSymbols(*ast).table;
      EXPECT_EQ(bool(validateModule(*ast, *module)), coherent);
      edited.emplace(std::move(*module));
    }
    const auto checked = validateModule(*edited);
    EXPECT_EQ(bool(checked), coherent);
    if (!checked) {
      const auto& target =
          edited->symbols.debugLocations()[1].inline_context->target;
      const SourceRange range =
          mode == 1 ? SourceRange{target.file_index.range.start,
                                  target.column_position.range.end}
                    : target.line_number.range;
      const std::string message =
          mode == 1 ? "inlined_at requires a preceding .loc base (1, 9, 0)."
          : mode == 2
              ? "Debug location integer must fit an unsigned 64-bit magnitude."
              : "Debug location leaf has invalid source boundaries.";
      EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& error) {
        return error.kind == checker::CheckDiagnosticKind::RuleViolation &&
               error.range == range && error.message == message;
      }));
      if (mode != 3)
        EXPECT_EQ(checked.error().size(), 1u);
    }
    const auto copy = *edited;
    ResolvedModule assigned;
    assigned = copy;
    EXPECT_EQ(bool(validateModule(copy)), coherent);
    EXPECT_EQ(bool(validateModule(assigned)), coherent);
    EXPECT_TRUE(validateModule(*original));
    auto function_copy = original->functions.front();
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    ASSERT_FALSE(function_assigned.body.empty());
    EXPECT_NE(function_assigned.body[0].get(),
              original->functions[0].body[0].get());
    EXPECT_TRUE(function_assigned.body[0]->clone());
  }
}

/** Coincident physical keys retain one-to-one reference multiplicity, not global uniqueness. */
TEST(LocInlinePrecedence, MatchesRepeatedPhysicalKeysOneToOne) {
  PtxSyntaxParser parser(locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0"));
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  auto nodes = locationNodes(*ast);
  const auto duplicate = *nodes[0];
  for (auto& item : ast->items)
    if (auto* function = std::get_if<syntax_ast::AstFunction>(&item))
      function->body.insert(function->body.begin() + 1, duplicate);
  module->symbols = binding::bindSymbols(*ast).table;
  ASSERT_EQ(module->symbols.debugLocations().size(), 3u);
  EXPECT_EQ(module->symbols.debugLocations()[0].range,
            module->symbols.debugLocations()[1].range);
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(validateModule(*ast, *module));
}

/** Source syntax rejection and transport rejection are recorded without fabricated lifetime passes. */
TEST(LocInlinePrecedence, RejectsInvalidSourceIntegerLeavesAtTheirActualStage) {
  for (unsigned field = 0; field < 7; ++field) {
    for (const std::string value : {"-1", "18446744073709551616U"}) {
      std::array<std::string, 7> leaves{"1", "1", "0", "1", "2", "0", "0"};
      leaves[field] = value;
      const auto source = locationSource(
          ".loc 1 2 0\n.loc " + leaves[0] + " " + leaves[1] + " " + leaves[2] +
          ", function_name name + " + leaves[6] + ", inlined_at " + leaves[3] +
          " " + leaves[4] + " " + leaves[5]);
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty()) {
        std::cout << "numeric-source field=" << field << " value=" << value
                  << " stage=parse owned=N_A\n";
        continue;
      }
      auto draft = resolveModuleOnly(*ast);
      if (!draft) {
        EXPECT_FALSE(draft.error().empty());
        std::cout << "numeric-source field=" << field << " value=" << value
                  << " stage=resolution owned=N_A\n";
        continue;
      }
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      EXPECT_FALSE(validateModule(*ast, *draft));
      EXPECT_FALSE(validateModule(*draft));
      std::cout << "numeric-source field=" << field << " value=" << value
                << " stage=validation owned=AST_FREE_API\n";
    }
  }
}

/** Namespace errors remain current errors even when valid replacement metadata exists. */
TEST(LocInlinePrecedence,
     RechecksBothMetadataJoinsBeforeLayeredBindingDiagnostics) {
  const auto source = locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name name, inlined_at 1 2 0");
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  const auto original = *module;
  auto nodes = locationNodes(*ast);
  nodes[1]->inline_context->function_name_label.syntax.text = "k";
  const auto rebound = binding::bindSymbols(*ast);
  ASSERT_FALSE(rebound.diagnostics.empty());
  module->symbols = rebound.table;
  PtxSyntaxParser valid_parser(source);
  const auto valid_ast = valid_parser.parseModule();
  ASSERT_TRUE(valid_ast);
  for (const auto& result :
       {validateModule(*module), validateModule(*valid_ast, *module),
        validateModule(*ast, original)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& error) {
      return error.kind == checker::CheckDiagnosticKind::RuleViolation &&
             error.range ==
                 nodes[1]->inline_context->function_name_label.syntax.range &&
             error.message ==
                 "Debug location leaf disagrees with its bound metadata "
                 "identity.";
    }));
  }
}

/** Supplied debug data supplies its version; retained intrinsic validity cannot be bypassed. */
TEST(LocInlinePrecedence, UsesReplacementVersionWithoutAHistoricalInlineVeto) {
  auto source = locationSource(
      ".loc 1 2 0\n.loc 1 1 0, function_name .debug_str, inlined_at 1 2 0");
  source.replace(source.find("9.3"), 3, "7.2");
  source.replace(source.find("sm_90"), 5, "sm_80");
  source.replace(source.find("name: "), 6, "");
  auto module = locationDraft(source);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  auto basic_source = source;
  basic_source.replace(basic_source.find("7.2"), 3, "7.1");
  const auto inline_start = basic_source.find(".loc 1 1 0,");
  basic_source.erase(inline_start,
                     basic_source.find('\n', inline_start) - inline_start);
  PtxSyntaxParser parser(basic_source);
  const auto basic_ast = parser.parseModule();
  ASSERT_TRUE(basic_ast);
  ASSERT_TRUE(basic_ast.diagnostics.empty());
  EXPECT_TRUE(resolveAndValidateModule(*basic_ast));
  EXPECT_TRUE(validateModule(*basic_ast, *module));
  for (auto& region : module->header.regions)
    region.version = checker::PtxVersion{7, 1};
  for (auto& function : module->functions)
    function.source_version = checker::PtxVersion{7, 1};
  const auto old = validateModule(*module);
  ASSERT_FALSE(old);
  EXPECT_TRUE(std::ranges::any_of(old.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::UnsupportedPtxVersion &&
           error.message == ".loc inline attributes require PTX ISA >= 7.2.";
  }));
  EXPECT_TRUE(validateModule(*basic_ast, *module));
}

/** Replacement functions use their own module lexical stream, never retained event order. */
TEST(LocInlinePrecedence, RechecksReplacementFunctionOrder) {
  const std::string p = ".loc 1 2 0\n";
  const std::string i = ".loc 1 1 0, function_name name, inlined_at 1 2 0\n";
  auto module =
      locationDraft(locationSource(p) + ".func g() {" + i + "ret;}\n");
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  PtxSyntaxParser parser(locationSource(i) + ".func g() {" + p + "ret;}\n");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto result = validateModule(*ast, *module);
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
    return error.message.starts_with("inlined_at requires a preceding");
  }));
  EXPECT_TRUE(std::ranges::all_of(result.error(), [](const auto& error) {
    return error.kind != checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
