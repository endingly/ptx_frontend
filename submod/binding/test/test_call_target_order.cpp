#include <gtest/gtest.h>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend {
namespace {
/** Call target events retain lexical declaration order for stable function identities. */
TEST(CallTargetOrder, IndexesExactCalleeUsesAcrossDeclarationsAndRecursion) {
  for (const auto& pair :
       {std::pair{".func f(); .entry kernel() { call f; } .func f() { ret; }",
                  true},
        std::pair{".func f() { ret; } .entry kernel() { call f; }", true},
        std::pair{".func self() { call self; ret; }", true},
        std::pair{".entry kernel() { call f; } .extern .func f();", false},
        std::pair{".entry kernel() { call f; } .func f() { ret; }", false},
        std::pair{".func b(); .func a() { call b; } .func b() { call a; }",
                  true}}) {
    SCOPED_TRACE(pair.first);
    PtxSyntaxParser parser(pair.first);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    ASSERT_TRUE(bound.diagnostics.empty());
    size_t uses = 0;
    for (const auto& reference : bound.table.references()) {
      if (reference.kind != binding::ReferenceKind::CallTarget)
        continue;
      ++uses;
      ASSERT_TRUE(reference.target);
      EXPECT_EQ(bound.table.hasPriorDeclaration(reference.target->symbol,
                                                reference.range),
                pair.second);
      EXPECT_FALSE(
          bound.table.hasPriorDeclaration(reference.target->symbol, {}));
    }
    EXPECT_GT(uses, 0u);
  }
}

/** Repeated exact ranges have no ordering context, even when coordinates look earlier. */
TEST(CallTargetOrder, RejectsAmbiguousUseAndDeclarationEvents) {
  PtxSyntaxParser parser(".func f(); .entry kernel() { call f; call f; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
  auto& first = std::get<syntax_ast::AstCallTarget>(
      std::get<syntax_ast::AstInstruction>(function.body[0]).operands[0]);
  auto& second = std::get<syntax_ast::AstCallTarget>(
      std::get<syntax_ast::AstInstruction>(function.body[1]).operands[0]);
  std::get<syntax_ast::AstIdentifierRef>(second.name).syntax.range =
      std::get<syntax_ast::AstIdentifierRef>(first.name).syntax.range;
  second.range = first.range;
  const auto bound = binding::bindSymbols(*ast);
  const auto symbol = bound.table.lookup(bound.table.moduleScope(), "f");
  ASSERT_TRUE(symbol);
  EXPECT_FALSE(
      bound.table
          .hasPriorDeclaration(
              symbol->symbol,
              std::get<syntax_ast::AstIdentifierRef>(first.name).syntax.range)
          .has_value());
  PtxSyntaxParser declarations(
      ".func f(); .func f(); .entry kernel() { call f; }");
  auto repeated = declarations.parseModule();
  ASSERT_TRUE(repeated);
  auto& prior = std::get<syntax_ast::AstFunction>(repeated->items[0]);
  const auto& later = std::get<syntax_ast::AstFunction>(repeated->items[1]);
  prior.name.syntax.range = later.name.syntax.range;
  const auto ambiguous = binding::bindSymbols(*repeated);
  const auto target =
      ambiguous.table.lookup(ambiguous.table.moduleScope(), "f");
  ASSERT_TRUE(target);
  const auto& caller =
      std::get<syntax_ast::AstFunction>(repeated->items.back());
  const auto& use = std::get<syntax_ast::AstCallTarget>(
      std::get<syntax_ast::AstInstruction>(caller.body[0]).operands[0]);
  EXPECT_FALSE(
      ambiguous.table
          .hasPriorDeclaration(
              target->symbol,
              std::get<syntax_ast::AstIdentifierRef>(use.name).syntax.range)
          .has_value());
}
/** The whole target range is authoritative even when malformed name syntax differs. */
TEST(CallTargetOrder, IndexesTheRangeRetainedByResolvedCallTargets) {
  PtxSyntaxParser parser(".func f(); .entry kernel() { call f; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto& function = std::get<syntax_ast::AstFunction>(ast->items.back());
  auto& target = std::get<syntax_ast::AstCallTarget>(
      std::get<syntax_ast::AstInstruction>(function.body.front())
          .operands.front());
  std::get<syntax_ast::AstIdentifierRef>(target.name).syntax.range = {{900, 1},
                                                                      {900, 2}};
  const auto bound = binding::bindSymbols(*ast);
  const auto symbol = bound.table.lookup(bound.table.moduleScope(), "f");
  ASSERT_TRUE(symbol);
  EXPECT_EQ(bound.table.hasPriorDeclaration(symbol->symbol, target.range),
            true);
  EXPECT_FALSE(
      bound.table
          .hasPriorDeclaration(
              symbol->symbol,
              std::get<syntax_ast::AstIdentifierRef>(target.name).syntax.range)
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend
