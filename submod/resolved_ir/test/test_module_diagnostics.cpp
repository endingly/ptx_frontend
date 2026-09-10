#include <gtest/gtest.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a module, retaining failure diagnostics without dereferencing invalid input. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  if (!module || !module.diagnostics.empty()) {
    ADD_FAILURE() << (module.diagnostics.empty()
                          ? "PTX source did not parse."
                          : module.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*module);
}

/** Verify the lossless projection of one binding diagnostic into resolution. */
void expectBindingProjection(const ResolveDiagnostic& actual,
                             const binding::BindDiagnostic& expected) {
  EXPECT_EQ(actual.stage(), ResolveDiagnosticStage::Binding);
  EXPECT_EQ(actual.binding_kind, expected.kind);
  EXPECT_EQ(actual.range, expected.range);
  EXPECT_EQ(actual.previous_range, expected.previous_range);
  EXPECT_EQ(actual.message, expected.message);
  EXPECT_FALSE(actual.declaration_kind);
  EXPECT_FALSE(actual.checker_kind);
}

/** Verify the lossless projection of one declaration diagnostic into resolution. */
void expectDeclarationProjection(
    const ResolveDiagnostic& actual,
    const declaration_semantics::DeclarationDiagnostic& expected) {
  EXPECT_EQ(actual.stage(), ResolveDiagnosticStage::DeclarationSemantics);
  EXPECT_EQ(actual.declaration_kind, expected.kind);
  EXPECT_EQ(actual.range, expected.range);
  EXPECT_EQ(actual.previous_range, expected.previous_range);
  EXPECT_EQ(actual.message, expected.message);
  EXPECT_FALSE(actual.binding_kind);
  EXPECT_FALSE(actual.checker_kind);
}

/** Verify the lossless projection of one checker diagnostic into resolution. */
void expectCheckerProjection(const ResolveDiagnostic& actual,
                             const checker::CheckDiagnostic& expected) {
  EXPECT_EQ(actual.stage(), ResolveDiagnosticStage::Checking);
  EXPECT_EQ(actual.checker_kind, expected.kind);
  EXPECT_EQ(actual.range, expected.range);
  EXPECT_EQ(actual.message, expected.message);
  EXPECT_FALSE(actual.previous_range);
  EXPECT_FALSE(actual.binding_kind);
  EXPECT_FALSE(actual.declaration_kind);
}

/** Resolve invalid input in a nested scope so all parser-owned data dies first. */
ModuleResolveDiagnostics diagnosticsAfterInputDies() {
  std::string source = R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %missing;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << (ast.diagnostics.empty()
                          ? "PTX source did not parse."
                          : ast.diagnostics.front().message);
    return {};
  }
  auto resolved = resolveModule(*ast);
  if (resolved) {
    ADD_FAILURE() << "PTX source unexpectedly resolved.";
    return {};
  }
  return std::move(resolved.error());
}

/** Binding categories, messages, primary ranges, and related ranges survive. */
TEST(ModuleDiagnostics, ProjectsBindingDiagnosticsWithoutClassification) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
again:
again:
  .reg .u32 %dst;
  mov.u32 %dst, %missing;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  ASSERT_EQ(binding.diagnostics.size(), 2u);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), binding.diagnostics.size());
  for (size_t index = 0; index < binding.diagnostics.size(); ++index)
    expectBindingProjection(resolved.error()[index], binding.diagnostics[index]);

  EXPECT_EQ(resolved.error()[0].binding_kind,
            binding::BindDiagnosticKind::DuplicateSymbol);
  EXPECT_TRUE(resolved.error()[0].previous_range);
  EXPECT_EQ(resolved.error()[1].binding_kind,
            binding::BindDiagnosticKind::UnresolvedReference);
  EXPECT_FALSE(resolved.error()[1].previous_range);
}

/** Binding diagnostics precede declaration diagnostics while preserving each order. */
TEST(ModuleDiagnostics, OrdersBindingThenDeclarationSemanticsDiagnostics) {
  const auto ast = parseModule(R"ptx(
.extern .global .u32 conflicting[2];
.extern .global .u64 conflicting[2];
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %missing;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  const auto declarations =
      declaration_semantics::checkDeclarations(*ast, binding.table);
  ASSERT_EQ(binding.diagnostics.size(), 1u);
  ASSERT_EQ(declarations.size(), 1u);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(),
            binding.diagnostics.size() + declarations.size());
  expectBindingProjection(resolved.error().front(), binding.diagnostics.front());
  expectDeclarationProjection(resolved.error().back(), declarations.front());
  EXPECT_EQ(resolved.error().back().declaration_kind,
            declaration_semantics::DeclarationDiagnosticKind::
                IncompatibleRedeclaration);
  EXPECT_TRUE(resolved.error().back().previous_range);
}

/** Storage lowering keeps its declaration category even without a public lowerer. */
TEST(ModuleDiagnostics, ProjectsStorageOnlyDeclarationDiagnostic) {
  const auto ast = parseModule(".global .bf16 unsupported_storage;");
  ASSERT_TRUE(ast);
  const auto resolved = resolveModule(*ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  const ResolveDiagnostic& diagnostic = resolved.error().front();
  EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::DeclarationSemantics);
  EXPECT_EQ(diagnostic.declaration_kind,
            declaration_semantics::DeclarationDiagnosticKind::
                UnsupportedStorageDeclaration);
  EXPECT_EQ(diagnostic.range,
            std::get<syntax_ast::AstVariableDeclaration>(ast->items.front())
                .type.range);
  EXPECT_FALSE(diagnostic.previous_range);
  EXPECT_FALSE(diagnostic.binding_kind);
  EXPECT_FALSE(diagnostic.checker_kind);
}

/** Availability checker categories and locations survive module resolution. */
TEST(ModuleDiagnostics, ProjectsCheckerDiagnosticForRecognizedTargetAndVersion) {
  constexpr std::string_view unchecked_source = R"ptx(
.version 0.9
.entry kernel() {
  .reg .u32 %r<3>;
  add.u32 %r0, %r1, %r2;
}
)ptx";
  constexpr std::string_view checked_source = R"ptx(
.version 0.9
.target sm_80
.entry kernel() {
  .reg .u32 %r<3>;
  add.u32 %r0, %r1, %r2;
}
)ptx";
  const auto unchecked_ast = parseModule(unchecked_source);
  const auto checked_ast = parseModule(checked_source);
  ASSERT_TRUE(unchecked_ast);
  ASSERT_TRUE(checked_ast);
  const auto unchecked_module = resolveModule(*unchecked_ast);
  ASSERT_TRUE(unchecked_module.has_value())
      << unchecked_module.error().front().message;
  const auto expected =
      checkModuleAvailability(*checked_ast, *unchecked_module);
  ASSERT_FALSE(expected.has_value());
  ASSERT_EQ(expected.error().size(), 1u);
  // Retargeting preserves the IR's original instruction location, not AST-B's.
  EXPECT_EQ(expected.error().front().range,
            unchecked_module->functions.front().instruction_ranges.front());
  auto same_source_expected = expected.error().front();
  const auto& checked_function =
      std::get<syntax_ast::AstFunction>(checked_ast->items.back());
  same_source_expected.range =
      std::get<syntax_ast::AstInstruction>(checked_function.body.back()).range;

  const auto resolved = resolveModule(*checked_ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), expected.error().size());
  expectCheckerProjection(resolved.error().front(), same_source_expected);
  EXPECT_EQ(resolved.error().front().checker_kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

/** Native instruction-resolution errors retain no imported category or stage. */
TEST(ModuleDiagnostics, NativeResolutionFailureDefaultsToResolutionStage) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  match.all.sync.b32 _|_, %b1, 0xffffffff;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto& function =
      std::get<syntax_ast::AstFunction>(ast->items.front());
  const auto& instruction =
      std::get<syntax_ast::AstInstruction>(function.body.back());
  const auto resolved = resolveModule(*ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_FALSE(resolved.error().empty());
  const ResolveDiagnostic& diagnostic = resolved.error().front();
  EXPECT_EQ(diagnostic.stage(), ResolveDiagnosticStage::Resolution);
  EXPECT_EQ(diagnostic.range, sourceRange(instruction.operands.front()));
  EXPECT_FALSE(diagnostic.binding_kind);
  EXPECT_FALSE(diagnostic.declaration_kind);
  EXPECT_FALSE(diagnostic.checker_kind);
  EXPECT_FALSE(diagnostic.previous_range);
}

/** Module diagnostics own all data needed after parser, source, and AST destruction. */
TEST(ModuleDiagnostics, OwnsDiagnosticDataAfterAstAndSourceDestruction) {
  const ModuleResolveDiagnostics diagnostics = diagnosticsAfterInputDies();

  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().stage(), ResolveDiagnosticStage::Binding);
  EXPECT_EQ(diagnostics.front().binding_kind,
            binding::BindDiagnosticKind::UnresolvedReference);
  EXPECT_EQ(diagnostics.front().message,
            "Unresolved instruction operand '%missing'.");
  EXPECT_EQ(diagnostics.front().range, (SourceRange{{4, 17}, {4, 25}}));
  EXPECT_FALSE(diagnostics.front().previous_range);
}

/** Existing positional two- and four-field diagnostic initializers remain valid. */
TEST(ModuleDiagnostics, PreservesLegacyAggregateInitialization) {
  static_assert(std::is_aggregate_v<ResolveDiagnostic>);
  const ResolveDiagnostic native{SourceRange{{1, 1}, {1, 2}}, "native"};
  const ResolveDiagnostic semantic{
      SourceRange{{2, 1}, {2, 2}}, "semantic",
      declaration_semantics::DeclarationDiagnosticKind::InvalidAlignment,
      SourceRange{{1, 1}, {1, 2}}};

  EXPECT_EQ(native.stage(), ResolveDiagnosticStage::Resolution);
  EXPECT_EQ(semantic.stage(), ResolveDiagnosticStage::DeclarationSemantics);
  EXPECT_EQ(semantic.previous_range, (SourceRange{{1, 1}, {1, 2}}));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
