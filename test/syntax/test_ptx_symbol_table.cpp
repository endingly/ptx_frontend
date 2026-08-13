#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

#include "ptx_ir/bind/ptx_symbol_table.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend {
namespace {

const binding::SymbolReference* findReference(const binding::SymbolTable& table,
                                              std::string_view spelling,
                                              binding::ReferenceKind kind) {
  const auto iterator = std::ranges::find_if(
      table.references(), [spelling, kind](const auto& reference) {
        return reference.spelling == spelling && reference.kind == kind;
      });
  return iterator == table.references().end() ? nullptr : &*iterator;
}

TEST(PtxSymbolTable, CollectsScopesAndBindsLexicalReferences) {
  constexpr std::string_view source = R"ptx(
.global .u32 count = 2;
.global .u32 values[count];
.global .u64 pointer = generic(values);
.entry kernel(.param .u32 input) {
  .reg .u32 %r<3>;
  .reg .pred %p;
  .local .u32 values;
start:
  @%p add.u32 %r0, values, input;
  bra start;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.error().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_TRUE(binding_result.diagnostics.empty());
  const auto& table = binding_result.table;
  ASSERT_EQ(table.scopes().size(), 2u);
  EXPECT_EQ(table.scope(table.moduleScope()).kind, binding::ScopeKind::Module);
  EXPECT_EQ(table.symbols().size(), 9u);

  const auto count = table.lookup(table.moduleScope(), "count");
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(table.symbol(count->symbol).kind, binding::SymbolKind::Variable);
  EXPECT_EQ(table.symbol(count->symbol).state_space,
            syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(table.symbol(count->symbol).type, ".u32");

  const auto kernel = table.lookup(table.moduleScope(), "kernel");
  ASSERT_TRUE(kernel.has_value());
  const auto& kernel_symbol = table.symbol(kernel->symbol);
  ASSERT_TRUE(kernel_symbol.owned_scope.has_value());
  const binding::ScopeId function_scope = *kernel_symbol.owned_scope;
  EXPECT_EQ(table.scope(function_scope).parent, table.moduleScope());

  const auto bank_member = table.lookup(function_scope, "%r2");
  ASSERT_TRUE(bank_member.has_value());
  EXPECT_EQ(bank_member->parameterized_index, 2u);
  EXPECT_EQ(table.symbol(bank_member->symbol).name, "%r");
  EXPECT_FALSE(table.lookup(function_scope, "%r3").has_value());

  const auto local_values = table.lookup(function_scope, "values");
  ASSERT_TRUE(local_values.has_value());
  EXPECT_EQ(table.symbol(local_values->symbol).scope, function_scope);
  EXPECT_NE(local_values->symbol,
            table.lookup(table.moduleScope(), "values")->symbol);

  const auto* dimension =
      findReference(table, "count", binding::ReferenceKind::ArrayDimension);
  ASSERT_NE(dimension, nullptr);
  ASSERT_TRUE(dimension->target.has_value());
  EXPECT_EQ(dimension->target->symbol, count->symbol);

  const auto* initializer =
      findReference(table, "values", binding::ReferenceKind::Initializer);
  ASSERT_NE(initializer, nullptr);
  ASSERT_TRUE(initializer->target.has_value());
  EXPECT_EQ(initializer->target->symbol,
            table.lookup(table.moduleScope(), "values")->symbol);
  EXPECT_EQ(std::ranges::count_if(table.references(),
                                  [](const auto& reference) {
                                    return reference.spelling == "generic";
                                  }),
            0);

  const auto* register_reference =
      findReference(table, "%r0", binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(register_reference, nullptr);
  ASSERT_TRUE(register_reference->target.has_value());
  EXPECT_EQ(register_reference->target->symbol, bank_member->symbol);
  EXPECT_EQ(register_reference->target->parameterized_index, 0u);

  const auto* predicate =
      findReference(table, "%p", binding::ReferenceKind::Predicate);
  ASSERT_NE(predicate, nullptr);
  ASSERT_TRUE(predicate->target.has_value());
  EXPECT_EQ(table.symbol(predicate->target->symbol).name, "%p");

  const auto* local_reference = findReference(
      table, "values", binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(local_reference, nullptr);
  ASSERT_TRUE(local_reference->target.has_value());
  EXPECT_EQ(local_reference->target->symbol, local_values->symbol);

  const auto* label =
      findReference(table, "start", binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(label, nullptr);
  ASSERT_TRUE(label->target.has_value());
  EXPECT_EQ(table.symbol(label->target->symbol).kind,
            binding::SymbolKind::Label);
}

TEST(PtxSymbolTable, SupportsParameterizedNamesOutsideRegisterSpace) {
  PtxSyntaxParser parser(
      ".global .u32 item<2>; .entry kernel() { ld.u32 %r0, item1; }");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.error().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_TRUE(binding_result.diagnostics.empty());
  const auto kernel =
      binding_result.table.lookup(binding_result.table.moduleScope(), "kernel");
  ASSERT_TRUE(kernel.has_value());
  const auto function_scope =
      *binding_result.table.symbol(kernel->symbol).owned_scope;
  const auto item = binding_result.table.lookup(function_scope, "item1");
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->parameterized_index, 1u);
  EXPECT_EQ(binding_result.table.symbol(item->symbol).state_space,
            syntax_ast::AstStateSpace::Global);
}

TEST(PtxSymbolTable, RetainsUnresolvedReferencesForLaterDiagnostics) {
  PtxSyntaxParser parser(
      ".entry kernel() { .reg .u32 %r<1>; add.u32 %r0, %missing, 1; }");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.error().message;

  const auto binding_result = binding::bindSymbols(*module);

  const auto* unresolved =
      findReference(binding_result.table, "%missing",
                    binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(unresolved, nullptr);
  EXPECT_FALSE(unresolved->target.has_value());
}

TEST(PtxSymbolTable, DiagnosesDuplicateSymbolsAndInvalidCount) {
  constexpr std::string_view source = R"ptx(
.global .u32 value;
.global .u32 value;
.entry kernel(.param .u32 input, .param .u32 input) {
  .reg .u32 %r<0>;
loop:
loop:
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.error().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::DuplicateSymbol;
                }),
            3);
  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::InvalidParameterizedCount;
                }),
            1);
  for (const auto& diagnostic : binding_result.diagnostics) {
    if (diagnostic.kind == binding::BindDiagnosticKind::DuplicateSymbol)
      EXPECT_TRUE(diagnostic.previous_range.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend
