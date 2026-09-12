#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Repeated destinations retain their index slots and the bound table identity. */
TEST(BranchTargetRepetition, PreservesExplicitAndCompactSequences) {
  const std::vector<std::pair<std::string, std::vector<std::string>>> cases{
      {"L0, L0, L1", {"L0", "L0", "L1"}},
      {"L<2>, L0", {"L0", "L1", "L0"}},
      {"L1, L<2>", {"L1", "L0", "L1"}},
      {"L<2>, L<2>", {"L0", "L1", "L0", "L1"}},
  };
  for (const auto& [entries, expected] : cases) {
    SCOPED_TRACE(entries);
    const std::string source =
        ".version 6.0\n.target sm_30\n.entry k() {\n.reg .u32 %idx;\n"
        "targets: .branchtargets " +
        entries +
        ";\nmov.u32 %idx, 1;\nbrx.idx %idx, targets;\nL0: ret;\nL1: ret;\n}";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto resolved = resolveModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    const auto& function = resolved->functions.front();
    const auto scope = resolved->symbols.symbol(function.symbol_id).owned_scope;
    ASSERT_TRUE(scope);
    const auto table_symbol = resolved->symbols.lookup(*scope, "targets");
    ASSERT_TRUE(table_symbol);
    ASSERT_EQ(function.body.size(), 4u);
    const auto& branch =
        std::get<Brx::Idx>(std::get<Brx>(function.body[1]).variant);
    EXPECT_EQ(branch.tlist.value.symbol_id, table_symbol->symbol);
    EXPECT_EQ(resolved->symbols.symbol(table_symbol->symbol).kind,
              binding::SymbolKind::BranchTargetSet);

    const auto& syntax_function =
        std::get<syntax_ast::AstFunction>(ast->items.back());
    const auto& table =
        std::get<syntax_ast::AstBranchTargets>(syntax_function.body[1]);
    // Expand only in this assertion: the public AST retains compact entries.
    std::vector<std::string> expanded;
    std::string retained_entries;
    for (const auto& entry : table.targets) {
      if (!retained_entries.empty())
        retained_entries += ", ";
      retained_entries += entry.name.syntax.text;
      if (entry.count) {
        retained_entries += "<" + entry.count->text + ">";
        for (unsigned index = 0; index < std::stoul(entry.count->text); ++index)
          expanded.push_back(entry.name.syntax.text + std::to_string(index));
      } else {
        expanded.push_back(entry.name.syntax.text);
      }
    }
    EXPECT_EQ(retained_entries, entries);
    EXPECT_EQ(expanded, expected);
    for (const auto& name : expanded) {
      const auto label = resolved->symbols.lookup(*scope, name);
      ASSERT_TRUE(label);
      EXPECT_EQ(resolved->symbols.symbol(label->symbol).kind,
                binding::SymbolKind::Label);
    }
  }
}

/** Repetition does not make missing or cross-function destinations valid. */
TEST(BranchTargetRepetition, RejectsMissingAndForeignLabels) {
  for (const std::string suffix : {"", ".func other() { Missing: ret; }"}) {
    SCOPED_TRACE(suffix);
    const std::string source =
        ".entry k() {\n.reg .u32 %idx;\n"
        "targets: .branchtargets Missing, Missing;\n"
        "brx.idx %idx, targets;\n}\n" +
        suffix;
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto& function =
        std::get<syntax_ast::AstFunction>(ast->items.front());
    const auto& table =
        std::get<syntax_ast::AstBranchTargets>(function.body[1]);
    const auto resolved = resolveModule(*ast);
    ASSERT_FALSE(resolved);
    ASSERT_EQ(resolved.error().size(), 2u);
    for (size_t index = 0; index < 2; ++index) {
      EXPECT_EQ(resolved.error()[index].declaration_kind,
                declaration_semantics::DeclarationDiagnosticKind::
                    UnresolvedMetadataTarget);
      EXPECT_EQ(resolved.error()[index].range, table.targets[index].range);
    }
  }
}

/** Repeated table members remain distinct from duplicate label definitions. */
TEST(BranchTargetRepetition, RejectsDuplicateLabelDefinitions) {
  PtxSyntaxParser parser(R"ptx(
.entry k() {
  targets: .branchtargets L0, L0;
L0: ret;
L0: ret;
}
)ptx");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved);
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().binding_kind,
            binding::BindDiagnosticKind::DuplicateSymbol);
  EXPECT_EQ(resolved.error().front().range.start.line, 5);
  ASSERT_TRUE(resolved.error().front().previous_range);
  EXPECT_EQ(resolved.error().front().previous_range->start.line, 4);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
