#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve owned metadata while releasing all source and AST ownership. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> commonModule(
    std::string source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModule(*ast);
}

/** Check whether an owned validation failed in a particular public category. */
bool hasKind(const checker::CheckResult& result,
             checker::CheckDiagnosticKind kind) {
  return !result && std::ranges::any_of(
                        result.error(),
                        [kind](const auto& item) { return item.kind == kind; });
}

/** Common syntax remains lossless while both syntax layers retain provenance. */
TEST(CommonLinkage, RetainsTriviaAndQualifierRanges) {
  constexpr std::string_view source =
      "// global definition\n.common /* retained */ .global .u32 value = 7;\n";
  PtxCstParser parser(source);
  const auto cst = parser.parseModule();
  ASSERT_TRUE(cst.has_value());
  ASSERT_TRUE(cst.diagnostics.empty());
  EXPECT_EQ(cst->sourceText(), source);
  const auto& declaration = std::get<syntax_cst::CstVariableDeclaration>(
      cst->module()->items.front());
  ASSERT_EQ(declaration.qualifiers.size(), 1u);
  const auto& qualifier = cst->token(declaration.qualifiers.front());
  EXPECT_EQ(qualifier.kind, TokenKind::DotCommon);
  EXPECT_EQ(qualifier.text, ".common");
  EXPECT_EQ(qualifier.range.start, (SourcePos{2, 1}));
  EXPECT_EQ(qualifier.range.end, (SourcePos{2, 8}));
  PtxSyntaxParser syntax(source);
  const auto ast = syntax.parseModule();
  ASSERT_TRUE(ast.has_value());
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto& variable =
      std::get<syntax_ast::AstVariableDeclaration>(ast->items.front());
  EXPECT_EQ(variable.qualifiers.front().text, ".common");
  EXPECT_EQ(variable.qualifiers.front().range, qualifier.range);
  const auto bound = binding::bindSymbols(*ast);
  ASSERT_TRUE(bound.diagnostics.empty());
  ASSERT_EQ(bound.table.symbols().size(), 1u);
  EXPECT_EQ(bound.table.symbols().front().linkage,
            binding::SymbolLinkage::Common);
}

/** The owned module retains Common after source destruction and checks it again. */
TEST(CommonLinkage, OwnsDefinitionsAndInitialization) {
  auto module = commonModule(R"ptx(.version 5.0
.target sm_20
.address_size 64
.common .global .u32 implicit_zero, explicit_zero = 0, nonzero = 7;
.common .global .align 16 .v4 .u32 packed[2] = {{1, 2}, {3}};
.common .global .u16 inferred[] = {4, 5};
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 5u);
  for (const auto& storage : module->storage_declarations) {
    EXPECT_EQ(storage.linkage, binding::SymbolLinkage::Common);
    EXPECT_EQ(storage.declaration_kind, StorageDeclarationKind::Definition);
    EXPECT_EQ(module->symbols.symbol(storage.symbol_id).linkage,
              binding::SymbolLinkage::Common);
  }
  EXPECT_EQ(module->storage_declarations[0].initialization,
            StorageInitializationKind::Zero);
  EXPECT_EQ(module->storage_declarations[1].initialization,
            StorageInitializationKind::Explicit);
  EXPECT_EQ(std::get<StorageConstant>(
                module->storage_declarations[2].initializer.front().value)
                .bits,
            7u);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Global linkage eligibility does not widen state-space, type, or layout rules. */
TEST(CommonLinkage, RejectsIneligibleSourceDeclarations) {
  for (const auto source : {
           ".common .const .u32 value;",
           ".common .shared .u32 value;",
           ".common .local .u32 value;",
           ".common .reg .u32 value;",
           ".common .param .u32 value;",
           ".common .global .texref value;",
           ".common .global .samplerref value;",
           ".common .global .surfref value;",
           ".common .tex .u64 value;",
           ".common .global .bf16 value;",
           ".common .global .pred value;",
           ".common .global .v4 .u64 value;",
           ".common .global .u32 value[];",
           ".common .func helper();",
           ".common .func helper() { ret; }",
           ".common .entry helper() { ret; }",
           ".common .weak .global .u32 value;",
           ".visible .common .global .u32 value;",
           ".extern .common .global .u32 value;",
           ".common .common .global .u32 value;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(commonModule(source));
  }
  PtxSyntaxParser parser(".entry f() { .common .global .u32 value; }");
  const auto body = parser.parseModule();
  EXPECT_TRUE(!body || !body.diagnostics.empty());
}

/** A programmatic body declaration cannot bypass module-only linkage grammar. */
TEST(CommonLinkage, RejectsConstructedBodyDeclaration) {
  PtxSyntaxParser parser(".entry f() { .global .u32 value; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value());
  auto& function = std::get<syntax_ast::AstFunction>(ast->items.front());
  auto& variable =
      std::get<syntax_ast::AstVariableDeclaration>(function.body.front());
  variable.qualifiers.push_back({".common", variable.range});
  EXPECT_FALSE(resolveModule(*ast));
}

/** Same-module duplicate definitions and incompatible linkage stay diagnosed. */
TEST(CommonLinkage, PreservesRedeclarationRules) {
  for (const auto other :
       {".common .global .u32 value;", ".weak .global .u32 value;",
        ".extern .global .u32 value;", ".common .global .u64 value;"}) {
    const auto result =
        commonModule(std::string{".common .global .u32 value;\n"} + other);
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().empty());
    EXPECT_TRUE(result.error().front().previous_range);
    EXPECT_EQ(result.error().front().declaration_kind,
              std::string_view{other} == ".common .global .u32 value;"
                  ? declaration_semantics::DeclarationDiagnosticKind::
                        MultipleDefinitions
                  : declaration_semantics::DeclarationDiagnosticKind::
                        IncompatibleRedeclaration);
  }
}

/** Known versions and targets are checked independently; fragments defer context. */
TEST(CommonLinkage, ChecksSourceVersionAndTargetBoundaries) {
  EXPECT_FALSE(
      commonModule(".version 4.3\n.target sm_20\n.common .global .u32 value;"));
  EXPECT_FALSE(
      commonModule(".version 5.0\n.target sm_13\n.common .global .u32 value;"));
  EXPECT_FALSE(commonModule(".target sm_13\n.common .global .u32 value;"));
  auto module = commonModule(".common .global .u32 value;");
  ASSERT_TRUE(module);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(hasKind(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext),
      checker::CheckDiagnosticKind::MissingValidationContext));
}

/** Target events select the latest region for each common declaration. */
TEST(CommonLinkage, ChecksOwnedMultiTargetOrderingAndVersion) {
  auto module = commonModule(R"ptx(.version 5.0
.target sm_20
.common .global .u32 first;
.target sm_30
.common .global .u32 second, third;
)ptx");
  ASSERT_TRUE(module);
  EXPECT_TRUE(validateModule(*module));
  module->header.regions[2].target_options.front() = "sm_13";
  const auto changed_target = validateModule(*module);
  ASSERT_FALSE(changed_target);
  EXPECT_EQ(std::ranges::count_if(
                changed_target.error(),
                [](const auto& item) {
                  return item.kind ==
                         checker::CheckDiagnosticKind::UnsupportedAvailability;
                }),
            2);
  EXPECT_TRUE(std::ranges::all_of(changed_target.error(), [](const auto& item) {
    return item.kind != checker::CheckDiagnosticKind::UnsupportedAvailability ||
           item.range.start.line == 5;
  }));
  module->header.regions[2].target_options.front() = "sm_30";
  module->header.regions[1].version = checker::PtxVersion{4, 3};
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::UnsupportedAvailability));
}

/** Replacement syntax selects availability without inheriting original targets. */
TEST(CommonLinkage, ChecksReplacementSourceContext) {
  constexpr std::string_view original =
      ".version 5.0\n.target sm_20\n.common .global .u32 value;";
  auto module = commonModule(std::string{original});
  ASSERT_TRUE(module);
  for (const auto source :
       {".version 4.3\n.target sm_20\n.common .global .u32 value;",
        ".version 5.0\n.target sm_13\n.common .global .u32 value;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_TRUE(hasKind(validateModule(*ast, *module),
                        checker::CheckDiagnosticKind::UnsupportedAvailability));
  }
}

/** Owned validation independently rejects linkage and eligibility corruption. */
TEST(CommonLinkage, RejectsForgedOwnedLinkageAndKinds) {
  auto module = commonModule(".common .global .u32 value;");
  ASSERT_TRUE(module);
  auto& storage = module->storage_declarations.front();
  storage.linkage = binding::SymbolLinkage::Weak;
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
  storage.linkage = binding::SymbolLinkage::Common;
  storage.element_type = StorageOpaqueType::Texture;
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
  module = commonModule(".global .texref texture;");
  ASSERT_TRUE(module);
  module->storage_declarations.front().linkage = binding::SymbolLinkage::Common;
  auto& opaque_symbol = const_cast<binding::Symbol&>(
      module->symbols.symbol(module->storage_declarations.front().symbol_id));
  opaque_symbol.linkage = binding::SymbolLinkage::Common;
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
  module = commonModule(".const .u32 value;");
  ASSERT_TRUE(module);
  module->storage_declarations.front().linkage = binding::SymbolLinkage::Common;
  auto& constant_symbol = const_cast<binding::Symbol&>(
      module->symbols.symbol(module->storage_declarations.front().symbol_id));
  constant_symbol.linkage = binding::SymbolLinkage::Common;
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
  module = commonModule(".common .global .u32 value;");
  ASSERT_TRUE(module);
  module->storage_declarations.clear();
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
  module = commonModule(".func helper();");
  ASSERT_TRUE(module);
  auto& function_symbol = const_cast<binding::Symbol&>(
      module->symbols.symbol(module->functions.front().symbol_id));
  function_symbol.linkage = binding::SymbolLinkage::Common;
  module->functions.front().contract.linkage = binding::SymbolLinkage::Common;
  EXPECT_TRUE(hasKind(validateModule(*module),
                      checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
