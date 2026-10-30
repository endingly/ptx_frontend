#include <gtest/gtest.h>

#include "ptx_source_identity.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/base/ptx_identifier.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Single-character punctuation identifiers must not become declarations. */
TEST(DeclarationIdentifiers, RejectsSingletonGlobalNames) {
  for (const std::string_view name : {"_", "$"}) {
    const std::string source = ".version 9.3\n.target sm_80\n.global .u32 " +
                               std::string(name) + ";\n";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast)) << name;
  }
}

/** Every concrete declaration context reports its precise invalid name leaf. */
TEST(DeclarationIdentifiers, RejectsNamesAcrossDeclarationContexts) {
  const std::vector<std::string> declarations{
      ".global .u32 #;",
      ".global .u32 #<2>;",
      ".entry #() { ret; }",
      ".func #();",
      ".entry k(.param .u32 #) { ret; }",
      ".func k(.reg .u32 #);",
      ".func (.reg .u32 #) k();",
      ".entry k() { .reg .u32 #; ret; }",
      ".entry k() { { .local .u32 #; } ret; }",
      ".entry k() { .param .u32 #; ret; }",
      ".entry k() { #: ret; }",
      ".entry k() { #: .callprototype _ (); ret; }",
      ".func target();\n.entry k() { #: .calltargets target; ret; }",
      ".entry k() { #: .branchtargets done; done: ret; }",
      ".func target();\n.func #();\n.alias #, target;",
      ".section .debug_str { #: .b8 0; };"};
  for (const auto& pattern : declarations) {
    for (const std::string_view name : {"_", "$"}) {
      std::string source = ".version 9.3\n.target sm_80\n" + pattern;
      std::ranges::replace(source, '#', name.front());
      SCOPED_TRACE(source);
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      const auto binding = binding::bindSymbols(*ast);
      const auto diagnostics =
          declaration_semantics::checkDeclarations(*ast, binding.table);
      const auto diagnostic = std::ranges::find(
          diagnostics,
          declaration_semantics::DeclarationDiagnosticKind::InvalidIdentifier,
          &declaration_semantics::DeclarationDiagnostic::kind);
      ASSERT_NE(diagnostic, diagnostics.end());
      size_t expected_count = 0;
      for (size_t offset = pattern.find('#'); offset != std::string::npos;
           offset = pattern.find('#', offset + 1)) {
        ++expected_count;
        const auto preceding = std::string_view(pattern).substr(0, offset);
        const int32_t line =
            3 + static_cast<int32_t>(std::ranges::count(preceding, '\n'));
        const auto newline = preceding.rfind('\n');
        const int32_t column = static_cast<int32_t>(
            newline == std::string_view::npos ? offset + 1 : offset - newline);
        const SourceRange range{{line, column}, {line, column + 1}};
        EXPECT_TRUE(std::ranges::any_of(diagnostics, [&](const auto& value) {
          return value.kind ==
                     declaration_semantics::DeclarationDiagnosticKind::
                         InvalidIdentifier &&
                 value.range == range;
        }));
      }
      EXPECT_EQ(std::ranges::count(
                    diagnostics,
                    declaration_semantics::DeclarationDiagnosticKind::
                        InvalidIdentifier,
                    &declaration_semantics::DeclarationDiagnostic::kind),
                expected_count);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

/** The public declaration checker uses AST spellings even with a stale table. */
TEST(DeclarationIdentifiers, ChecksAstNamesIndependentlyOfBinding) {
  PtxSyntaxParser parser(".global .u32 a;\n.global .u32 a;");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto bound = binding::bindSymbols(*ast);
  for (auto& item : ast->items)
    std::get<syntax_ast::AstVariableDeclaration>(item)
        .declarators[0]
        .name.syntax.text = "$";
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, bound.table);
  std::vector<SourceRange> ranges;
  for (const auto& diagnostic : diagnostics)
    if (diagnostic.kind ==
        declaration_semantics::DeclarationDiagnosticKind::InvalidIdentifier)
      ranges.push_back(diagnostic.range);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_EQ(ranges[0], (SourceRange{{1, 14}, {1, 15}}));
  EXPECT_EQ(ranges[1], (SourceRange{{2, 14}, {2, 15}}));
}

/** Prefixes remain legal in longer names and dummy/sink contexts stay distinct. */
TEST(DeclarationIdentifiers, PreservesLegalPrefixesAndContextualSinks) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(.version 9.3
.target sm_80
.global .u32 _x, $x, a, _g<2>, $g<2>;
.section .debug_str { debug_name: .b8 0; };
.file 1 "file.ptx"
.entry k() {
  .reg .b32 %r<2>;
  .reg .pred %p;
proto: .callprototype (.reg .u32 _) _ (.reg .u32 _);
  setp.eq.b32 _|%p, %r0, %r1;
  setp.eq.b32 %p|_, %r0, %r1;
  mov.v2.b32 {_,%r1}, {%r0,%r1};
  ret;
}
)ptx";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  EXPECT_TRUE(validateModule(*owned));
  for (const std::string source :
       {".entry k() { p: .callprototype _ (.reg .u32 $); ret; }",
        ".entry k() { p: .callprototype (.reg .u32 $) _ (); ret; }"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    const auto diagnostics =
        declaration_semantics::checkDeclarations(*ast, bound.table);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
      return diagnostic.kind ==
             declaration_semantics::DeclarationDiagnosticKind::
                 InvalidIdentifier;
    }));
  }
}

/** Coherent owned names fail legality after both source and AST have gone away. */
TEST(DeclarationIdentifiers, RevalidatesOwnedAndReplacementNames) {
  for (const std::string_view name : {"_", "$"}) {
    for (const std::string_view pattern :
         {".global .u32 #;", ".global .u32 #<2>;", ".entry #() { ret; }",
          ".section .debug_str { #: .b8 0; };"}) {
      const bool function = pattern.starts_with(".entry");
      std::optional<ResolvedModule> module;
      SourceRange expected;
      {
        const std::string prefix = ".version 9.3\n.target sm_80\n";
        std::string valid(pattern);
        valid[valid.find('#')] = 'a';
        PtxSyntaxParser parser(prefix + valid);
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast);
        auto result = resolveAndValidateModule(*ast);
        ASSERT_TRUE(result);
        module.emplace(std::move(*result));
        std::string invalid = valid;
        invalid[pattern.find('#')] = name.front();
        PtxSyntaxParser replacement(prefix + invalid);
        const auto replacement_ast = replacement.parseModule();
        ASSERT_TRUE(replacement_ast);
        ASSERT_TRUE(replacement_ast.diagnostics.empty());
        module->symbols = binding::bindSymbols(*replacement_ast).table;
        module->source_identity =
            detail::module_source_identity(*replacement_ast);
        if (function) {
          module->functions.front().name = name;
          module->functions.front().source_identity =
              detail::function_source_identity(
                  std::get<syntax_ast::AstFunction>(
                      replacement_ast->items.back()));
        }
        const auto symbol = std::ranges::find(module->symbols.symbols(), name,
                                              &binding::Symbol::name);
        ASSERT_NE(symbol, module->symbols.symbols().end());
        expected = symbol->declaration_range;
        const auto validation = validateModule(*replacement_ast, *module);
        ASSERT_FALSE(validation);
        EXPECT_EQ(validation.error().front().kind,
                  checker::CheckDiagnosticKind::RuleViolation);
        EXPECT_EQ(validation.error().front().range, expected);
      }
      const auto validation = validateModule(*module);
      ASSERT_FALSE(validation);
      EXPECT_EQ(validation.error().front().kind,
                checker::CheckDiagnosticKind::RuleViolation);
      EXPECT_EQ(validation.error().front().range, expected);
    }
  }
}

/** The shared grammar rejects malformed constructed spellings without normalization. */
TEST(DeclarationIdentifiers, RecognizesExactAsciiGrammar) {
  for (const std::string_view name :
       {"a", "Z", "_a", "$a", "%r", "__", "$$", "%_", "a0_$"})
    EXPECT_TRUE(base::isIdentifier(name)) << name;
  for (const std::string_view name :
       {"", "_", "$", "%", "0a", "a%b", "a.b", "a b", "é"})
    EXPECT_FALSE(base::isIdentifier(name)) << name;
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
