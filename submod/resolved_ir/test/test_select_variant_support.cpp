#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

static_assert(!std::is_nothrow_constructible_v<WithLocs<std::string>,
                                               std::string&&, SourceRange>);

/** Parse one standalone instruction for resolver support tests. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

/** Parse an immediate operand while preserving its syntax spelling and range. */
syntax_ast::AstImmediate parse_immediate(std::string_view literal) {
  const auto ast = parse_instruction(std::string("add.u32 %r0, %r1, ") +
                                     std::string(literal) + ";");
  return std::get<syntax_ast::AstImmediate>(ast.operands.back());
}

/** Resolve one synthetic indirect-callee field using support descriptors. */
std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_indirect_callee_field(const syntax_ast::AstInstruction& ast,
                              const ResolveContext* context = nullptr) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> syntax_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::CallTarget |
                         check_end::OperandSyntaxShape::CallTargetSet,
       .presence = check_end::OperandPresence::Required},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts =
      {{
          {.layout_id = "indirect_callee",
           .kind = check_end::OperandLayoutKind::Flat,
           .slots = syntax_slots},
      }};
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{
      {.variant_name = "indirect_callee",
       .modifiers = {},
       .operand_layouts = syntax_layouts},
  }};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "call",
      .variants = syntax_variants,
  };

  const std::array<check_end::ResolvedFieldDescriptor, 1> operand_fields = {{
      {.field_id = "callee",
       .value_kind = check_end::ResolvedValueKind::IndirectCallee},
  }};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 1>
      operand_bindings = {{
          {.target_field_id = "callee",
           .type_expression = {},
           .role = check_end::OperandRole::Source,
           .access = check_end::OperandAccess::Control,
           .allowed_shapes = checker::OperandShape::Register,
           .allowed_vector_arities = {},
           .allowed_address_state_spaces = {}},
      }};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 1>
      resolved_layouts = {{
          {.layout_id = "indirect_callee",
           .fields = operand_fields,
           .bindings = operand_bindings},
      }};
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants =
      {{
          {.variant_name = "indirect_callee",
           .fields = {},
           .modifier_bindings = {},
           .operand_layouts = resolved_layouts},
      }};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "call",
      .variants = resolved_variants,
  };
  return resolve_fields(ast, syntax_descriptor, resolved_descriptor,
                        "indirect_callee", context);
}

/** Build a standalone call AST with one metadata target. */
syntax_ast::AstInstruction indirect_metadata_instruction(std::string spelling) {
  const SourceRange range{{1, 1}, {1, 1}};
  syntax_ast::AstInstruction ast{
      .opcode = syntax_ast::AstOpcode{.syntax = {"call", range}},
      .range = range,
  };
  ast.operands.emplace_back(syntax_ast::AstCallTargetSet{
      .name =
          syntax_ast::AstIdentifierRef{.syntax = {std::move(spelling), range}},
      .range = range,
  });
  return ast;
}

/** Retrieve the resolved indirect callee from synthetic fields. */
const WithLocs<ResolvedIndirectCallee>& indirect_callee_field(
    const ResolvedInstructionFields& fields) {
  return std::get<WithLocs<ResolvedIndirectCallee>>(
      fields.operands.at("callee"));
}

/** Resolve a synthetic register-pack instruction through support descriptors. */
std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_register_pack(const syntax_ast::AstInstruction& ast) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> syntax_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
       .presence = check_end::OperandPresence::Required,
       .minimum_elements = 1,
       .maximum_elements = 5,
       .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts =
      {{
          {.layout_id = "pack",
           .kind = check_end::OperandLayoutKind::Flat,
           .slots = syntax_slots},
      }};
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{
      {.variant_name = "Pack",
       .modifiers = {},
       .operand_layouts = syntax_layouts},
  }};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "sample",
      .variants = syntax_variants,
  };
  const std::array<check_end::ResolvedFieldDescriptor, 0> fields{};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 0> bindings{};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 1> layouts = {{
      {.layout_id = "pack", .fields = fields, .bindings = bindings},
  }};
  const std::array<check_end::ResolvedVariantDescriptor, 1> variants = {{
      {.variant_name = "Pack",
       .fields = {},
       .modifier_bindings = {},
       .operand_layouts = layouts},
  }};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "sample",
      .variants = variants,
  };
  return resolve_fields(ast, syntax_descriptor, resolved_descriptor, "Pack");
}

/** Resolve a synthetic pack against two candidate syntax layouts. */
std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_modern_pack_layouts(
    const syntax_ast::AstInstruction& ast,
    const std::array<check_end::SyntaxOperandLayoutDescriptor, 2>&
        syntax_layouts) {
  const check_end::SyntaxVariantDescriptor syntax_variant{
      .variant_name = "Pack",
      .modifiers = {},
      .operand_layouts = syntax_layouts,
  };
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{
      syntax_variant,
  }};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "sample",
      .variants = syntax_variants,
  };
  const std::array<check_end::ResolvedFieldDescriptor, 1> fields = {{
      {.field_id = "pack",
       .value_kind = check_end::ResolvedValueKind::RegisterVector},
  }};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 1> bindings = {{
      {.target_field_id = "pack",
       .type_expression = {},
       .role = check_end::OperandRole::Source,
       .access = check_end::OperandAccess::Read,
       .allowed_shapes = checker::OperandShape::Vector,
       .allowed_vector_arities = {},
       .minimum_elements = 1,
       .maximum_elements = 64,
       .allowed_element_shapes = checker::OperandShape::Register,
       .allowed_address_state_spaces = {}},
  }};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 2>
      resolved_layouts = {{
          {.layout_id = syntax_layouts[0].layout_id,
           .fields = fields,
           .bindings = bindings},
          {.layout_id = syntax_layouts[1].layout_id,
           .fields = fields,
           .bindings = bindings},
      }};
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants =
      {{
          {.variant_name = "Pack",
           .fields = {},
           .modifier_bindings = {},
           .operand_layouts = resolved_layouts},
      }};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "sample",
      .variants = resolved_variants,
  };
  return resolve_fields(ast, syntax_descriptor, resolved_descriptor, "Pack");
}

TEST(ScalarTypeMetadata, InvalidHasInvalidKindAndZeroSize) {
  EXPECT_EQ(scalar_kind(ScalarType::Invalid), base::ScalarKind::Invalid);
  EXPECT_EQ(scalar_size_of(ScalarType::Invalid), 0U);
}

TEST(ScalarTypeMetadata, AppliesExplicitRegisterSizePolicy) {
  using base::ScalarTypeSizePolicy;
  constexpr auto wider = ScalarTypeSizePolicy::EqualOrWider;

  EXPECT_TRUE(scalar_types_compatible(ScalarType::U64, ScalarType::U8, wider));
  EXPECT_TRUE(scalar_types_compatible(ScalarType::S64, ScalarType::U16, wider));
  EXPECT_TRUE(scalar_types_compatible(ScalarType::B64, ScalarType::F32, wider));
  EXPECT_TRUE(scalar_types_compatible(ScalarType::F64, ScalarType::B32, wider));
  EXPECT_FALSE(
      scalar_types_compatible(ScalarType::U16, ScalarType::U32, wider));
  EXPECT_FALSE(
      scalar_types_compatible(ScalarType::F64, ScalarType::F32, wider));
  EXPECT_FALSE(
      scalar_types_compatible(ScalarType::F64, ScalarType::U32, wider));
  EXPECT_FALSE(
      scalar_types_compatible(ScalarType::B128, ScalarType::U32, wider));
  EXPECT_TRUE(
      scalar_types_compatible(ScalarType::B128, ScalarType::B128, wider));

  // The default remains exact-width for every existing caller.
  EXPECT_FALSE(scalar_types_compatible(ScalarType::U64, ScalarType::U32));
}

TEST(ControlFlowSyntaxShape, ExposesDedicatedDescriptorFacingKinds) {
  PtxSyntaxParser call_parser("call (%result), callee, (%argument), targets;");
  const auto call = call_parser.parseInstruction();
  ASSERT_TRUE(call.has_value()) << call.diagnostics.front().message;
  ASSERT_EQ(call->operands.size(), 4u);
  EXPECT_EQ(check_end::get_operand_syntax_shape(call->operands[0]),
            check_end::OperandSyntaxShape::Group);
  EXPECT_EQ(check_end::get_operand_syntax_shape(call->operands[1]),
            check_end::OperandSyntaxShape::CallTarget);
  EXPECT_EQ(check_end::get_operand_syntax_shape(call->operands[2]),
            check_end::OperandSyntaxShape::Group);
  EXPECT_EQ(check_end::get_operand_syntax_shape(call->operands[3]),
            check_end::OperandSyntaxShape::CallTargetSet);

  PtxSyntaxParser branch_parser("bra done;");
  const auto branch = branch_parser.parseInstruction();
  ASSERT_TRUE(branch.has_value()) << branch.diagnostics.front().message;
  ASSERT_EQ(branch->operands.size(), 1u);
  EXPECT_EQ(check_end::get_operand_syntax_shape(branch->operands[0]),
            check_end::OperandSyntaxShape::BranchTarget);

  PtxSyntaxParser indexed_branch_parser("brx.idx %r0, targets;");
  const auto indexed_branch = indexed_branch_parser.parseInstruction();
  ASSERT_TRUE(indexed_branch.has_value())
      << indexed_branch.diagnostics.front().message;
  ASSERT_EQ(indexed_branch->operands.size(), 2u);
  EXPECT_EQ(check_end::get_operand_syntax_shape(indexed_branch->operands[1]),
            check_end::OperandSyntaxShape::BranchTargetSet);

  PtxSyntaxParser predicate_pair_parser("setp.eq.u32 %p0|%p1, %r0, %r1;");
  const auto predicate_pair = predicate_pair_parser.parseInstruction();
  ASSERT_TRUE(predicate_pair.has_value())
      << predicate_pair.diagnostics.front().message;
  EXPECT_EQ(check_end::get_operand_syntax_shape(predicate_pair->operands[0]),
            check_end::OperandSyntaxShape::RegisterPredicatePair);

  PtxSyntaxParser negated_constant_parser("mov.pred %p0, !1;");
  const auto negated_constant = negated_constant_parser.parseInstruction();
  ASSERT_TRUE(negated_constant.has_value())
      << negated_constant.diagnostics.front().message;
  EXPECT_EQ(check_end::get_operand_syntax_shape(negated_constant->operands[1]),
            check_end::OperandSyntaxShape::NegatedImmediate);
}

TEST(ResolveFields, DiagnosesModernPackCardinalityAtSyntaxSelection) {
  const auto ast = parse_instruction("sample {%r0, %r1, %r2, %r3, %r4, %r5};");
  const auto resolved = resolve_register_pack(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range,
            std::get<syntax_ast::AstVectorPack>(ast.operands.front()).range);
  EXPECT_EQ(resolved.error().message,
            "Vector operand requires 1 to 5 elements.");
}

TEST(ResolveFields, DiagnosesModernPackElementShapeAtSyntaxSelection) {
  const auto ast = parse_instruction("sample {1};");
  const auto& vector =
      std::get<syntax_ast::AstVectorPack>(ast.operands.front());
  const auto resolved = resolve_register_pack(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().range,
      std::get<syntax_ast::AstImmediate>(vector.elements.front()).syntax.range);
  EXPECT_EQ(resolved.error().message,
            "Vector operand element has a shape not accepted by this "
            "instruction layout.");
}

TEST(ResolveFields, SelectsRegisterOnlyModernPackLayout) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1>
      register_only_slots = {{
          {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
           .presence = check_end::OperandPresence::Required,
           .minimum_elements = 1,
           .maximum_elements = 5,
           .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
      }};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1>
      register_or_immediate_slots = {{
          {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
           .presence = check_end::OperandPresence::Required,
           .minimum_elements = 1,
           .maximum_elements = 5,
           .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier |
                                     check_end::OperandSyntaxShape::Immediate},
      }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 2> layouts = {{
      {.layout_id = "register_only",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = register_only_slots},
      {.layout_id = "register_or_immediate",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = register_or_immediate_slots},
  }};

  const auto fields =
      resolve_modern_pack_layouts(parse_instruction("sample {%r0};"), layouts);
  ASSERT_TRUE(fields.has_value()) << fields.error().message;
  EXPECT_EQ(fields->operand_layout.value, 0u);
}

TEST(ResolveFields, SelectsNarrowModernPackCardinalityLayout) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> narrow_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
       .presence = check_end::OperandPresence::Required,
       .minimum_elements = 1,
       .maximum_elements = 2,
       .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
  }};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> wide_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
       .presence = check_end::OperandPresence::Required,
       .minimum_elements = 1,
       .maximum_elements = 5,
       .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 2> layouts = {{
      {.layout_id = "narrow",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = narrow_slots},
      {.layout_id = "wide",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = wide_slots},
  }};

  const auto narrow =
      resolve_modern_pack_layouts(parse_instruction("sample {%r0};"), layouts);
  ASSERT_TRUE(narrow.has_value()) << narrow.error().message;
  EXPECT_EQ(narrow->operand_layout.value, 0u);

  const auto wide = resolve_modern_pack_layouts(
      parse_instruction("sample {%r0, %r1, %r2, %r3};"), layouts);
  ASSERT_TRUE(wide.has_value()) << wide.error().message;
  EXPECT_EQ(wide->operand_layout.value, 1u);
}

TEST(ResolveFields, SelectsBoundedModernPackOverUnconstrainedVectorPack) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> bounded_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
       .presence = check_end::OperandPresence::Required,
       .minimum_elements = 1,
       .maximum_elements = 2,
       .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
  }};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1>
      unconstrained_slots = {{
          {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
           .presence = check_end::OperandPresence::Required},
      }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 2> layouts = {{
      {.layout_id = "bounded",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = bounded_slots},
      {.layout_id = "unconstrained",
       .kind = check_end::OperandLayoutKind::Flat,
       .slots = unconstrained_slots},
  }};

  const auto bounded =
      resolve_modern_pack_layouts(parse_instruction("sample {%r0};"), layouts);
  ASSERT_TRUE(bounded.has_value()) << bounded.error().message;
  EXPECT_EQ(bounded->operand_layout.value, 0u);

  const auto unconstrained = resolve_modern_pack_layouts(
      parse_instruction("sample {%r0, %r1, %r2};"), layouts);
  ASSERT_TRUE(unconstrained.has_value()) << unconstrained.error().message;
  EXPECT_EQ(unconstrained->operand_layout.value, 1u);
}

TEST(ResolveIndirectCallee, ResolvesStandaloneRegisterAndMetadataSpelling) {
  const auto register_fields =
      resolve_indirect_callee_field(parse_instruction("call %r12;"));
  ASSERT_TRUE(register_fields.has_value()) << register_fields.error().message;
  const auto* register_ref = std::get_if<ResolvedRegisterRef>(
      &indirect_callee_field(*register_fields).value);
  ASSERT_NE(register_ref, nullptr);
  EXPECT_EQ(register_ref->spelling, "%r12");
  EXPECT_EQ(register_ref->index, 12u);
  EXPECT_FALSE(register_ref->symbol_id.has_value());

  const auto metadata_fields =
      resolve_indirect_callee_field(indirect_metadata_instruction("prototype"));
  ASSERT_TRUE(metadata_fields.has_value()) << metadata_fields.error().message;
  const auto* metadata = std::get_if<ResolvedIndirectMetadataRef>(
      &indirect_callee_field(*metadata_fields).value);
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->spelling, "prototype");
  EXPECT_FALSE(metadata->symbol_id.has_value());
  EXPECT_FALSE(metadata->declaration_kind.has_value());
}

TEST(ResolveIndirectCallee, BindsRegisterAndMetadataDeclarations) {
  PtxSyntaxParser parser(R"ptx(
.func target();
.entry caller() {
  .reg .u64 %fptr;
  L:
  prototype: .callprototype _;
  targets: .calltargets target;
  branches: .branchtargets L;
}
)ptx");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto binding = binding::bindSymbols(*module);
  EXPECT_TRUE(binding.diagnostics.empty());
  const auto caller =
      binding.table.lookup(binding.table.moduleScope(), "caller");
  ASSERT_TRUE(caller.has_value());
  const auto scope = binding.table.symbol(caller->symbol).owned_scope;
  ASSERT_TRUE(scope.has_value());
  const ResolveContext context{
      .symbols = binding.table,
      .scope = *scope,
      .function_is_entry = true,
  };

  const auto register_fields =
      resolve_indirect_callee_field(parse_instruction("call %fptr;"), &context);
  ASSERT_TRUE(register_fields.has_value()) << register_fields.error().message;
  const auto* register_ref = std::get_if<ResolvedRegisterRef>(
      &indirect_callee_field(*register_fields).value);
  ASSERT_NE(register_ref, nullptr);
  const auto fptr = binding.table.lookup(*scope, "%fptr");
  ASSERT_TRUE(fptr.has_value());
  EXPECT_EQ(register_ref->symbol_id, fptr->symbol);
  EXPECT_EQ(register_ref->declared_type, ScalarType::U64);

  const auto expect_metadata = [&](std::string spelling,
                                   binding::SymbolKind expected_kind) {
    const auto fields = resolve_indirect_callee_field(
        indirect_metadata_instruction(std::move(spelling)), &context);
    ASSERT_TRUE(fields.has_value()) << fields.error().message;
    const auto* metadata = std::get_if<ResolvedIndirectMetadataRef>(
        &indirect_callee_field(*fields).value);
    ASSERT_NE(metadata, nullptr);
    const auto expected = binding.table.lookup(*scope, metadata->spelling);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(metadata->symbol_id, expected->symbol);
    EXPECT_EQ(metadata->declaration_kind, expected_kind);
  };
  expect_metadata("prototype", binding::SymbolKind::CallPrototype);
  expect_metadata("targets", binding::SymbolKind::CallTargetSet);
}

TEST(ResolveIndirectCallee, RejectsInvalidMetadataAndDirectCalleeKinds) {
  PtxSyntaxParser parser(R"ptx(
.global .u64 table[1];
.func target();
.entry caller() {
  .reg .u64 %table<2>;
  L:
  branches: .branchtargets L;
}
)ptx");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto binding = binding::bindSymbols(*module);
  ASSERT_TRUE(binding.diagnostics.empty());
  const auto caller =
      binding.table.lookup(binding.table.moduleScope(), "caller");
  ASSERT_TRUE(caller.has_value());
  const auto scope = binding.table.symbol(caller->symbol).owned_scope;
  ASSERT_TRUE(scope.has_value());
  const ResolveContext context{
      .symbols = binding.table, .scope = *scope, .function_is_entry = true};

  const auto function = resolve_indirect_callee_field(
      parse_instruction("call target;"), &context);
  ASSERT_FALSE(function.has_value());
  EXPECT_EQ(function.error().message,
            "Symbol 'target' is not a .reg variable.");

  const auto branch = resolve_indirect_callee_field(
      indirect_metadata_instruction("branches"), &context);
  ASSERT_FALSE(branch.has_value());
  EXPECT_EQ(branch.error().message,
            "Indirect call metadata 'branches' must name a function-local "
            ".callprototype or .calltargets declaration.");

  const auto array = resolve_indirect_callee_field(
      indirect_metadata_instruction("%table0"), &context);
  ASSERT_FALSE(array.has_value());
  EXPECT_EQ(array.error().message,
            "Indirect call metadata variables and call-table arrays are not "
            "supported.");

  const auto global_array = resolve_indirect_callee_field(
      indirect_metadata_instruction("table"), &context);
  ASSERT_FALSE(global_array.has_value());
  EXPECT_EQ(global_array.error().message,
            "Indirect call metadata variables and call-table arrays are not "
            "supported.");

  const auto missing = resolve_indirect_callee_field(
      indirect_metadata_instruction("missing"), &context);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().message,
            "Unresolved indirect call metadata 'missing'.");
}

TEST(CollectActualModifiers, BindsRepeatedSpellingsToOrderedSlots) {
  const std::array<std::string_view, 1> f16 = {".f16"};
  const std::array<check_end::SyntaxModifierDescriptor, 2> modifiers = {{
      {.allowed_values = f16,
       .presence = check_end::PresenceRequirement::Required,
       .kind_id = "first_type"},
      {.allowed_values = f16,
       .presence = check_end::PresenceRequirement::Required,
       .kind_id = "second_type"},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 0> layouts{};
  const std::array<check_end::SyntaxVariantDescriptor, 1> variants = {{
      {.variant_name = "Repeated",
       .modifiers = modifiers,
       .operand_layouts = layouts},
  }};
  const check_end::SyntaxInstructionDescriptor instruction{
      .Opcode_name = "sample",
      .variants = variants,
  };

  const auto ast = parse_instruction("sample.f16.f16;");
  const auto actual = collect_actual_modifiers(ast, variants.front());
  ASSERT_TRUE(actual.has_value()) << actual.error().message;
  EXPECT_EQ(actual->at("first_type"), &ast.modifiers[0]);
  EXPECT_EQ(actual->at("second_type"), &ast.modifiers[1]);
  EXPECT_EQ(select_variant_name(ast, instruction), "Repeated");

  const auto extra = parse_instruction("sample.f16.f16.f16;");
  const auto duplicate = collect_actual_modifiers(extra, variants.front());
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error().range, extra.modifiers.back().syntax.range);
  EXPECT_EQ(duplicate.error().message, "Duplicate 'second_type' modifier.");
}

TEST(ResolveImmediateLiteral, ConvertsEvaluatedIntegerSourcesAtTheUseWidth) {
  const auto decimal = parse_immediate("123U");
  EXPECT_EQ(decimal.kind, syntax_ast::AstImmediateKind::DecimalInteger);
  const auto decimal_value =
      resolve_immediate_literal(decimal, ScalarType::U16);
  ASSERT_TRUE(decimal_value.has_value()) << decimal_value.error().message;
  EXPECT_EQ(decimal_value->bits, 123U);

  const auto hexadecimal = parse_immediate("0x10U");
  EXPECT_EQ(hexadecimal.kind, syntax_ast::AstImmediateKind::HexInteger);
  const auto hexadecimal_value =
      resolve_immediate_literal(hexadecimal, ScalarType::U16);
  ASSERT_TRUE(hexadecimal_value.has_value())
      << hexadecimal_value.error().message;
  EXPECT_EQ(hexadecimal_value->bits, 16U);

  const auto negative = parse_immediate("-1");
  const auto negative_value =
      resolve_immediate_literal(negative, ScalarType::S16);
  ASSERT_TRUE(negative_value.has_value()) << negative_value.error().message;
  EXPECT_EQ(negative_value->bits, 0xffffU);

  const auto negative_unsigned =
      resolve_immediate_literal(negative, ScalarType::U16);
  ASSERT_TRUE(negative_unsigned.has_value())
      << negative_unsigned.error().message;
  EXPECT_EQ(negative_unsigned->bits, 0xffffU);

  const auto narrowed =
      resolve_immediate_literal(parse_immediate("65536"), ScalarType::U16);
  ASSERT_TRUE(narrowed.has_value()) << narrowed.error().message;
  EXPECT_EQ(narrowed->bits, 0U);
  EXPECT_EQ(narrowed->integer_source_bits, 65536U);

  const auto signed_word =
      resolve_immediate_literal(parse_immediate("0xffffffff"), ScalarType::S32);
  ASSERT_TRUE(signed_word.has_value()) << signed_word.error().message;
  EXPECT_EQ(signed_word->bits, 0xffffffffU);
  EXPECT_EQ(signed_word->integer_source_bits, 0xffffffffU);

  const auto unsigned_word =
      resolve_immediate_literal(parse_immediate("4294967296"), ScalarType::U32);
  ASSERT_TRUE(unsigned_word.has_value()) << unsigned_word.error().message;
  EXPECT_EQ(unsigned_word->bits, 0U);
  EXPECT_EQ(unsigned_word->integer_source_bits, 4294967296U);
}

TEST(ResolveImmediateLiteral,
     ResolvesOctalIntegerLiteralsAndRetainsDecimalSyntaxKind) {
  constexpr std::array<std::pair<std::string_view, uint64_t>, 6> cases{{
      {"0", 0U},
      {"0U", 0U},
      {"010", 8U},
      {"077", 63U},
      {"010u", 8U},
      {"010U", 8U},
  }};
  for (const auto& [source, expected] : cases) {
    SCOPED_TRACE(source);
    const auto immediate = parse_immediate(source);
    EXPECT_EQ(immediate.kind, syntax_ast::AstImmediateKind::DecimalInteger);
    const auto resolved = resolve_immediate_literal(immediate, ScalarType::U8);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_EQ(resolved->bits, expected);
    EXPECT_EQ(resolved->type, ScalarType::U8);
  }

  const auto positive =
      resolve_immediate_literal(parse_immediate("+010"), ScalarType::S8);
  ASSERT_TRUE(positive.has_value()) << positive.error().message;
  EXPECT_EQ(positive->bits, 8U);
  EXPECT_FALSE(positive->is_negative);

  const auto negative =
      resolve_immediate_literal(parse_immediate("-010"), ScalarType::S8);
  ASSERT_TRUE(negative.has_value()) << negative.error().message;
  EXPECT_EQ(negative->bits, 0xf8U);
  EXPECT_TRUE(negative->is_negative);
}

TEST(ResolveImmediateLiteral, RetainsIntegerSourceBitsAndRejectsOverflow) {
  const auto signed_limit =
      resolve_immediate_literal(parse_immediate("0177"), ScalarType::S8);
  ASSERT_TRUE(signed_limit.has_value()) << signed_limit.error().message;
  EXPECT_EQ(signed_limit->bits, 0x7fU);
  EXPECT_EQ(signed_limit->integer_source_bits, 127U);

  const auto narrowed_octal =
      resolve_immediate_literal(parse_immediate("0200"), ScalarType::S8);
  ASSERT_TRUE(narrowed_octal.has_value()) << narrowed_octal.error().message;
  EXPECT_EQ(narrowed_octal->bits, 0x80U);
  EXPECT_EQ(narrowed_octal->integer_source_bits, 128U);
  EXPECT_FALSE(narrowed_octal->is_negative);

  const auto uint64_limit = resolve_immediate_literal(
      parse_immediate("01777777777777777777777"), ScalarType::U64);
  ASSERT_TRUE(uint64_limit.has_value()) << uint64_limit.error().message;
  EXPECT_EQ(uint64_limit->bits, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(uint64_limit->integer_source_bits,
            std::numeric_limits<uint64_t>::max());

  const auto uint64_overflow = resolve_immediate_literal(
      parse_immediate("02000000000000000000000"), ScalarType::U64);
  ASSERT_FALSE(uint64_overflow.has_value());
  EXPECT_EQ(uint64_overflow.error().message,
            "Invalid integer literal '02000000000000000000000'.");
}

TEST(ResolveImmediateLiteral, NormalizesIntegerMinusZeroAndUnsignedNegation) {
  for (const auto spelling : {"0", "+0", "-0", "-0U", "-0x0", "-0x0U"}) {
    SCOPED_TRACE(spelling);
    const auto resolved =
        resolve_immediate_literal(parse_immediate(spelling), ScalarType::U32);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_EQ(resolved->bits, 0U);
    EXPECT_EQ(resolved->integer_source_bits, 0U);
    EXPECT_FALSE(resolved->is_negative);
  }

  const auto signed_negative =
      resolve_immediate_literal(parse_immediate("-1"), ScalarType::U32);
  ASSERT_TRUE(signed_negative.has_value()) << signed_negative.error().message;
  EXPECT_TRUE(signed_negative->is_negative);
  EXPECT_EQ(signed_negative->integer_source_bits,
            std::numeric_limits<uint64_t>::max());

  const auto unsigned_negation = resolve_immediate_literal(
      parse_immediate("-18446744073709551615U"), ScalarType::U32);
  ASSERT_TRUE(unsigned_negation.has_value())
      << unsigned_negation.error().message;
  EXPECT_FALSE(unsigned_negation->is_negative);
  EXPECT_EQ(unsigned_negation->integer_source_bits, 1U);
  EXPECT_EQ(unsigned_negation->bits, 1U);

  const auto negative_unsigned_one =
      resolve_immediate_literal(parse_immediate("-1U"), ScalarType::U32);
  ASSERT_TRUE(negative_unsigned_one.has_value())
      << negative_unsigned_one.error().message;
  EXPECT_FALSE(negative_unsigned_one->is_negative);
  EXPECT_EQ(negative_unsigned_one->integer_source_bits,
            std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(negative_unsigned_one->bits, 0xffffffffU);
}

TEST(ResolveImmediateLiteral, RejectsInvalidOctalTextOutsideLexer) {
  constexpr std::array<std::string_view, 3> invalid_cases{{
      "09",
      "078U",
      "02000000000000000000000",
  }};
  for (const auto source : invalid_cases) {
    SCOPED_TRACE(source);
    auto immediate = parse_immediate("010");
    immediate.syntax.text = source;
    EXPECT_EQ(immediate.kind, syntax_ast::AstImmediateKind::DecimalInteger);

    const auto resolved = resolve_immediate_literal(immediate, ScalarType::U64);

    ASSERT_FALSE(resolved.has_value());
    EXPECT_EQ(resolved.error().range, immediate.syntax.range);
  }
}

TEST(ResolveImmediateLiteral, PreservesOctalAdjacentLiteralForms) {
  const auto decimal = parse_immediate("10");
  EXPECT_EQ(decimal.kind, syntax_ast::AstImmediateKind::DecimalInteger);
  const auto decimal_value = resolve_immediate_literal(decimal, ScalarType::U8);
  ASSERT_TRUE(decimal_value.has_value()) << decimal_value.error().message;
  EXPECT_EQ(decimal_value->bits, 10U);

  const auto hexadecimal = parse_immediate("0x10");
  EXPECT_EQ(hexadecimal.kind, syntax_ast::AstImmediateKind::HexInteger);
  const auto hexadecimal_value =
      resolve_immediate_literal(hexadecimal, ScalarType::U8);
  ASSERT_TRUE(hexadecimal_value.has_value())
      << hexadecimal_value.error().message;
  EXPECT_EQ(hexadecimal_value->bits, 16U);

  const auto decimal_float = parse_immediate("0.5");
  EXPECT_EQ(decimal_float.kind, syntax_ast::AstImmediateKind::DecimalFloat);
  const auto decimal_float_value =
      resolve_immediate_literal(decimal_float, ScalarType::F32);
  ASSERT_TRUE(decimal_float_value.has_value())
      << decimal_float_value.error().message;
  EXPECT_EQ(decimal_float_value->bits, 0x3f000000U);

  const auto f32_hex = parse_immediate("0f3f800000");
  EXPECT_EQ(f32_hex.kind, syntax_ast::AstImmediateKind::F32Hex);
  const auto f32_hex_value =
      resolve_immediate_literal(f32_hex, ScalarType::F32);
  ASSERT_TRUE(f32_hex_value.has_value()) << f32_hex_value.error().message;
  EXPECT_EQ(f32_hex_value->bits, 0x3f800000U);

  const auto f64_hex = parse_immediate("0d3ff0000000000000");
  EXPECT_EQ(f64_hex.kind, syntax_ast::AstImmediateKind::F64Hex);
  const auto f64_hex_value =
      resolve_immediate_literal(f64_hex, ScalarType::F64);
  ASSERT_TRUE(f64_hex_value.has_value()) << f64_hex_value.error().message;
  EXPECT_EQ(f64_hex_value->bits, 0x3ff0000000000000ULL);
}

TEST(ResolveImmediateLiteral, SupportsFloatingLexicalForms) {
  const auto decimal = parse_immediate("1.5");
  EXPECT_EQ(decimal.kind, syntax_ast::AstImmediateKind::DecimalFloat);
  const auto decimal_value =
      resolve_immediate_literal(decimal, ScalarType::F32);
  ASSERT_TRUE(decimal_value.has_value()) << decimal_value.error().message;
  EXPECT_EQ(decimal_value->bits, 0x3fc00000U);

  const auto f32_hex = parse_immediate("0f3f800000");
  EXPECT_EQ(f32_hex.kind, syntax_ast::AstImmediateKind::F32Hex);
  const auto f32_hex_value =
      resolve_immediate_literal(f32_hex, ScalarType::F32);
  ASSERT_TRUE(f32_hex_value.has_value()) << f32_hex_value.error().message;
  EXPECT_EQ(f32_hex_value->bits, 0x3f800000U);

  const auto f64_hex = parse_immediate("0d3ff0000000000000");
  EXPECT_EQ(f64_hex.kind, syntax_ast::AstImmediateKind::F64Hex);
  const auto f64_hex_value =
      resolve_immediate_literal(f64_hex, ScalarType::F64);
  ASSERT_TRUE(f64_hex_value.has_value()) << f64_hex_value.error().message;
  EXPECT_EQ(f64_hex_value->bits, 0x3ff0000000000000ULL);

  const auto incompatible = resolve_immediate_literal(decimal, ScalarType::U32);
  ASSERT_FALSE(incompatible.has_value());
  EXPECT_EQ(incompatible.error().range, decimal.syntax.range);
  EXPECT_EQ(incompatible.error().message,
            "Decimal floating literal '1.5' is incompatible with scalar type "
            "'U32'.");
}

TEST(ResolveImmediateLiteral,
     NarrowsAtIeeeBoundariesAndPreservesExactPayloads) {
  constexpr std::array<std::pair<std::string_view, uint64_t>, 15> cases{{
      {"0d0000000000000000", 0x00000000},
      {"0d8000000000000000", 0x80000000},
      {"0d0000000000000001", 0x00000000},
      {"0d8000000000000001", 0x80000000},
      {"0d3690000000000000", 0x00000000},
      {"0d3690000000000001", 0x00000001},
      {"0d36a0000000000000", 0x00000001},
      {"0d36a8000000000000", 0x00000002},
      {"0d380fffffe0000000", 0x00800000},
      {"0d3ff0000010000000", 0x3f800000},
      {"0d3ff0000030000000", 0x3f800002},
      {"0d47efffffefffffff", 0x7f7fffff},
      {"0d47effffff0000000", 0x7f800000},
      {"0d7ff0000000000000", 0x7f800000},
      {"0d7ff0000000000001", 0x7fc00000},
  }};
  for (const auto& [source, expected] : cases) {
    SCOPED_TRACE(source);
    const auto converted =
        resolve_immediate_literal(parse_immediate(source), ScalarType::F32);
    ASSERT_TRUE(converted.has_value()) << converted.error().message;
    EXPECT_EQ(converted->bits, expected);
    EXPECT_EQ(converted->type, ScalarType::F32);
  }
  const auto unchanged_nan =
      resolve_immediate_literal(parse_immediate("0f7f800001"), ScalarType::F32);
  ASSERT_TRUE(unchanged_nan.has_value());
  EXPECT_EQ(unchanged_nan->bits, 0x7f800001);
  const auto widened_nan =
      resolve_immediate_literal(parse_immediate("0f7f800001"), ScalarType::F64);
  ASSERT_TRUE(widened_nan.has_value());
  EXPECT_EQ(widened_nan->bits, 0x7ff8000020000000ULL);
}

TEST(ResolveImmediateLiteral, PreservesFloatingNegativeZero) {
  const auto negative_zero = parse_immediate("-0.0");
  const auto f32 = resolve_immediate_literal(negative_zero, ScalarType::F32);
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  EXPECT_EQ(f32->bits, 0x80000000U);
  EXPECT_FALSE(f32->integer_source_bits.has_value());

  const auto f64 = resolve_immediate_literal(negative_zero, ScalarType::F64);
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  EXPECT_EQ(f64->bits, 0x8000000000000000ULL);
  EXPECT_FALSE(f64->integer_source_bits.has_value());
}

TEST(ResolveCallLiteral, TypesAgainstTheFormalAndPreservesSourceRange) {
  const declaration_semantics::FunctionParameterContract u16{
      .scalar_type = base::ScalarType::U16,
      .type_spelling = ".u16",
  };
  const auto typed_immediate = parse_immediate("42");
  const auto typed = resolve_call_literal(
      ResolvedCallLiteral{.spelling = typed_immediate.syntax.text,
                          .kind = typed_immediate.kind},
      typed_immediate.syntax.range, u16);
  ASSERT_TRUE(typed.has_value()) << typed.error().message;
  EXPECT_EQ(typed->value, (ResolvedImmediate{.bits = 42,
                                             .type = ScalarType::U16,
                                             .integer_source_bits = 42}));
  EXPECT_EQ(typed->locs, std::vector{typed_immediate.syntax.range});

  const auto overflow_immediate = parse_immediate("65536");
  const auto overflow = resolve_call_literal(
      ResolvedCallLiteral{.spelling = overflow_immediate.syntax.text,
                          .kind = overflow_immediate.kind},
      overflow_immediate.syntax.range, u16);
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error().range, overflow_immediate.syntax.range);
  EXPECT_EQ(overflow.error().message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");

  const declaration_semantics::FunctionParameterContract u32{
      .scalar_type = base::ScalarType::U32,
      .type_spelling = ".u32",
  };
  const auto float_immediate = parse_immediate("1.5");
  const auto mismatch = resolve_call_literal(
      ResolvedCallLiteral{.spelling = float_immediate.syntax.text,
                          .kind = float_immediate.kind},
      float_immediate.syntax.range, u32);
  ASSERT_FALSE(mismatch.has_value());
  EXPECT_EQ(mismatch.error().range, float_immediate.syntax.range);
  EXPECT_EQ(mismatch.error().message,
            "Decimal floating literal '1.5' is incompatible with scalar type "
            "'U32'.");

  const declaration_semantics::FunctionParameterContract unsupported_type{
      .type_spelling = ".v2"};
  const auto unsupported_immediate = parse_immediate("1");
  const auto unsupported = resolve_call_literal(
      ResolvedCallLiteral{.spelling = unsupported_immediate.syntax.text,
                          .kind = unsupported_immediate.kind},
      unsupported_immediate.syntax.range, unsupported_type);
  ASSERT_FALSE(unsupported.has_value());
  EXPECT_EQ(unsupported.error().range, unsupported_immediate.syntax.range);
  EXPECT_EQ(unsupported.error().message,
            "Call literal '1' has unsupported formal scalar type '.v2'.");
}

TEST(ResolveFields, AppliesTypedOptionalModifierDefault) {
  const std::array<std::string_view, 2> allowed_types = {".u32", ".u64"};
  const std::array<check_end::SyntaxModifierDescriptor, 1> syntax_modifiers = {
      {{
          .allowed_values = allowed_types,
          .presence = check_end::PresenceRequirement::Optional,
          .kind_id = "type",
      }}};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 0> syntax_slots{};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts =
      {{{
          .layout_id = "default",
          .kind = check_end::OperandLayoutKind::Flat,
          .slots = syntax_slots,
      }}};
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{{
      .variant_name = "Defaulted",
      .modifiers = syntax_modifiers,
      .operand_layouts = syntax_layouts,
  }}};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "sample",
      .variants = syntax_variants,
  };

  const std::array<check_end::ResolvedFieldDescriptor, 1> resolved_fields = {{{
      .field_id = "type",
      .value_kind = check_end::ResolvedValueKind::ScalarType,
  }}};
  const std::array<check_end::ResolvedModifierBindingDescriptor, 1>
      modifier_bindings = {{{
          .source_kind_id = "type",
          .target_field_id = "type",
          .default_value =
              {
                  .kind = check_end::ResolvedModifierDefaultKind::ScalarType,
                  .bool_value = false,
                  .scalar_type = ScalarType::U32,
              },
      }}};
  const std::array<check_end::ResolvedFieldDescriptor, 0> operand_fields{};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 0>
      operand_bindings{};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 1>
      resolved_layouts = {{{
          .layout_id = "default",
          .fields = operand_fields,
          .bindings = operand_bindings,
      }}};
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants =
      {{{
          .variant_name = "Defaulted",
          .fields = resolved_fields,
          .modifier_bindings = modifier_bindings,
          .operand_layouts = resolved_layouts,
      }}};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "sample",
      .variants = resolved_variants,
  };

  const auto implicit_ast = parse_instruction("sample;");
  const auto implicit = resolve_fields(implicit_ast, syntax_descriptor,
                                       resolved_descriptor, "Defaulted");
  ASSERT_TRUE(implicit.has_value()) << implicit.error().message;
  const auto* implicit_type =
      std::get_if<WithLocs<ScalarType>>(&implicit->modifiers.at("type"));
  ASSERT_NE(implicit_type, nullptr);
  EXPECT_EQ(implicit_type->value, ScalarType::U32);
  EXPECT_TRUE(implicit_type->locs.empty());

  const auto explicit_ast = parse_instruction("sample.u64;");
  const auto explicit_value = resolve_fields(explicit_ast, syntax_descriptor,
                                             resolved_descriptor, "Defaulted");
  ASSERT_TRUE(explicit_value.has_value()) << explicit_value.error().message;
  const auto* explicit_type =
      std::get_if<WithLocs<ScalarType>>(&explicit_value->modifiers.at("type"));
  ASSERT_NE(explicit_type, nullptr);
  EXPECT_EQ(explicit_type->value, ScalarType::U64);
  ASSERT_EQ(explicit_type->locs.size(), 1U);
  EXPECT_EQ(explicit_type->locs.front(),
            explicit_ast.modifiers.front().syntax.range);
}

TEST(ResolveFields, ResolvesComparisonOperatorModifier) {
  const std::array<std::string_view, 1> allowed_comparisons = {".lt"};
  const std::array<check_end::SyntaxModifierDescriptor, 1> syntax_modifiers = {
      {{
          .allowed_values = allowed_comparisons,
          .presence = check_end::PresenceRequirement::Required,
          .kind_id = "comparison",
      }}};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 0> syntax_slots{};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts =
      {{{
          .layout_id = "default",
          .kind = check_end::OperandLayoutKind::Flat,
          .slots = syntax_slots,
      }}};
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{{
      .variant_name = "Comparison",
      .modifiers = syntax_modifiers,
      .operand_layouts = syntax_layouts,
  }}};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "sample",
      .variants = syntax_variants,
  };

  const std::array<check_end::ResolvedFieldDescriptor, 1> resolved_fields = {{{
      .field_id = "comparison",
      .value_kind = check_end::ResolvedValueKind::ComparisonOperator,
  }}};
  const std::array<check_end::ResolvedModifierBindingDescriptor, 1>
      modifier_bindings = {{{
          .source_kind_id = "comparison",
          .target_field_id = "comparison",
      }}};
  const std::array<check_end::ResolvedFieldDescriptor, 0> operand_fields{};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 0>
      operand_bindings{};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 1>
      resolved_layouts = {{{
          .layout_id = "default",
          .fields = operand_fields,
          .bindings = operand_bindings,
      }}};
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants =
      {{{
          .variant_name = "Comparison",
          .fields = resolved_fields,
          .modifier_bindings = modifier_bindings,
          .operand_layouts = resolved_layouts,
      }}};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "sample",
      .variants = resolved_variants,
  };

  const auto ast = parse_instruction("sample.lt;");
  const auto fields =
      resolve_fields(ast, syntax_descriptor, resolved_descriptor, "Comparison");
  ASSERT_TRUE(fields.has_value()) << fields.error().message;
  const auto* comparison = std::get_if<WithLocs<ComparisonOperator>>(
      &fields->modifiers.at("comparison"));
  ASSERT_NE(comparison, nullptr);
  EXPECT_EQ(comparison->value, ComparisonOperator::Lt);
  ASSERT_EQ(comparison->locs.size(), 1U);
  EXPECT_EQ(comparison->locs.front(), ast.modifiers.front().syntax.range);
}

TEST(ResolveFields, ResolvesBooleanOperatorModifier) {
  const std::array<std::string_view, 3> allowed_boolean_operators = {
      ".and", ".or", ".xor"};
  const std::array<check_end::SyntaxModifierDescriptor, 1> syntax_modifiers = {
      {{
          .allowed_values = allowed_boolean_operators,
          .presence = check_end::PresenceRequirement::Required,
          .kind_id = "boolean",
      }}};
  const std::array<check_end::SyntaxOperandSlotDescriptor, 0> syntax_slots{};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts =
      {{{
          .layout_id = "default",
          .kind = check_end::OperandLayoutKind::Flat,
          .slots = syntax_slots,
      }}};
  const std::array<check_end::SyntaxVariantDescriptor, 1> syntax_variants = {{{
      .variant_name = "Boolean",
      .modifiers = syntax_modifiers,
      .operand_layouts = syntax_layouts,
  }}};
  const check_end::SyntaxInstructionDescriptor syntax_descriptor{
      .Opcode_name = "sample",
      .variants = syntax_variants,
  };

  const std::array<check_end::ResolvedFieldDescriptor, 1> resolved_fields = {{{
      .field_id = "boolean",
      .value_kind = check_end::ResolvedValueKind::BooleanOperator,
  }}};
  const std::array<check_end::ResolvedModifierBindingDescriptor, 1>
      modifier_bindings = {{{
          .source_kind_id = "boolean",
          .target_field_id = "boolean",
      }}};
  const std::array<check_end::ResolvedFieldDescriptor, 0> operand_fields{};
  const std::array<check_end::ResolvedOperandBindingDescriptor, 0>
      operand_bindings{};
  const std::array<check_end::ResolvedOperandLayoutDescriptor, 1>
      resolved_layouts = {{{
          .layout_id = "default",
          .fields = operand_fields,
          .bindings = operand_bindings,
      }}};
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants =
      {{{
          .variant_name = "Boolean",
          .fields = resolved_fields,
          .modifier_bindings = modifier_bindings,
          .operand_layouts = resolved_layouts,
      }}};
  const check_end::ResolvedInstructionDescriptor resolved_descriptor{
      .opcode_name = "sample",
      .variants = resolved_variants,
  };

  const auto ast = parse_instruction("sample.xor;");
  const auto fields =
      resolve_fields(ast, syntax_descriptor, resolved_descriptor, "Boolean");
  ASSERT_TRUE(fields.has_value()) << fields.error().message;
  const auto* boolean =
      std::get_if<WithLocs<BooleanOperator>>(&fields->modifiers.at("boolean"));
  ASSERT_NE(boolean, nullptr);
  EXPECT_EQ(boolean->value, BooleanOperator::Xor);
  ASSERT_EQ(boolean->locs.size(), 1U);
  EXPECT_EQ(boolean->locs.front(), ast.modifiers.front().syntax.range);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
