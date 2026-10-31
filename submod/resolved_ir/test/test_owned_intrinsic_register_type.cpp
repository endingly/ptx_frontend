#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Match the mixed arithmetic availability independently of source storage. */
checker::Context intrinsic_context() {
  return {.target = {.ptx_version = {9, 3},
                     .sm_version = 100,
                     .identity = *base::parse_target_identity("sm_100")}};
}
/** Keep the used one-ADD owner separate from the independently initialized control. */
std::string intrinsic_source(bool unused = false) {
  return ".version 9.3\n.target sm_100\n.entry kernel(){.reg .b16 %b; "
         ".reg .f32 %f,%dst; " +
         std::string(unused ? "mov.f32 %f,0f3f800000; add.f32 %dst,%f,%f;"
                            : "add.f32.bf16 %dst,%b,%f;") +
         "}";
}
/** Replace only the named variable's declaration type in a public mutable AST. */
void set_intrinsic_type(syntax_ast::AstModule& ast, std::string_view spelling) {
  size_t changed = 0;
  for (auto& item : ast.items)
    if (auto* function = std::get_if<syntax_ast::AstFunction>(&item))
      for (auto& body : function->body)
        if (auto* declaration =
                std::get_if<syntax_ast::AstVariableDeclaration>(&body))
          for (const auto& variable : declaration->declarators)
            if (variable.name.syntax.text == "%b") {
              declaration->type.text = spelling;
              ++changed;
            }
  EXPECT_EQ(changed, 1u);
}
/** Count actual cached target references without interpreting source spelling. */
struct IntrinsicReferenceCount final : detail::IReferenceObserver {
  /** Stable target from the current owner's public symbol table. */
  binding::SymbolId target;
  /** Number of actual scalar cached references to that target. */
  size_t count = 0;
  /** Count a direct destination/reference without relying on spelling. */
  void reg(const ResolvedRegisterRef& ref, std::span<const SourceRange>,
           checker::AddressSymbolResolutionPolicy) override {
    if (ref.symbol_id == target)
      ++count;
  }
  /** Observe every register alternative of the ADD source fields. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy policy) override {
    if (const auto* ref = std::get_if<ResolvedRegisterRef>(&value))
      reg(*ref, ranges, policy);
  }
  /** Include scalar MOV sources as well as its direct destination callback. */
  void mov_source(const ResolvedMovSource& value,
                  std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy policy) override {
    if (const auto* ref = std::get_if<ResolvedRegisterRef>(&value))
      reg(*ref, ranges, policy);
  }
};
/** Produce the current owner with stable public table rebinding and no body edits. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> intrinsic_owner(
    bool unused, std::string_view declaration = ".b16",
    std::optional<ScalarType> actual = ScalarType::B16) {
  auto ast = test_helpers::parseModule(intrinsic_source(unused));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto module = resolveAndValidateModule(*ast);
  if (!module)
    return module;
  const auto scope =
      module->symbols.symbol(module->functions.front().symbol_id).owned_scope;
  EXPECT_TRUE(scope);
  if (!scope)
    return std::unexpected(ModuleResolveDiagnostics{});
  const auto old = module->symbols.lookup(*scope, "%b");
  EXPECT_TRUE(old);
  if (!old)
    return std::unexpected(ModuleResolveDiagnostics{});
  const auto old_symbol = module->symbols.symbol(old->symbol);
  const auto old_identity = module->source_identity;
  const auto old_ranges = module->functions.front().instruction_ranges;
  const auto old_opcodes = module->functions.front().instruction_opcodes;
  std::vector<const Instruction*> body_pointers;
  for (const auto& instruction : module->functions.front().body)
    body_pointers.push_back(instruction.get());
  const auto old_function_identity = module->functions.front().source_identity;
  const auto old_references = module->symbols.references().size();
  size_t target_references = 0;
  for (const auto& reference : module->symbols.references())
    if (reference.target && reference.target->symbol == old_symbol.id)
      ++target_references;
  EXPECT_EQ(target_references, unused ? 0u : 1u);
  IntrinsicReferenceCount before_count;
  before_count.target = old_symbol.id;
  for (const auto& instruction : module->functions.front().body)
    instruction->visit_references(before_count);
  EXPECT_EQ(before_count.count, unused ? 0u : 1u);
  set_intrinsic_type(*ast, declaration);
  auto rebound = binding::bindSymbols(*ast);
  EXPECT_TRUE(rebound.diagnostics.empty());
  const auto& changed = rebound.table.symbol(old_symbol.id);
  EXPECT_EQ(changed.id, old_symbol.id);
  EXPECT_EQ(changed.scope, old_symbol.scope);
  EXPECT_EQ(changed.declaration_range, old_symbol.declaration_range);
  EXPECT_EQ(changed.name, old_symbol.name);
  EXPECT_EQ(changed.kind, old_symbol.kind);
  EXPECT_EQ(changed.state_space, old_symbol.state_space);
  EXPECT_EQ(changed.vector_width, old_symbol.vector_width);
  EXPECT_EQ(changed.parameterized_count, old_symbol.parameterized_count);
  EXPECT_EQ(rebound.table.references().size(), old_references);
  EXPECT_EQ(rebound.table.declarationOccurrences(changed.id).size(),
            module->symbols.declarationOccurrences(changed.id).size());
  EXPECT_EQ(rebound.table.scopes().size(), module->symbols.scopes().size());
  for (size_t index = 0; index < rebound.table.scopes().size(); ++index) {
    const auto& before = module->symbols.scopes()[index];
    const auto& after = rebound.table.scopes()[index];
    EXPECT_EQ(before.id, after.id);
    EXPECT_EQ(before.kind, after.kind);
    EXPECT_EQ(before.parent, after.parent);
    EXPECT_EQ(before.owner, after.owner);
    EXPECT_EQ(before.range, after.range);
  }
  for (size_t index = 0; index < old_references; ++index) {
    const auto& before = module->symbols.references()[index];
    const auto& after = rebound.table.references()[index];
    EXPECT_EQ(before.scope, after.scope);
    EXPECT_EQ(before.kind, after.kind);
    EXPECT_EQ(before.spelling, after.spelling);
    EXPECT_EQ(before.range, after.range);
    EXPECT_EQ(before.classification, after.classification);
    EXPECT_EQ(before.target.has_value(), after.target.has_value());
    if (before.target && after.target) {
      EXPECT_EQ(before.target->symbol, after.target->symbol);
      EXPECT_EQ(before.target->parameterized_index,
                after.target->parameterized_index);
    }
  }
  const auto occurrences = module->symbols.declarationOccurrences(changed.id);
  const auto rebound_occurrences =
      rebound.table.declarationOccurrences(changed.id);
  for (size_t index = 0; index < occurrences.size(); ++index) {
    EXPECT_EQ(occurrences[index].symbol, rebound_occurrences[index].symbol);
    EXPECT_EQ(occurrences[index].range, rebound_occurrences[index].range);
    EXPECT_EQ(occurrences[index].lexical_order,
              rebound_occurrences[index].lexical_order);
    EXPECT_EQ(occurrences[index].variable_declaration_range,
              rebound_occurrences[index].variable_declaration_range);
  }
  module->symbols = std::move(rebound.table);
  if (!unused) {
    auto& add =
        dynamic_cast<AddMixedF32&>(*module->functions.front().body.front());
    auto& ref = std::get<ResolvedRegisterRef>(add.src.value);
    EXPECT_EQ(ref.symbol_id, old_symbol.id);
    EXPECT_EQ(ref.declared_type, ScalarType::B16);
    ref.declared_type = actual;
  }
  IntrinsicReferenceCount after_count;
  after_count.target = old_symbol.id;
  for (const auto& instruction : module->functions.front().body)
    instruction->visit_references(after_count);
  EXPECT_EQ(after_count.count, before_count.count);
  EXPECT_EQ(module->source_identity, old_identity);
  EXPECT_EQ(module->functions.front().instruction_ranges, old_ranges);
  EXPECT_EQ(module->functions.front().instruction_opcodes, old_opcodes);
  EXPECT_EQ(module->functions.front().source_identity, old_function_identity);
  EXPECT_EQ(module->functions.front().body.size(), body_pointers.size());
  for (size_t index = 0; index < body_pointers.size(); ++index)
    EXPECT_EQ(module->functions.front().body[index].get(),
              body_pointers[index]);
  return module;
}
TEST(OwnedIntrinsicRegisterType, KeepsInitializedAndSourceDeclarationControls) {
  const std::string source =
      ".version 9.3\n.target sm_100\n.entry kernel(){"
      ".reg .b16 %b; .reg .f32 %f<2>; mov.f32 %f0,0f3f800000; "
      "cvt.rn.bf16.f32 %b,%f0; add.f32.bf16 %f1,%b,%f0;}";
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_TRUE(resolveAndValidateModule(*ast));
  auto invalid_source = intrinsic_source();
  invalid_source.replace(invalid_source.find(".reg .b16"), 9, ".reg .bf16");
  const auto invalid = test_helpers::parseModule(invalid_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
  const auto rejected = resolveModuleOnly(*invalid);
  ASSERT_FALSE(rejected);
  EXPECT_TRUE(std::ranges::any_of(rejected.error(), [](const auto& diagnostic) {
    return diagnostic.declaration_kind ==
           declaration_semantics::DeclarationDiagnosticKind::
               UnsupportedRegisterDeclarationType;
  }));
}
TEST(OwnedIntrinsicRegisterType,
     RejectsAffirmativeImpossibleDirectDeclaration) {
  PtxSyntaxParser parser("add.f32.bf16 %f0,%b1,%f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto instruction = resolveInstruction(*ast);
  ASSERT_TRUE(instruction);
  auto& add = dynamic_cast<AddMixedF32&>(**instruction);
  auto& ref = std::get<ResolvedRegisterRef>(add.src.value);
  ASSERT_FALSE(ref.symbol_id);
  EXPECT_FALSE(ref.declared_type);
  EXPECT_TRUE(add.check(intrinsic_context()));
  ref.declared_type = ScalarType::B16;
  EXPECT_TRUE(add.check(intrinsic_context()));
  ref.declared_type = ScalarType::BF16;
  const auto checked = add.check(intrinsic_context());
  ASSERT_FALSE(checked);
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.kind ==
               checker::CheckDiagnosticKind::OperandTypeMismatch &&
           diagnostic.range == add.src.locs.front() &&
           diagnostic.message.find("fundamental") != std::string::npos;
  }));
}
TEST(OwnedIntrinsicRegisterType,
     RechecksUsedAndUnusedCurrentOwnedDeclarations) {
  for (const bool unused : {false, true}) {
    auto valid = intrinsic_owner(unused);
    ASSERT_TRUE(valid);
    EXPECT_TRUE(validateModule(*valid));
    auto invalid = intrinsic_owner(unused, ".bf16", ScalarType::BF16);
    ASSERT_TRUE(invalid);
    EXPECT_FALSE(validateModule(*invalid));
    const auto original_ast =
        test_helpers::parseModule(intrinsic_source(unused));
    ASSERT_MODULE_PARSE_SUCCEEDS(original_ast);
    const auto with_source = validateModule(*original_ast, *invalid);
    ASSERT_FALSE(with_source);
    EXPECT_TRUE(
        std::ranges::any_of(with_source.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                     checker::CheckDiagnosticKind::RuleViolation &&
                 diagnostic.message.find(".reg declaration") !=
                     std::string::npos;
        }));
    ResolvedModule copied(*invalid), assigned;
    assigned = copied;
    EXPECT_FALSE(validateModule(copied));
    EXPECT_FALSE(validateModule(assigned));
    ResolvedFunction function(invalid->functions.front()), function_assigned;
    function_assigned = function;
    assigned.functions.front() = std::move(function_assigned);
    assigned.functions.front().body.back() = function.body.back()->clone();
    EXPECT_FALSE(validateModule(assigned));
    invalid->symbols = valid->symbols;
    if (!unused)
      std::get<ResolvedRegisterRef>(
          dynamic_cast<AddMixedF32&>(*invalid->functions.front().body.front())
              .src.value)
          .declared_type = ScalarType::B16;
    EXPECT_TRUE(validateModule(*invalid));
  }
}
/** Require a declaration-eligibility error at the actual direct operand. */
void expect_intrinsic_reference_error(const checker::CheckResult& checked,
                                      SourceRange range) {
  ASSERT_FALSE(checked);
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.kind ==
               checker::CheckDiagnosticKind::OperandTypeMismatch &&
           diagnostic.range == range &&
           diagnostic.message.find("fundamental") != std::string::npos;
  }));
  EXPECT_TRUE(std::ranges::none_of(checked.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}
TEST(OwnedIntrinsicRegisterType,
     SeparatesActualEligibilityFromContextualTypeFacts) {
  const std::array ranges{SourceRange{{1, 2}, {1, 4}}};
  ResolvedRegisterRef ref{.spelling = "r",
                          .register_class = ResolvedRegisterClass::General};
  checker::OperandDescriptor descriptor{
      .target_field_id = "source",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::BF16},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Register};
  checker::OperandView view{.field_id = "source",
                            .actual_shape = checker::OperandShape::Register,
                            .register_type = ScalarType::BF16,
                            .register_ref = &ref,
                            .locations = ranges};
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {},
                                   intrinsic_context());
  };
  EXPECT_TRUE(check());
  ref.declared_type = ScalarType::B16;
  EXPECT_TRUE(
      check());  // The contextual BF16 cache is not a physical declaration.
  ref.declared_type = ScalarType::BF16;
  expect_intrinsic_reference_error(check(), ranges.front());
  view.register_type = ScalarType::B16;
  expect_intrinsic_reference_error(check(), ranges.front());
  size_t fundamental = 0, instruction_only = 0;
  for (const auto& metadata : base::scalar_type_metadata()) {
    if (metadata.type == ScalarType::Pred)
      continue;  // Predicate roles have their own shape.
    ref.declared_type = metadata.type;
    view.register_type = metadata.type;
    descriptor.type_expression.fixed_scalar_type = metadata.type;
    if (metadata.register_declaration_usage ==
        base::ScalarDeclarationUsage::Fundamental) {
      ++fundamental;
      EXPECT_TRUE(check()) << metadata.source_spelling;
    } else {
      ++instruction_only;
      expect_intrinsic_reference_error(check(), ranges.front());
    }
  }
  EXPECT_GT(fundamental, 0u);
  EXPECT_GT(instruction_only, 0u);
  for (const auto type : {ScalarType::Invalid, static_cast<ScalarType>(255)}) {
    ref.declared_type = type;
    view.register_type = ScalarType::BF16;
    descriptor.type_expression.fixed_scalar_type = ScalarType::BF16;
    expect_intrinsic_reference_error(check(), ranges.front());
  }
}
TEST(OwnedIntrinsicRegisterType,
     ChecksFixedAndDynamicLeavesWithoutBanningLogicalImmediateWords) {
  const std::array ranges{SourceRange{{1, 2}, {1, 4}},
                          SourceRange{{1, 6}, {1, 8}}};
  ResolvedRegisterRef first{.spelling = "a",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B16};
  ResolvedRegisterRef second = first;
  second.spelling = "b";
  constexpr uint8_t arities[]{2};
  checker::OperandDescriptor descriptor{
      .target_field_id = "source",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::BF16},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Vector,
      .allowed_vector_arities = arities,
      .vector_type_policy = checker::VectorTypePolicy::Element,
      .allowed_element_shapes = checker::OperandShape::Register};
  checker::OperandView view{.field_id = "source",
                            .actual_shape = checker::OperandShape::Vector,
                            .vector_arity = 2,
                            .locations = ranges};
  view.vector_element_shapes[0] = view.vector_element_shapes[1] =
      checker::OperandShape::Register;
  view.vector_element_types[0] = view.vector_element_types[1] = ScalarType::B16;
  view.vector_element_registers[0] = &first;
  view.vector_element_registers[1] = &second;
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {},
                                   intrinsic_context());
  };
  for (const bool fixed : {false, true}) {
    descriptor.minimum_elements = fixed ? 2 : 0;
    descriptor.maximum_elements = fixed ? 2 : 0;
    EXPECT_TRUE(check());
    first.declared_type = ScalarType::BF16;
    view.vector_element_types[0] = ScalarType::BF16;
    expect_intrinsic_reference_error(check(), ranges[0]);
    first.declared_type = ScalarType::B16;
    view.vector_element_types[0] = ScalarType::B16;
  }
  descriptor.minimum_elements = descriptor.maximum_elements = 0;
  descriptor.source_value_vector = true;
  ResolvedValueVector values;
  values.source.range = SourceRange{{1, 1}, {1, 9}};
  values.elements = {
      first, ResolvedImmediate{.bits = 0x3f80, .type = ScalarType::BF16}};
  view.value_vector = &values;
  view.vector_element_registers[0] =
      &std::get<ResolvedRegisterRef>(values.elements[0]);
  view.vector_element_shapes[1] = checker::OperandShape::Immediate;
  view.vector_element_types[1] = ScalarType::BF16;
  view.vector_element_registers[1] = nullptr;
  view.vector_element_immediates[1] =
      &std::get<ResolvedImmediate>(values.elements[1]);
  view.vector_immediate_bits[1] = 0x3f80;
  view.vector_immediate_negative[1] = false;
  EXPECT_TRUE(check());
  std::get<ResolvedRegisterRef>(values.elements[0]).declared_type =
      ScalarType::BF16;
  view.vector_element_types[0] = ScalarType::BF16;
  expect_intrinsic_reference_error(check(), ranges[0]);
  std::get<ResolvedRegisterRef>(values.elements[0]).declared_type =
      ScalarType::B16;
  view.vector_element_types[0] = ScalarType::B16;
  EXPECT_TRUE(check());
}
TEST(OwnedIntrinsicRegisterType,
     ValidatesUnusedDeclarationMetadataIndependentlyOfHistory) {
  const auto baseline = intrinsic_owner(true);
  ASSERT_TRUE(baseline);
  for (const auto spelling :
       {".b16", ".f16", ".f16x2", ".pred", ".u32", ".s64", ".b128"}) {
    auto positive = intrinsic_owner(true, spelling);
    ASSERT_TRUE(positive);
    EXPECT_TRUE(validateModule(*positive)) << spelling;
  }
  for (const auto& metadata : base::scalar_type_metadata()) {
    if (metadata.register_declaration_usage ==
        base::ScalarDeclarationUsage::Fundamental)
      continue;
    auto invalid = intrinsic_owner(true, metadata.source_spelling);
    ASSERT_TRUE(invalid);
    const auto checked = validateModule(*invalid);
    ASSERT_FALSE(checked) << metadata.source_spelling;
    EXPECT_TRUE(
        std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                     checker::CheckDiagnosticKind::RuleViolation &&
                 diagnostic.message.find(".reg declaration") !=
                     std::string::npos &&
                 diagnostic.range.start.line > 0 &&
                 diagnostic.range.start.column > 0;
        }));
  }
  for (const auto spelling : {"", ".unknown"}) {
    auto invalid = intrinsic_owner(true, spelling);
    ASSERT_TRUE(invalid);
    const auto checked = validateModule(*invalid);
    ASSERT_FALSE(checked);
    EXPECT_TRUE(
        std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                     checker::CheckDiagnosticKind::RuleViolation &&
                 diagnostic.message.find(".reg declaration") !=
                     std::string::npos;
        }));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
