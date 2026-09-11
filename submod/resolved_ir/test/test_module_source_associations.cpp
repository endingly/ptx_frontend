#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete module and retain a useful failure in the test output. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "PTX source did not parse."
                          : parsed.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Test whether a resolution diagnostic carries the requested checker kind. */
bool hasCheckerKind(const ModuleResolveDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.checker_kind && *diagnostic.checker_kind == kind)
      return true;
  }
  return false;
}

/** Test whether a checker result carries the requested diagnostic kind. */
bool hasCheckerKind(const checker::CheckDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind == kind)
      return true;
  }
  return false;
}

TEST(ModuleValidationContract, MatchesFunctionsIndependentlyOfIrVectorOrder) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
.func second() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  std::swap(resolved->functions[0], resolved->functions[1]);

  EXPECT_TRUE(checkModuleAvailability(*ast, *resolved));
}

TEST(ModuleValidationContract, KeepsPrototypeAndDefinitionScopesDistinct) {
  const auto ast = parseModule(R"ptx(
.func callee(.param .u32 input);
.func callee(.param .u32 input) {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);

  const auto& prototype = resolved->functions[0];
  const auto& definition = resolved->functions[1];
  EXPECT_EQ(prototype.symbol_id, definition.symbol_id);
  EXPECT_NE(prototype.declaration_scope, definition.declaration_scope);
  EXPECT_EQ(resolved->symbols.functionScope(prototype.range),
            prototype.declaration_scope);
  EXPECT_EQ(resolved->symbols.functionScope(definition.range),
            definition.declaration_scope);
}

TEST(ModuleValidationContract, ReportsMissingAndExtraFunctions) {
  const auto two_functions = parseModule(R"ptx(
.func first() { ret; }
.func second() { ret; }
)ptx");
  ASSERT_TRUE(two_functions);
  auto missing = resolveModule(*two_functions);
  ASSERT_TRUE(missing.has_value()) << missing.error().front().message;
  ASSERT_EQ(missing->functions.size(), 2u);
  missing->functions.pop_back();
  const auto missing_result = checkModuleAvailability(*two_functions, *missing);
  ASSERT_FALSE(missing_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(missing_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));

  const auto one_function = parseModule(R"ptx(
.func first() { ret; }
)ptx");
  ASSERT_TRUE(one_function);
  auto extra = resolveModule(*one_function);
  ASSERT_TRUE(extra.has_value()) << extra.error().front().message;
  extra->functions.push_back(extra->functions.front());
  const auto extra_result = checkModuleAvailability(*one_function, *extra);
  ASSERT_FALSE(extra_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(extra_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

TEST(ModuleValidationContract, ReportsMissingAndExtraInstructions) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  auto missing = resolveModule(*ast);
  ASSERT_TRUE(missing.has_value()) << missing.error().front().message;
  ASSERT_EQ(missing->functions.front().body.size(), 1u);
  missing->functions.front().body.clear();
  const auto missing_result = checkModuleAvailability(*ast, *missing);
  ASSERT_FALSE(missing_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(missing_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));

  auto extra = resolveModule(*ast);
  ASSERT_TRUE(extra.has_value()) << extra.error().front().message;
  auto& function = extra->functions.front();
  function.body.push_back(function.body.front());
  function.instruction_ranges.push_back(function.instruction_ranges.front());
  function.instruction_opcodes.push_back(function.instruction_opcodes.front());
  const auto extra_result = checkModuleAvailability(*ast, *extra);
  ASSERT_FALSE(extra_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(extra_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

TEST(ModuleValidationContract, ReportsMalformedSourceMapAndOpcodeMismatch) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  auto malformed_map = resolveModule(*ast);
  ASSERT_TRUE(malformed_map.has_value())
      << malformed_map.error().front().message;
  malformed_map->functions.front().instruction_ranges.clear();
  const auto map_result = checkModuleAvailability(*ast, *malformed_map);
  ASSERT_FALSE(map_result.has_value());
  EXPECT_TRUE(hasCheckerKind(
      map_result.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));

  auto wrong_opcode = resolveModule(*ast);
  ASSERT_TRUE(wrong_opcode.has_value()) << wrong_opcode.error().front().message;
  wrong_opcode->functions.front().instruction_opcodes.front() = "add";
  const auto opcode_result = checkModuleAvailability(*ast, *wrong_opcode);
  ASSERT_FALSE(opcode_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(opcode_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

TEST(ModuleValidationContract, RejectsChangedSameShapeSource) {
  const auto original = parseModule(R"ptx(
.func first() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %r0;
}
)ptx");
  const auto changed = parseModule(R"ptx(
.func first() {
  .reg .u32 %r<2>;
  mov.u32 %r1, %r0;
}
)ptx");
  ASSERT_TRUE(original);
  ASSERT_TRUE(changed);
  const auto resolved = resolveModule(*original);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto result = checkModuleAvailability(*changed, *resolved);
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(hasCheckerKind(
      result.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

TEST(ModuleValidationContract, OwnsInstructionSourcesAfterAstDestruction) {
  /** Return only the IR, destroying both source storage and syntax nodes. */
  const auto resolve_local = [] {
    const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_80
.func local() { { ret; } }
)ptx");
    return resolveModule(*ast);
  };
  const auto resolved = resolve_local();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  ASSERT_EQ(function.instruction_ranges.size(), 1u);
  EXPECT_EQ(function.instruction_ranges.front().start.line, 4);
  EXPECT_EQ(function.instruction_opcodes.front(), "ret");
  EXPECT_EQ(function.source_target, "sm_80");
  EXPECT_FALSE(function.source_identity.empty());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
