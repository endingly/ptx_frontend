#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <ptx_frontend/base/ptx_special_register.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

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
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

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
  EXPECT_FALSE(table.lookup(function_scope, "%r02").has_value());

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
      findReference(table, "start", binding::ReferenceKind::BranchTarget);
  ASSERT_NE(label, nullptr);
  ASSERT_TRUE(label->target.has_value());
  EXPECT_EQ(table.symbol(label->target->symbol).kind,
            binding::SymbolKind::Label);
}

TEST(PtxSymbolTable, BindsNestedBlocksLexicallyButKeepsControlMetadataLocal) {
  constexpr std::string_view source = R"ptx(
.func callee();
.entry kernel() {
  .reg .u32 %value;
  {
    .reg .u32 %value;
    .reg .u64 %function_pointer;
    inner_label:
    prototype: .callprototype _;
    targets: .calltargets callee;
    branches: .branchtargets inner_label;
    add.u32 %value, %value, %value;
    call %function_pointer, prototype;
    brx.idx %value, branches;
    bra inner_label;
  }
  { .reg .u32 %sibling; }
  add.u32 %value, %value, %value;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto binding_result = binding::bindSymbols(*module);
  ASSERT_TRUE(binding_result.diagnostics.empty());

  const auto kernel =
      binding_result.table.lookup(binding_result.table.moduleScope(), "kernel");
  ASSERT_TRUE(kernel.has_value());
  const auto function_scope =
      *binding_result.table.symbol(kernel->symbol).owned_scope;
  const auto& function =
      std::get<syntax_ast::AstFunction>(module->items[1]);
  const auto& first_block = *std::get<std::unique_ptr<syntax_ast::AstBlock>>(
      function.body[1]);
  const auto& second_block = *std::get<std::unique_ptr<syntax_ast::AstBlock>>(
      function.body[2]);
  const auto inner_scope =
      binding_result.table.blockScope(function_scope, first_block.range);
  const auto sibling_scope =
      binding_result.table.blockScope(function_scope, second_block.range);
  ASSERT_TRUE(inner_scope.has_value());
  ASSERT_TRUE(sibling_scope.has_value());
  EXPECT_EQ(binding_result.table.scope(*inner_scope).kind,
            binding::ScopeKind::Block);
  EXPECT_EQ(binding_result.table.scope(*inner_scope).parent, function_scope);

  const auto outer_value = binding_result.table.lookup(function_scope, "%value");
  const auto inner_value = binding_result.table.lookup(*inner_scope, "%value");
  ASSERT_TRUE(outer_value.has_value());
  ASSERT_TRUE(inner_value.has_value());
  EXPECT_NE(outer_value->symbol, inner_value->symbol);
  EXPECT_FALSE(binding_result.table.lookup(function_scope, "%sibling"));
  EXPECT_FALSE(binding_result.table.lookup(*inner_scope, "%sibling"));
  EXPECT_TRUE(binding_result.table.lookup(*sibling_scope, "%sibling"));
  EXPECT_EQ(binding_result.table.lookup(*sibling_scope, "%value")->symbol,
            outer_value->symbol);
  const auto expect_add_references = [&](const syntax_ast::AstInstruction& add,
                                         binding::SymbolId expected) {
    for (const auto& operand : add.operands) {
      const auto* identifier =
          std::get_if<syntax_ast::AstIdentifierRef>(&operand);
      ASSERT_NE(identifier, nullptr);
      const auto reference = std::ranges::find_if(
          binding_result.table.references(), [&](const auto& candidate) {
            return candidate.kind == binding::ReferenceKind::InstructionOperand &&
                   candidate.range == identifier->syntax.range;
          });
      ASSERT_NE(reference, binding_result.table.references().end());
      ASSERT_TRUE(reference->target.has_value());
      EXPECT_EQ(reference->target->symbol, expected);
    }
  };
  expect_add_references(
      std::get<syntax_ast::AstInstruction>(first_block.body[6]),
      inner_value->symbol);
  expect_add_references(
      std::get<syntax_ast::AstInstruction>(function.body[3]),
      outer_value->symbol);

  for (const auto [name, kind] : std::initializer_list<
           std::pair<std::string_view, binding::SymbolKind>>{
           {"inner_label", binding::SymbolKind::Label},
           {"prototype", binding::SymbolKind::CallPrototype},
           {"targets", binding::SymbolKind::CallTargetSet},
           {"branches", binding::SymbolKind::BranchTargetSet}}) {
    const auto symbol = binding_result.table.lookup(function_scope, name);
    ASSERT_TRUE(symbol.has_value()) << name;
    EXPECT_EQ(binding_result.table.symbol(symbol->symbol).kind, kind);
    EXPECT_EQ(binding_result.table.symbol(symbol->symbol).scope, function_scope);
  }
  for (const auto [name, kind] : std::initializer_list<
           std::pair<std::string_view, binding::ReferenceKind>>{
           {"prototype", binding::ReferenceKind::CallTargetSet},
           {"branches", binding::ReferenceKind::BranchTargetSet},
           {"inner_label", binding::ReferenceKind::BranchTarget}}) {
    const auto* reference = findReference(binding_result.table, name, kind);
    ASSERT_NE(reference, nullptr) << name;
    ASSERT_TRUE(reference->target.has_value()) << name;
    EXPECT_EQ(binding_result.table.symbol(reference->target->symbol).scope,
              function_scope);
  }
}

TEST(PtxSymbolTable, SupportsParameterizedNamesOutsideRegisterSpace) {
  PtxSyntaxParser parser(
      ".global .u32 item<2>; .entry kernel() { .reg .u32 %r0; "
      "ld.u32 %r0, item1; }");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

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

TEST(PtxSymbolTable, DiagnosesTrulyUnresolvedReferences) {
  PtxSyntaxParser parser(
      ".entry kernel() { .reg .u32 %r<1>; add.u32 %r0, %missing, 1; }");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  const auto* unresolved =
      findReference(binding_result.table, "%missing",
                    binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(unresolved, nullptr);
  EXPECT_FALSE(unresolved->target.has_value());
  EXPECT_EQ(unresolved->classification,
            binding::ReferenceClassification::Unresolved);
  ASSERT_EQ(binding_result.diagnostics.size(), 1u);
  EXPECT_EQ(binding_result.diagnostics.front().kind,
            binding::BindDiagnosticKind::UnresolvedReference);
  EXPECT_EQ(binding_result.diagnostics.front().message,
            "Unresolved instruction operand '%missing'.");
}

TEST(PtxSymbolTable, ClassifiesSpecialRegistersAndExternalSymbols) {
  constexpr std::string_view source = R"ptx(
.extern .global .u32 external_value;
.extern .func external_function();
.entry kernel() {
  .reg .u32 %r;
  add.u32 %r, external_value, %laneid;
  add.u32 %r, %envreg31, %pm7_64;
  add.u32 %r, %reserved_smem_offset_1, %tid.x;
  call external_function;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_TRUE(binding_result.diagnostics.empty());
  const auto external = binding_result.table.lookup(
      binding_result.table.moduleScope(), "external_value");
  ASSERT_TRUE(external.has_value());
  EXPECT_EQ(binding_result.table.symbol(external->symbol).linkage,
            binding::SymbolLinkage::External);
  const auto* external_reference =
      findReference(binding_result.table, "external_value",
                    binding::ReferenceKind::InstructionOperand);
  ASSERT_NE(external_reference, nullptr);
  EXPECT_EQ(external_reference->classification,
            binding::ReferenceClassification::ExternalSymbol);
  ASSERT_TRUE(external_reference->target.has_value());
  EXPECT_EQ(external_reference->target->symbol, external->symbol);
  const auto external_function = binding_result.table.lookup(
      binding_result.table.moduleScope(), "external_function");
  ASSERT_TRUE(external_function.has_value());
  EXPECT_EQ(binding_result.table.symbol(external_function->symbol).linkage,
            binding::SymbolLinkage::External);
  const auto* function_reference =
      findReference(binding_result.table, "external_function",
                    binding::ReferenceKind::CallTarget);
  ASSERT_NE(function_reference, nullptr);
  EXPECT_EQ(function_reference->classification,
            binding::ReferenceClassification::ExternalSymbol);

  for (const std::string_view spelling :
       {"%laneid", "%envreg31", "%pm7_64", "%reserved_smem_offset_1", "%tid"}) {
    const auto* reference =
        findReference(binding_result.table, spelling,
                      binding::ReferenceKind::InstructionOperand);
    ASSERT_NE(reference, nullptr) << spelling;
    EXPECT_EQ(reference->classification,
              binding::ReferenceClassification::SpecialRegister)
        << spelling;
    EXPECT_FALSE(reference->target.has_value()) << spelling;
  }
}

TEST(PtxSymbolTable, BindsDedicatedCallAndBranchReferences) {
  constexpr std::string_view source = R"ptx(
.func callee(.reg .u32 input);
.entry kernel() {
  .reg .u32 %result, %argument;
  .param .u32 parameter;
again:
  call (%result), callee, (%argument, parameter, 4);
  bra again;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_TRUE(binding_result.diagnostics.empty());
  for (const auto [spelling, kind] : std::initializer_list<
           std::pair<std::string_view, binding::ReferenceKind>>{
           {"%result", binding::ReferenceKind::CallReturnParameter},
           {"callee", binding::ReferenceKind::CallTarget},
           {"%argument", binding::ReferenceKind::CallArgument},
           {"parameter", binding::ReferenceKind::CallArgument},
           {"again", binding::ReferenceKind::BranchTarget}}) {
    const auto* reference = findReference(binding_result.table, spelling, kind);
    ASSERT_NE(reference, nullptr) << spelling;
    EXPECT_TRUE(reference->target.has_value()) << spelling;
  }
}

TEST(PtxSymbolTable, ClassifiesLocalCallParametersInTheirFunctionScope) {
  constexpr std::string_view source = R"ptx(
.func callee();
.func first() {
  .param .u32 staging;
  call callee, (staging);
}
.func second() { call callee; }
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);
  ASSERT_TRUE(binding_result.diagnostics.empty());
  const auto first = binding_result.table.lookup(
      binding_result.table.moduleScope(), "first");
  const auto second = binding_result.table.lookup(
      binding_result.table.moduleScope(), "second");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  const auto first_scope =
      *binding_result.table.symbol(first->symbol).owned_scope;
  const auto second_scope =
      *binding_result.table.symbol(second->symbol).owned_scope;
  const auto staging = binding_result.table.lookup(first_scope, "staging");
  ASSERT_TRUE(staging.has_value());
  EXPECT_EQ(binding_result.table.symbol(staging->symbol).kind,
            binding::SymbolKind::CallParameter);
  EXPECT_EQ(binding_result.table.symbol(staging->symbol).scope, first_scope);
  EXPECT_FALSE(binding_result.table.lookup(second_scope, "staging").has_value());
  const auto* argument = findReference(binding_result.table, "staging",
                                       binding::ReferenceKind::CallArgument);
  ASSERT_NE(argument, nullptr);
  ASSERT_TRUE(argument->target.has_value());
  EXPECT_EQ(argument->target->symbol, staging->symbol);
}

TEST(PtxSymbolTable, CollectsFunctionLocalControlFlowMetadataSymbols) {
  constexpr std::string_view source = R"ptx(
.func callee();
.func dispatch() {
  .reg .u32 %r0;
  prototype: .callprototype _;
  targets: .calltargets callee;
  branches: .branchtargets L0, N<5>;
  call callee, prototype;
  call callee, targets;
  brx.idx %r0, branches;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto first = binding::bindSymbols(*module);
  const auto second = binding::bindSymbols(*module);
  ASSERT_TRUE(first.diagnostics.empty());
  ASSERT_TRUE(second.diagnostics.empty());

  const auto dispatch =
      first.table.lookup(first.table.moduleScope(), "dispatch");
  ASSERT_TRUE(dispatch.has_value());
  const auto function_scope =
      *first.table.symbol(dispatch->symbol).owned_scope;
  for (const auto [name, kind] : std::initializer_list<
           std::pair<std::string_view, binding::SymbolKind>>{
           {"prototype", binding::SymbolKind::CallPrototype},
           {"targets", binding::SymbolKind::CallTargetSet},
           {"branches", binding::SymbolKind::BranchTargetSet}}) {
    const auto symbol = first.table.lookup(function_scope, name);
    const auto rebound = second.table.lookup(function_scope, name);
    ASSERT_TRUE(symbol.has_value()) << name;
    ASSERT_TRUE(rebound.has_value()) << name;
    EXPECT_EQ(first.table.symbol(symbol->symbol).kind, kind);
    EXPECT_EQ(first.table.symbol(symbol->symbol).scope, function_scope);
    EXPECT_EQ(symbol->symbol, rebound->symbol);
  }
  EXPECT_NE(first.table.lookup(function_scope, "prototype")->symbol,
            first.table.lookup(function_scope, "targets")->symbol);
  EXPECT_NE(first.table.lookup(function_scope, "prototype")->symbol,
            first.table.lookup(function_scope, "branches")->symbol);
  EXPECT_NE(first.table.lookup(function_scope, "targets")->symbol,
            first.table.lookup(function_scope, "branches")->symbol);
  EXPECT_FALSE(first.table.lookup(first.table.moduleScope(), "prototype")
                   .has_value());

  for (const std::string_view name : {"prototype", "targets"}) {
    const auto* reference =
        findReference(first.table, name, binding::ReferenceKind::CallTargetSet);
    ASSERT_NE(reference, nullptr);
    ASSERT_TRUE(reference->target.has_value());
    EXPECT_EQ(reference->classification,
              binding::ReferenceClassification::DeclaredSymbol);
  }
  const auto* branch_reference = findReference(
      first.table, "branches", binding::ReferenceKind::BranchTargetSet);
  ASSERT_NE(branch_reference, nullptr);
  ASSERT_TRUE(branch_reference->target.has_value());
  EXPECT_EQ(first.table.symbol(branch_reference->target->symbol).kind,
            binding::SymbolKind::BranchTargetSet);
}

TEST(PtxSymbolTable, DiagnosesNonCallMetadataTargetSetReference) {
  constexpr std::string_view source = R"ptx(
.func callee();
.func dispatch() {
  branches: .branchtargets L0;
  call callee, branches;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  ASSERT_EQ(binding_result.diagnostics.size(), 1u);
  EXPECT_EQ(binding_result.diagnostics.front().kind,
            binding::BindDiagnosticKind::InvalidReferenceTarget);
  EXPECT_EQ(binding_result.diagnostics.front().message,
            "Call target set 'branches' must name a .callprototype or "
            ".calltargets declaration.");
}

TEST(PtxSymbolTable, DiagnosesInvalidIndexedBranchTargetSetReference) {
  constexpr std::string_view source = R"ptx(
.global .u32 branches;
.entry kernel() {
  .reg .u32 %index;
  brx.idx %index, branches;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  ASSERT_EQ(binding_result.diagnostics.size(), 1u);
  EXPECT_EQ(binding_result.diagnostics.front().kind,
            binding::BindDiagnosticKind::InvalidReferenceTarget);
  EXPECT_EQ(binding_result.diagnostics.front().message,
            "Branch target set 'branches' must name a .branchtargets "
            "declaration in the current function.");
}

TEST(PtxSymbolTable, DiagnosesInvalidCallAndBranchTargetKinds) {
  constexpr std::string_view source = R"ptx(
.global .u32 global_value;
.func callee();
.entry kernel() {
  .reg .u32 %register;
  .local .u32 local_value;
  call global_value;
  call (local_value), callee, (global_value);
  bra %register;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::InvalidReferenceTarget;
                }),
            4u);
}

TEST(PtxSymbolTable, SpecialRegisterFamiliesHaveExactBounds) {
  EXPECT_TRUE(binding::isSpecialRegister("%tid.x"));
  EXPECT_TRUE(binding::isSpecialRegister("%envreg0"));
  EXPECT_TRUE(binding::isSpecialRegister("%envreg31"));
  EXPECT_TRUE(binding::isSpecialRegister("%pm0"));
  EXPECT_TRUE(binding::isSpecialRegister("%pm7_64"));
  EXPECT_TRUE(binding::isSpecialRegister("%reserved_smem_offset_1"));

  EXPECT_FALSE(binding::isSpecialRegister("%tid.q"));
  EXPECT_FALSE(binding::isSpecialRegister("%envreg32"));
  EXPECT_FALSE(binding::isSpecialRegister("%envreg01"));
  EXPECT_FALSE(binding::isSpecialRegister("%pm8"));
  EXPECT_FALSE(binding::isSpecialRegister("%pm00"));
  EXPECT_FALSE(binding::isSpecialRegister("%pm8_64"));
  EXPECT_FALSE(binding::isSpecialRegister("%reserved_smem_offset_2"));
  EXPECT_FALSE(binding::isSpecialRegister("%tid.w"));
  EXPECT_FALSE(binding::isSpecialRegister("%made_up"));
}

TEST(PtxSymbolTable, SpecialRegisterMetadataCarriesTypeShapeAndAvailability) {
  const auto laneid = base::lookup("%laneid");
  ASSERT_TRUE(laneid.has_value());
  EXPECT_EQ(laneid->id.kind, base::SpecialRegisterKind::LaneId);
  EXPECT_EQ(laneid->element_type, base::ScalarType::U32);
  EXPECT_EQ(laneid->vector_width, 1u);
  EXPECT_EQ(laneid->minimum_ptx_major, 1u);
  EXPECT_EQ(laneid->minimum_ptx_minor, 3u);
  EXPECT_EQ(laneid->minimum_sm, 0u);

  const auto tid = base::lookup("%tid");
  const auto tid_x = base::lookup("%tid.x");
  ASSERT_TRUE(tid.has_value());
  ASSERT_TRUE(tid_x.has_value());
  EXPECT_EQ(tid->element_type, base::ScalarType::U32);
  EXPECT_EQ(tid->id.kind, base::SpecialRegisterKind::Tid);
  EXPECT_EQ(tid_x->id, tid->id);
  EXPECT_EQ(tid->vector_width, 4u);
  EXPECT_EQ(tid_x->vector_width, 1u);
  EXPECT_EQ(tid_x->minimum_ptx_major, 2u);
  EXPECT_EQ(base::metadata(tid_x->id), *tid);

  const auto pm3 = base::lookup("%pm3");
  const auto pm4 = base::lookup("%pm4");
  ASSERT_TRUE(pm3.has_value());
  ASSERT_TRUE(pm4.has_value());
  EXPECT_EQ(pm3->id.kind,
            base::SpecialRegisterKind::PerformanceMonitor);
  EXPECT_EQ(pm3->id.index, 3u);
  EXPECT_EQ(pm4->id.index, 4u);
  EXPECT_EQ(pm3->minimum_ptx_major, 1u);
  EXPECT_EQ(pm3->minimum_ptx_minor, 3u);
  EXPECT_EQ(pm3->minimum_sm, 0u);
  EXPECT_EQ(pm4->minimum_ptx_major, 3u);
  EXPECT_EQ(pm4->minimum_sm, 20u);

  const auto cluster = base::lookup("%cluster_ctarank");
  ASSERT_TRUE(cluster.has_value());
  EXPECT_EQ(cluster->element_type, base::ScalarType::U32);
  EXPECT_EQ(cluster->minimum_ptx_major, 7u);
  EXPECT_EQ(cluster->minimum_ptx_minor, 8u);
  EXPECT_EQ(cluster->minimum_sm, 90u);
}

TEST(PtxSymbolTable, DiagnosesDuplicateSymbolsAndInvalidCount) {
  constexpr std::string_view source = R"ptx(
.global .u32 value;
.global .u32 value;
.extern .visible .global .u32 linked;
.entry kernel(.param .u32 input, .param .u32 input) {
  .reg .u32 %r<0>;
loop:
loop:
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::DuplicateSymbol;
                }),
            2);
  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::InvalidParameterizedCount;
                }),
            1);
  EXPECT_EQ(
      std::ranges::count_if(
          binding_result.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.kind ==
                   binding::BindDiagnosticKind::ConflictingLinkageQualifiers;
          }),
      1);
  for (const auto& diagnostic : binding_result.diagnostics) {
    if (diagnostic.kind == binding::BindDiagnosticKind::DuplicateSymbol)
      EXPECT_TRUE(diagnostic.previous_range.has_value());
  }
}

TEST(PtxSymbolTable, DiagnosesOverlappingParameterizedNameSets) {
  constexpr std::string_view source = R"ptx(
.global .u32 item<3>;
.global .u32 item2;
.global .u32 value2;
.global .u32 value<3>;
.global .u32 distinct<2>;
.global .u32 distinct;
.entry kernel() {
  .reg .u32 %r<11>;
  .reg .u32 %r1<2>;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  const auto duplicates = std::ranges::count_if(
      binding_result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.kind == binding::BindDiagnosticKind::DuplicateSymbol;
      });
  EXPECT_EQ(duplicates, 3u);
  for (const auto& diagnostic : binding_result.diagnostics) {
    EXPECT_EQ(diagnostic.kind, binding::BindDiagnosticKind::DuplicateSymbol);
    EXPECT_TRUE(diagnostic.previous_range.has_value());
    EXPECT_NE(diagnostic.message.find("overlapping symbol names"),
              std::string::npos);
  }
}

TEST(PtxSymbolTable, BindsDebugMetadataInItsOwnNamespace) {
  constexpr std::string_view source = R"ptx(
.file 1 "first.ptx"
.file 0x1U "same-id.ptx"
.section .debug_str {
debug_name:
  .b8 0;
};
.section .debug_str { };
.global .u32 debug_name;
.entry kernel() {
  .loc 0x1U 7 0
  {
    .loc 1 8 0, function_name .debug_str+0, inlined_at 0x1U 1 0;
    .loc 1 9 0, function_name debug_name, inlined_at 1 2 0;
  }
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  ASSERT_TRUE(binding_result.diagnostics.empty());
  const auto& table = binding_result.table;
  const auto debug_files = std::ranges::count_if(
      table.symbols(), [](const auto& symbol) {
        return symbol.kind == binding::SymbolKind::DebugFile;
      });
  EXPECT_EQ(debug_files, 1u);
  const auto debug_file = std::ranges::find_if(
      table.symbols(), [](const auto& symbol) {
        return symbol.kind == binding::SymbolKind::DebugFile;
      });
  ASSERT_NE(debug_file, table.symbols().end());
  EXPECT_EQ(debug_file->name, "1");

  const auto* first_file = findReference(
      table, "0x1U", binding::ReferenceKind::DebugFile);
  const auto* inline_file = findReference(
      table, "1", binding::ReferenceKind::DebugFile);
  ASSERT_NE(first_file, nullptr);
  ASSERT_NE(inline_file, nullptr);
  ASSERT_TRUE(first_file->target.has_value());
  ASSERT_TRUE(inline_file->target.has_value());
  EXPECT_EQ(first_file->target->symbol, debug_file->id);
  EXPECT_EQ(inline_file->target->symbol, debug_file->id);

  const auto* section_name = findReference(
      table, ".debug_str", binding::ReferenceKind::DebugFunctionName);
  const auto* label_name = findReference(
      table, "debug_name", binding::ReferenceKind::DebugFunctionName);
  ASSERT_NE(section_name, nullptr);
  ASSERT_NE(label_name, nullptr);
  ASSERT_TRUE(section_name->target.has_value());
  ASSERT_TRUE(label_name->target.has_value());
  EXPECT_EQ(table.symbol(section_name->target->symbol).kind,
            binding::SymbolKind::DebugStringLabel);
  EXPECT_EQ(table.symbol(label_name->target->symbol).kind,
            binding::SymbolKind::DebugStringLabel);

  const auto ordinary = table.lookup(table.moduleScope(), "debug_name");
  ASSERT_TRUE(ordinary.has_value());
  EXPECT_EQ(table.symbol(ordinary->symbol).kind,
            binding::SymbolKind::Variable);
  EXPECT_FALSE(table.lookup(table.moduleScope(), ".debug_str").has_value());
}

TEST(PtxSymbolTable, DiagnosesUnresolvedDebugMetadataAndDuplicateDebugLabel) {
  constexpr std::string_view source = R"ptx(
.file 1 "known.ptx"
.section .debug_info { other_name: };
.section .debug_str { duplicate: duplicate: };
.entry kernel() {
  .loc 2 1 0
  .loc 1 2 0, function_name other_name, inlined_at 1 1 0;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics, [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::UnresolvedReference;
                }),
            2);
  EXPECT_EQ(std::ranges::count_if(
                binding_result.diagnostics, [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::DuplicateSymbol;
                }),
            1);
  const auto* file = findReference(binding_result.table, "2",
                                   binding::ReferenceKind::DebugFile);
  const auto* name = findReference(binding_result.table, "other_name",
                                   binding::ReferenceKind::DebugFunctionName);
  ASSERT_NE(file, nullptr);
  ASSERT_NE(name, nullptr);
  EXPECT_FALSE(file->target.has_value());
  EXPECT_FALSE(name->target.has_value());
}

TEST(PtxSymbolTable, DiagnosesDebugFileIndexOutsideUint64) {
  PtxSyntaxParser parser(
      ".file 18446744073709551616U \"too-large.ptx\"\n"
      ".entry kernel() { }");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;

  const auto binding_result = binding::bindSymbols(*module);

  ASSERT_EQ(binding_result.diagnostics.size(), 1u);
  EXPECT_EQ(binding_result.diagnostics.front().kind,
            binding::BindDiagnosticKind::InvalidDebugFileId);
  EXPECT_EQ(binding_result.diagnostics.front().message,
            "Debug file index must be an unsigned 64-bit integer.");
}

}  // namespace
}  // namespace ptx_frontend
