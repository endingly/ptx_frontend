#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

static_assert(!std::is_nothrow_constructible_v<WithLocs<std::string>,
                                               std::string&&, SourceRange>);

TEST(ScalarTypeMetadata, InvalidHasInvalidKindAndZeroSize) {
  EXPECT_EQ(scalar_kind(ScalarType::Invalid), base::ScalarKind::Invalid);
  EXPECT_EQ(scalar_size_of(ScalarType::Invalid), 0U);
}

TEST(ScalarTypeMetadata, AppliesExplicitRegisterSizePolicy) {
  using base::ScalarTypeSizePolicy;
  constexpr auto wider = ScalarTypeSizePolicy::EqualOrWider;

  EXPECT_TRUE(scalar_types_compatible(ScalarType::U64, ScalarType::U8, wider));
  EXPECT_TRUE(
      scalar_types_compatible(ScalarType::S64, ScalarType::U16, wider));
  EXPECT_TRUE(
      scalar_types_compatible(ScalarType::B64, ScalarType::F32, wider));
  EXPECT_TRUE(
      scalar_types_compatible(ScalarType::F64, ScalarType::B32, wider));
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
}

syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

syntax_ast::AstImmediate parse_immediate(std::string_view literal) {
  const auto ast = parse_instruction(std::string("add.u32 %r0, %r1, ") +
                                     std::string(literal) + ";");
  return std::get<syntax_ast::AstImmediate>(ast.operands.back());
}

std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_indirect_callee_field(const syntax_ast::AstInstruction& ast,
                              const ResolveContext* context = nullptr) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> syntax_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::CallTarget |
                         check_end::OperandSyntaxShape::CallTargetSet,
       .presence = check_end::OperandPresence::Required},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts = {{
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
  const std::array<check_end::ResolvedVariantDescriptor, 1>
      resolved_variants = {{
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

syntax_ast::AstInstruction indirect_metadata_instruction(std::string spelling) {
  const SourceRange range{{1, 1}, {1, 1}};
  syntax_ast::AstInstruction ast{
      .opcode = syntax_ast::AstOpcode{.syntax = {"call", range}},
      .range = range,
  };
  ast.operands.emplace_back(syntax_ast::AstCallTargetSet{
      .name = syntax_ast::AstIdentifierRef{.syntax = {std::move(spelling),
                                                       range}},
      .range = range,
  });
  return ast;
}

const WithLocs<ResolvedIndirectCallee>& indirect_callee_field(
    const ResolvedInstructionFields& fields) {
  return std::get<WithLocs<ResolvedIndirectCallee>>(
      fields.operands.at("callee"));
}

std::expected<ResolvedInstructionFields, ResolveDiagnostic>
resolve_register_pack(const syntax_ast::AstInstruction& ast) {
  const std::array<check_end::SyntaxOperandSlotDescriptor, 1> syntax_slots = {{
      {.allowed_shapes = check_end::OperandSyntaxShape::VectorPack,
       .presence = check_end::OperandPresence::Required,
       .minimum_elements = 1,
       .maximum_elements = 5,
       .allowed_element_shapes = check_end::OperandSyntaxShape::Identifier},
  }};
  const std::array<check_end::SyntaxOperandLayoutDescriptor, 1> syntax_layouts = {{
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
  const std::array<check_end::ResolvedVariantDescriptor, 1> resolved_variants = {{
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

TEST(ResolveFields, DiagnosesModernPackCardinalityAtSyntaxSelection) {
  const auto ast = parse_instruction("sample {%r0, %r1, %r2, %r3, %r4, %r5};");
  const auto resolved = resolve_register_pack(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range,
            std::get<syntax_ast::AstVectorPack>(ast.operands.front()).range);
  EXPECT_EQ(resolved.error().message, "Vector operand requires 1 to 5 elements.");
}

TEST(ResolveFields, DiagnosesModernPackElementShapeAtSyntaxSelection) {
  const auto ast = parse_instruction("sample {1};");
  const auto& vector = std::get<syntax_ast::AstVectorPack>(ast.operands.front());
  const auto resolved = resolve_register_pack(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range,
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

  const auto fields = resolve_modern_pack_layouts(
      parse_instruction("sample {%r0};"), layouts);
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

  const auto metadata_fields = resolve_indirect_callee_field(
      indirect_metadata_instruction("prototype"));
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
  const ResolveContext context{.symbols = binding.table,
                               .scope = *scope,
                               .function_is_entry = true};

  const auto function =
      resolve_indirect_callee_field(parse_instruction("call target;"), &context);
  ASSERT_FALSE(function.has_value());
  EXPECT_EQ(function.error().message, "Symbol 'target' is not a .reg variable.");

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

TEST(SelectVariantAdd, SelectsEveryGeneratedVariant) {
  const auto expect_variant = [](std::string_view source,
                                 Add::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Add>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("add.u32 %r0, %r1, %r2;", Add::VariantType::IntegerNoSat);
  expect_variant("add.sat.s32 %r0, %r1, %r2;", Add::VariantType::Sat);
  expect_variant("add.u16x2 %r0, %r1, %r2;", Add::VariantType::IntegerNoSat);
  expect_variant("add.u8x4 %r0, %r1, %r2;",
                 Add::VariantType::PackedOptionalSat);
  expect_variant("add.sat.u32 %r0, %r1, %r2;", Add::VariantType::Sat);
  expect_variant("add.f32 %f0, %f1, %f2;", Add::VariantType::FloatF32);
  expect_variant("add.rz.ftz.sat.f32 %f0, %f1, %f2;",
                 Add::VariantType::FloatF32);
  expect_variant("add.rp.f32x2 %r0, %r1, %r2;", Add::VariantType::FloatF32x2);
  expect_variant("add.rm.f64 %fd0, %fd1, %fd2;", Add::VariantType::FloatF64);
  expect_variant("add.rn.ftz.sat.f16x2 %r0, %r1, %r2;", Add::VariantType::Half);
  expect_variant("add.bf16 %r0, %r1, %r2;", Add::VariantType::Bfloat);
  expect_variant("add.f32.f16 %f0, %h1, %f2;", Add::VariantType::MixedF32);
  expect_variant("add.rz.f32.bf16.sat %f0, %h1, %f2;",
                 Add::VariantType::MixedF32);
}

TEST(SelectVariantSub, SelectsEveryGeneratedVariant) {
  const auto expect_variant = [](std::string_view source,
                                 Sub::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Sub>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("sub.u32 %r0, %r1, %r2;", Sub::VariantType::IntegerNoSat);
  expect_variant("sub.s32 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.sat.s32 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.u8x4 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.sat.s8x4 %r0, %r1, %r2;", Sub::VariantType::OptionalSat);
  expect_variant("sub.rz.ftz.sat.f32 %f0, %f1, %f2;",
                 Sub::VariantType::FloatF32);
  expect_variant("sub.rp.f32x2 %r0, %r1, %r2;", Sub::VariantType::FloatF32x2);
  expect_variant("sub.rm.f64 %fd0, %fd1, %fd2;", Sub::VariantType::FloatF64);
  expect_variant("sub.rn.ftz.sat.f16x2 %r0, %r1, %r2;", Sub::VariantType::Half);
  expect_variant("sub.bf16 %r0, %r1, %r2;", Sub::VariantType::Bfloat);
  expect_variant("sub.f32.f16 %f0, %h1, %f2;", Sub::VariantType::MixedF32);
  expect_variant("sub.rz.f32.bf16.sat %f0, %h1, %f2;",
                 Sub::VariantType::MixedF32);
}

TEST(SelectVariantBar, SelectsEveryGeneratedVariant) {
  const auto expect_variant = [](std::string_view source,
                                 Bar::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Bar>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("bar.sync 0;", Bar::VariantType::Sync);
  expect_variant("bar.cta.sync 0;", Bar::VariantType::CtaSync);
  expect_variant("bar.arrive 0, 32;", Bar::VariantType::Arrive);
  expect_variant("bar.cta.arrive 0, 32;", Bar::VariantType::CtaArrive);
  expect_variant("bar.red.popc.u32 %r0, 1, %p1;", Bar::VariantType::RedPopcU32);
  expect_variant("bar.cta.red.popc.u32 %r0, 1, !%p1;",
                 Bar::VariantType::CtaRedPopcU32);
  expect_variant("bar.red.and.pred %p0, 1, %p1;", Bar::VariantType::RedAndPred);
  expect_variant("bar.cta.red.and.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedAndPred);
  expect_variant("bar.red.or.pred %p0, 1, %p1;", Bar::VariantType::RedOrPred);
  expect_variant("bar.cta.red.or.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedOrPred);
  expect_variant("bar.warp.sync 0xffffffff;", Bar::VariantType::WarpSync);

  for (const std::string_view source : {"bar.warp 0xffffffff;",
                                        "bar.warp.arrive 0xffffffff;"}) {
    const auto selected = selectVariant<Bar>(parse_instruction(source));
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(SelectVariantBarrier, SelectsClusterArriveAndWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Barrier::VariantType expected) {
    const auto selected = selectVariant<Barrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  for (const std::string_view source : {
           "barrier.cluster.arrive;",
           "barrier.cluster.arrive.aligned;",
           "barrier.cluster.arrive.release.aligned;",
           "barrier.cluster.arrive.relaxed;",
       }) {
    expect_variant(source, Barrier::VariantType::ClusterArrive);
  }
  for (const std::string_view source : {
           "barrier.cluster.wait;",
           "barrier.cluster.wait.aligned;",
           "barrier.cluster.wait.acquire.aligned;",
       }) {
    expect_variant(source, Barrier::VariantType::ClusterWait);
  }

  for (const std::string_view source : {
           "barrier.cluster.arrive.acquire;",
           "barrier.cluster.wait.release;",
           "barrier.cluster.arrive.aligned.release;",
       }) {
    const auto selected = selectVariant<Barrier>(parse_instruction(source));
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(SelectVariantMatch, SelectsMatchSyncFormsAndRejectsInvalidOnes) {
  const auto expect_variant = [](std::string_view source,
                                 Match::VariantType expected) {
    const auto selected = selectVariant<Match>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("match.any.sync.b32 %b0, %b1, 0xffffffff;",
                 Match::VariantType::AnySync);
  expect_variant("match.any.sync.b64 %b0, %d0, %r0;",
                 Match::VariantType::AnySync);
  expect_variant("match.all.sync.b32 %b0, %b1, 0xffffffff;",
                 Match::VariantType::AllSync);
  expect_variant("match.all.sync.b64 %b0|%p0, %d0, %r0;",
                 Match::VariantType::AllSync);

  for (const std::string_view source : {
           "match.any.b32 %b0, %b1, 0xffffffff;",
           "match.sync.any.b32 %b0, %b1, 0xffffffff;",
           "match.all.sync.u32 %b0, %b1, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Match>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Match>(parse_instruction(
      "match.any.sync.b32 %b0|%p0, %b1, 0xffffffff;"))
                   .has_value());
  EXPECT_FALSE(resolve<Match>(parse_instruction(
      "match.all.sync.b32 _|%p0, %b1, 0xffffffff;"))
                   .has_value());
}

TEST(SelectVariantRedux, SelectsReduxSyncFormsAndRejectsInvalidOnes) {
  const auto expect_variant = [](std::string_view source,
                                 Redux::VariantType expected) {
    const auto selected = selectVariant<Redux>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("redux.sync.add.u32 %r0, %r1, 0xffffffff;",
                 Redux::VariantType::SyncAdd);
  expect_variant("redux.sync.min.s32 %r0, %r1, %r2;",
                 Redux::VariantType::SyncMin);
  expect_variant("redux.sync.max.u32 %r0, %r1, %r2;",
                 Redux::VariantType::SyncMax);
  for (const std::string_view source : {
           "redux.sync.and.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.or.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.xor.b32 %r0, %r1, 0xffffffff;",
       }) {
    expect_variant(source, Redux::VariantType::SyncBoolean);
  }
  for (const std::string_view source : {
           "redux.sync.min.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.abs.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.NaN.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.abs.NaN.f32 %f0, %f1, 0xffffffff;",
       }) {
    expect_variant(source, Redux::VariantType::SyncMinF32);
  }
  expect_variant("redux.sync.max.abs.NaN.f32 %f0, %f1, 0xffffffff;",
                 Redux::VariantType::SyncMaxF32);

  for (const std::string_view source : {
           "redux.sync.add.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.and.u32 %r0, %r1, 0xffffffff;",
           "redux.sync.min.NaN.abs.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.add.abs.u32 %r0, %r1, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Redux>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantGriddepcontrol, SelectsActionsAndRejectsInvalidForms) {
  const auto expect_variant = [](std::string_view source,
                                 Griddepcontrol::VariantType expected) {
    const auto selected = selectVariant<Griddepcontrol>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("griddepcontrol.launch_dependents;",
                 Griddepcontrol::VariantType::LaunchDependents);
  expect_variant("griddepcontrol.wait;", Griddepcontrol::VariantType::Wait);

  for (const std::string_view source : {
           "griddepcontrol;",
           "griddepcontrol.launch_dependents.wait;",
           "griddepcontrol.wait.sync;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Griddepcontrol>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Griddepcontrol>(
      parse_instruction("griddepcontrol.wait %r0;")).has_value());
}

TEST(SelectVariantCp, SelectsAsyncMbarrierArriveForms) {
  const auto expect_variant = [](std::string_view source, Cp::VariantType expected) {
    const auto selected = selectVariant<Cp>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("cp.async.mbarrier.arrive.b64 [%rd0];",
                 Cp::VariantType::AsyncMbarrierArriveGenericOrShared);
  expect_variant("cp.async.mbarrier.arrive.shared.b64 [shared_value];",
                 Cp::VariantType::AsyncMbarrierArriveGenericOrShared);
  expect_variant("cp.async.mbarrier.arrive.shared::cta.b64 [shared_value];",
                 Cp::VariantType::AsyncMbarrierArriveSharedCta);
  expect_variant("cp.async.mbarrier.arrive.noinc.b64 [%rd0];",
                 Cp::VariantType::AsyncMbarrierArriveNoincGenericOrShared);
  expect_variant("cp.async.mbarrier.arrive.noinc.shared::cta.b64 [shared_value];",
                 Cp::VariantType::AsyncMbarrierArriveNoincSharedCta);

  for (const std::string_view source : {
           "cp.async.mbarrier.arrive.shared::cluster.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b32 [%rd0];",
           "cp.async.mbarrier.arrive.noinc.noinc.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b64.noinc [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Cp>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Cp>(
      parse_instruction("cp.async.mbarrier.arrive.b64 [%rd0], 1;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsBasicTestWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.test_wait.b64 %p0, [%rd0], %state;",
                 Mbarrier::VariantType::TestWaitTokenGenericOrShared);
  expect_variant("mbarrier.test_wait.shared.b64 %p0, [shared_value], %state;",
                 Mbarrier::VariantType::TestWaitTokenGenericOrShared);
  expect_variant("mbarrier.test_wait.shared::cta.b64 %p0, [shared_value], %state;",
                 Mbarrier::VariantType::TestWaitTokenSharedCta);
  expect_variant("mbarrier.test_wait.parity.b64 %p0, [%rd0], 1;",
                 Mbarrier::VariantType::TestWaitParityGenericOrShared);
  expect_variant("mbarrier.test_wait.parity.shared::cta.b64 %p0, [shared_value], %r0;",
                 Mbarrier::VariantType::TestWaitParitySharedCta);

  for (const std::string_view source : {
           "mbarrier.test_wait.shared::cluster.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.b32 %p0, [%rd0], %state;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.test_wait.b64 %p0, [%rd0];")).has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.test_wait.b64 %p0, [%rd0], %state, 1;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsBasicTryWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.try_wait.b64 %p0, [%rd0], %state;",
                 Mbarrier::VariantType::TryWaitTokenGenericOrShared);
  expect_variant("mbarrier.try_wait.shared::cta.b64 %p0, [shared_value], %state, 1;",
                 Mbarrier::VariantType::TryWaitTokenSharedCta);
  expect_variant("mbarrier.try_wait.parity.b64 %p0, [%rd0], 1;",
                 Mbarrier::VariantType::TryWaitParityGenericOrShared);
  expect_variant("mbarrier.try_wait.parity.shared::cta.b64 %p0, [shared_value], %r0, %r1;",
                 Mbarrier::VariantType::TryWaitParitySharedCta);

  EXPECT_FALSE(selectVariant<Mbarrier>(
      parse_instruction("mbarrier.try_wait.b32 %p0, [%rd0], %state;")).has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.try_wait.b64 %p0, [%rd0];")).has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.try_wait.b64 %p0, [%rd0], %state, 1, 2;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsPhaseAndReportWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.test_wait.phase_type::primary.b64 %p0, [%rd0], %state;",
                 Mbarrier::VariantType::TestWaitTokenPrimaryGenericOrShared);
  expect_variant("mbarrier.test_wait.phase_type::primary.shared::cta.b64 %p0|%p1, %b0, [shared_value], %state;",
                 Mbarrier::VariantType::TestWaitTokenPrimarySharedCta);
  expect_variant("mbarrier.test_wait.parity.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], 1;",
                 Mbarrier::VariantType::TestWaitParityPrimaryGenericOrShared);
  expect_variant("mbarrier.test_wait.parity.phase_type::primary.shared::cta.b64 %p0, [shared_value], 1;",
                 Mbarrier::VariantType::TestWaitParityPrimarySharedCta);
  expect_variant("mbarrier.test_wait.parity.phase_type::conditional.b64 %p0, [%rd0], 1;",
                 Mbarrier::VariantType::TestWaitParityConditionalGenericOrShared);
  expect_variant("mbarrier.test_wait.parity.phase_type::conditional.shared::cta.b64 %p0, [shared_value], 1;",
                 Mbarrier::VariantType::TestWaitParityConditionalSharedCta);
  expect_variant("mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], %state, 1;",
                 Mbarrier::VariantType::TryWaitTokenPrimaryGenericOrShared);
  expect_variant("mbarrier.try_wait.phase_type::primary.shared::cta.b64 %p0, [shared_value], %state;",
                 Mbarrier::VariantType::TryWaitTokenPrimarySharedCta);
  expect_variant("mbarrier.try_wait.parity.phase_type::primary.b64 %p0|%p1, %b0, [%rd0], 1, 2;",
                 Mbarrier::VariantType::TryWaitParityPrimaryGenericOrShared);
  expect_variant("mbarrier.try_wait.parity.phase_type::primary.shared::cta.b64 %p0, [shared_value], 1;",
                 Mbarrier::VariantType::TryWaitParityPrimarySharedCta);
  expect_variant("mbarrier.try_wait.parity.phase_type::conditional.b64 %p0, [%rd0], 1, 2;",
                 Mbarrier::VariantType::TryWaitParityConditionalGenericOrShared);
  expect_variant("mbarrier.try_wait.parity.phase_type::conditional.shared::cta.b64 %p0, [shared_value], 1;",
                 Mbarrier::VariantType::TryWaitParityConditionalSharedCta);

  for (const std::string_view source : {
           "mbarrier.test_wait.phase_type::conditional.b64 %p0, [%rd0], %state;",
           "mbarrier.test_wait.parity.phase_type::conditional.b64 %p0|%p1, [%rd0], 1;",
           "mbarrier.try_wait.phase_type::primary.b64 %p0, %b0, [%rd0], %state;",
           "mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, _, [%rd0], %state;",
           "mbarrier.try_wait.phase_type::primary.b64 %p0|%p1, 1, [%rd0], %state;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsPendingCount) {
  const auto expect_variant = [](std::string_view source) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, Mbarrier::VariantType::PendingCount);
  };
  expect_variant("mbarrier.pending_count.b64 %r0, %state;");
  expect_variant("mbarrier.pending_count.layout::v0.b64 %r0, %state;");

  for (const std::string_view source : {
           "mbarrier.pending_count.layout::v1.b64 %r0, %state;",
           "mbarrier.pending_count.b32 %r0, %state;",
           "mbarrier.pending_count.b64 %r0, %state, 1;",
           "mbarrier.pending_count.shared.b64 %r0, %state;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
  for (const std::string_view source : {
           "mbarrier.pending_count.b64 _, %state;",
           "mbarrier.pending_count.b64 1, %state;",
           "mbarrier.pending_count.b64 %r0, _;",
           "mbarrier.pending_count.b64 %r0, 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMapa, SelectsSharedClusterAndGenericForms) {
  const auto expect_variant = [](std::string_view source,
                                 Mapa::VariantType expected) {
    const auto selected = selectVariant<Mapa>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mapa.shared::cluster.u32 %r0, %r1, 0;",
                 Mapa::VariantType::SharedCluster);
  expect_variant("mapa.shared::cluster.u64 %rd0, shared_value+4, %r0;",
                 Mapa::VariantType::SharedCluster);
  expect_variant("mapa.u32 %r0, %r1, 0;", Mapa::VariantType::Generic);
  expect_variant("mapa.u64 %rd0, %rd1, %r0;", Mapa::VariantType::Generic);

  for (const std::string_view source : {
           "mapa.shared::cluster %r0, %r1, 0;",
           "mapa.u32.shared::cluster %r0, %r1, 0;",
           "mapa.shared::cluster.u32.u64 %r0, %r1, 0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mapa>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mapa>(
      parse_instruction("mapa.shared::cluster.u32 %r0, %r1;")).has_value());
}

TEST(SelectVariantGetctarank, SelectsSharedClusterAndGenericForms) {
  const auto expect_variant = [](std::string_view source,
                                 Getctarank::VariantType expected) {
    const auto selected = selectVariant<Getctarank>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("getctarank.shared::cluster.u32 %r0, %r1;",
                 Getctarank::VariantType::SharedCluster);
  expect_variant("getctarank.shared::cluster.u64 %r0, shared_value+4;",
                 Getctarank::VariantType::SharedCluster);
  expect_variant("getctarank.u32 %r0, %r1;", Getctarank::VariantType::Generic);
  expect_variant("getctarank.u64 %r0, %rd1;", Getctarank::VariantType::Generic);

  for (const std::string_view source : {
           "getctarank.shared::cluster %r0, %r1;",
           "getctarank.u32.shared::cluster %r0, %r1;",
           "getctarank.shared::cluster.u32.u64 %r0, %r1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Getctarank>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Getctarank>(
      parse_instruction("getctarank.shared::cluster.u32 %r0;")).has_value());
}

TEST(SelectVariantElect, SelectsAndResolvesOptionalDataDestination) {
  for (const std::string_view source : {
           "elect.sync %lane|%p, 0xffffffff;",
           "elect.sync _|%p, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    const auto selected = selectVariant<Elect>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, Elect::VariantType::Sync);
  }
  for (const std::string_view source : {
           "elect %lane|%p, 0xffffffff;",
           "elect.sync.abs %lane|%p, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Elect>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsInitLayoutsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.init.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitGenericV0);
  expect_variant("mbarrier.init.shared.b64 [%rd0], %r0;",
                 Mbarrier::VariantType::InitSharedV0);
  expect_variant("mbarrier.init.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedCtaV0);
  expect_variant("mbarrier.init.layout::v1.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitGenericV1);
  expect_variant("mbarrier.init.layout::v1.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedV1);
  expect_variant("mbarrier.init.layout::v1.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::InitSharedCtaV1);

  for (const std::string_view source : {
           "mbarrier.init.b64.shared [%rd0], 1;",
           "mbarrier.init.shared.layout::v1.b64 [%rd0], 1;",
           "mbarrier.init.shared.shared::cta.b64 [%rd0], 1;",
           "mbarrier.init.layout::v0.layout::v1.b64 [%rd0], 1;",
           "mbarrier.shared.b64 [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.init.b64 [%rd0];")).has_value());
}

TEST(SelectVariantMbarrier, SelectsInvalSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.inval.b64 [%rd0];",
                 Mbarrier::VariantType::InvalGeneric);
  expect_variant("mbarrier.inval.shared.b64 [%rd0];",
                 Mbarrier::VariantType::InvalShared);
  expect_variant("mbarrier.inval.shared::cta.b64 [%rd0];",
                 Mbarrier::VariantType::InvalSharedCta);

  for (const std::string_view source : {
           "mbarrier.inval [%rd0];",
           "mbarrier.inval.b64.shared [%rd0];",
           "mbarrier.inval.shared.shared::cta.b64 [%rd0];",
           "mbarrier.inval.shared::cta.shared.b64 [%rd0];",
           "mbarrier.inval.layout::v0.b64 [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.inval.b64;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsExpectTxSemanticsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.expect_tx.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxGenericOrShared);
  expect_variant("mbarrier.expect_tx.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxGenericOrShared);
  expect_variant("mbarrier.expect_tx.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxSharedCta);
  expect_variant("mbarrier.expect_tx.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxSharedCluster);
  expect_variant("mbarrier.expect_tx.relaxed.cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cta.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cta.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaSharedCta);
  expect_variant("mbarrier.expect_tx.relaxed.cta.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedCtaSharedCluster);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterGenericOrShared);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterSharedCta);
  expect_variant("mbarrier.expect_tx.relaxed.cluster.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::ExpectTxRelaxedClusterSharedCluster);

  for (const std::string_view source : {
           "mbarrier.expect_tx.relaxed.b64 [%rd0], 1;",
           "mbarrier.expect_tx.cta.b64 [%rd0], 1;",
           "mbarrier.expect_tx.relaxed.gpu.b64 [%rd0], 1;",
           "mbarrier.expect_tx.relaxed.cta.global.b64 [%rd0], 1;",
           "mbarrier.expect_tx.shared.relaxed.cta.b64 [%rd0], 1;",
           "mbarrier.expect_tx.shared [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.expect_tx.b64 [%rd0];")).has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.expect_tx.b64 [%rd0], %tid.x;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsCompleteTxSemanticsAndSpaces) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.complete_tx.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxGenericOrShared);
  expect_variant("mbarrier.complete_tx.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxGenericOrShared);
  expect_variant("mbarrier.complete_tx.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxSharedCta);
  expect_variant("mbarrier.complete_tx.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxSharedCluster);
  expect_variant("mbarrier.complete_tx.relaxed.cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cta.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cta.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaSharedCta);
  expect_variant("mbarrier.complete_tx.relaxed.cta.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedCtaSharedCluster);
  expect_variant("mbarrier.complete_tx.relaxed.cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedClusterGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cluster.shared.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedClusterGenericOrShared);
  expect_variant("mbarrier.complete_tx.relaxed.cluster.shared::cta.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedClusterSharedCta);
  expect_variant("mbarrier.complete_tx.relaxed.cluster.shared::cluster.b64 [%rd0], 1;",
                 Mbarrier::VariantType::CompleteTxRelaxedClusterSharedCluster);

  for (const std::string_view source : {
           "mbarrier.complete_tx.relaxed.b64 [%rd0], 1;",
           "mbarrier.complete_tx.cta.b64 [%rd0], 1;",
           "mbarrier.complete_tx.relaxed.gpu.b64 [%rd0], 1;",
           "mbarrier.complete_tx.relaxed.cta.global.b64 [%rd0], 1;",
           "mbarrier.complete_tx.shared.relaxed.cta.b64 [%rd0], 1;",
           "mbarrier.complete_tx.shared [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.complete_tx.b64 [%rd0];")).has_value());
  EXPECT_FALSE(resolve<Mbarrier>(
      parse_instruction("mbarrier.complete_tx.b64 [%rd0], %tid.x;")).has_value());
}

TEST(SelectVariantMbarrier, SelectsArriveFormsAndLayouts) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  const auto sink_ast = parse_instruction("mbarrier.arrive.b64 _, [%rd0];");
  const auto* sink = std::get_if<syntax_ast::AstIdentifierRef>(&sink_ast.operands[0]);
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(sink->syntax.text, "_");

  expect_variant("mbarrier.arrive.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveGenericOrShared);
  expect_variant("mbarrier.arrive.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveSharedCta);
  expect_variant("mbarrier.arrive.shared::cluster.b64 _, [%rd0];",
                 Mbarrier::VariantType::ArriveSharedCluster);
  expect_variant("mbarrier.arrive.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveSemanticsGenericOrShared);
  expect_variant("mbarrier.arrive.release.cluster.shared::cta.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveSemanticsSharedCta);
  expect_variant("mbarrier.arrive.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveSemanticsSharedCluster);
  expect_variant("mbarrier.arrive.expect_tx.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxGenericOrShared);
  expect_variant("mbarrier.arrive.expect_tx.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSharedCta);
  expect_variant("mbarrier.arrive.expect_tx.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSharedCluster);
  expect_variant("mbarrier.arrive.expect_tx.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSemanticsGenericOrShared);
  expect_variant("mbarrier.arrive.expect_tx.release.cluster.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSemanticsSharedCta);
  expect_variant("mbarrier.arrive.expect_tx.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveExpectTxSemanticsSharedCluster);
  expect_variant("mbarrier.arrive.noComplete.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteGenericOrShared);
  expect_variant("mbarrier.arrive.noComplete.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteSharedCta);
  expect_variant("mbarrier.arrive.noComplete.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteReleaseCtaGenericOrShared);
  expect_variant("mbarrier.arrive.noComplete.release.cta.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveNoCompleteReleaseCtaSharedCta);

  for (const std::string_view source : {
           "mbarrier.arrive.release.b64 %state, [%rd0];",
           "mbarrier.arrive.cta.b64 %state, [%rd0];",
           "mbarrier.arrive.noComplete.relaxed.cta.b64 %state, [%rd0], 1;",
           "mbarrier.arrive.expect_tx.noComplete.b64 %state, [%rd0], 1;",
           "mbarrier.arrive.b64.shared %state, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantMbarrier, SelectsArriveDropFormsAndLayouts) {
  const auto expect_variant = [](std::string_view source,
                                 Mbarrier::VariantType expected) {
    const auto selected = selectVariant<Mbarrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("mbarrier.arrive_drop.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveDropGenericOrShared);
  expect_variant("mbarrier.arrive_drop.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropSharedCta);
  expect_variant("mbarrier.arrive_drop.shared::cluster.b64 _, [%rd0];",
                 Mbarrier::VariantType::ArriveDropSharedCluster);
  expect_variant("mbarrier.arrive_drop.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropSemanticsGenericOrShared);
  expect_variant("mbarrier.arrive_drop.release.cluster.shared::cta.b64 %state, [%rd0];",
                 Mbarrier::VariantType::ArriveDropSemanticsSharedCta);
  expect_variant("mbarrier.arrive_drop.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropSemanticsSharedCluster);
  expect_variant("mbarrier.arrive_drop.expect_tx.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxGenericOrShared);
  expect_variant("mbarrier.arrive_drop.expect_tx.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSharedCta);
  expect_variant("mbarrier.arrive_drop.expect_tx.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSharedCluster);
  expect_variant("mbarrier.arrive_drop.expect_tx.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSemanticsGenericOrShared);
  expect_variant("mbarrier.arrive_drop.expect_tx.release.cluster.shared::cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSemanticsSharedCta);
  expect_variant("mbarrier.arrive_drop.expect_tx.relaxed.cta.shared::cluster.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropExpectTxSemanticsSharedCluster);
  expect_variant("mbarrier.arrive_drop.noComplete.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropNoCompleteGenericOrShared);
  expect_variant("mbarrier.arrive_drop.noComplete.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropNoCompleteSharedCta);
  expect_variant("mbarrier.arrive_drop.noComplete.release.cta.b64 %state, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropNoCompleteReleaseCtaGenericOrShared);
  expect_variant("mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 _, [%rd0], 1;",
                 Mbarrier::VariantType::ArriveDropNoCompleteReleaseCtaSharedCta);

  for (const std::string_view source : {
           "mbarrier.arrive_drop.release.b64 %state, [%rd0];",
           "mbarrier.arrive_drop.cta.b64 %state, [%rd0];",
           "mbarrier.arrive_drop.noComplete.relaxed.cta.b64 %state, [%rd0], 1;",
           "mbarrier.arrive_drop.expect_tx.noComplete.b64 %state, [%rd0], 1;",
           "mbarrier.arrive_drop.b64.shared %state, [%rd0];",
           "mbarrier.arrive_drop.noComplete.shared::cluster.b64 _, [%rd0], 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Mbarrier>(parse_instruction(source)).has_value());
  }
}

TEST(SelectVariantLoadStore, SelectsLegalCacheOperatorsAndRejectsWrongOnes) {
  const auto expect_load = [](std::string_view source, Ld::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<Ld>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_load("ld.ca.u32 %r0, [%rd0];", Ld::VariantType::GenericScalar);
  expect_load("ld.global.cv.u32 %r0, [%rd0];", Ld::VariantType::ExplicitScalar);
  expect_load("ld.cg.v2.u32 {%r0, %r1}, [%rd0];",
              Ld::VariantType::GenericVector);
  expect_load("ld.shared.ca.v4.u16 {%h0, %h1, %h2, %h3}, [%rd0];",
              Ld::VariantType::ExplicitVector);

  const auto invalid_load = parse_instruction("ld.wb.u32 %r0, [%rd0];");
  const auto invalid_load_selected = selectVariant<Ld>(invalid_load);
  ASSERT_FALSE(invalid_load_selected.has_value());
  EXPECT_EQ(invalid_load_selected.error().range,
            invalid_load.modifiers.front().syntax.range);
  EXPECT_EQ(invalid_load_selected.error().message, "Unknown modifier '.wb'.");

  const auto expect_store = [](std::string_view source,
                               St::VariantType expected) {
    const auto ast = parse_instruction(source);
    const auto selected = selectVariant<St>(ast);
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_store("st.wt.u32 [%rd0], %r0;", St::VariantType::GenericScalar);
  expect_store("st.global.cg.u32 [%rd0], %r0;", St::VariantType::ExplicitScalar);
  expect_store("st.wt.v2.u32 [%rd0], {%r0, %r1};",
               St::VariantType::GenericVector);
  expect_store("st.shared.cg.v4.u16 [%rd0], {%h0, %h1, %h2, %h3};",
               St::VariantType::ExplicitVector);

  const auto invalid_store = parse_instruction("st.ca.u32 [%rd0], %r0;");
  const auto invalid_store_selected = selectVariant<St>(invalid_store);
  ASSERT_FALSE(invalid_store_selected.has_value());
  EXPECT_EQ(invalid_store_selected.error().range,
            invalid_store.modifiers.front().syntax.range);
  EXPECT_EQ(invalid_store_selected.error().message, "Unknown modifier '.ca'.");

  const auto modern_vector = parse_instruction(
      "ld.v8.u32 {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];");
  const auto modern_vector_selected = selectVariant<Ld>(modern_vector);
  ASSERT_TRUE(modern_vector_selected.has_value())
      << modern_vector_selected.error().message;
  EXPECT_EQ(*modern_vector_selected, Ld::VariantType::GenericVector);

  const auto vector_mmio = parse_instruction(
      "ld.mmio.relaxed.sys.v2.u32 {%r0, %r1}, [%rd0];");
  const auto vector_mmio_selected = selectVariant<Ld>(vector_mmio);
  ASSERT_FALSE(vector_mmio_selected.has_value());
  EXPECT_EQ(vector_mmio_selected.error().message,
            "No variant of instruction 'ld' accepts this modifier combination.");
}

TEST(SelectVariantAdd, ReportsUnknownModifier) {
  const auto ast = parse_instruction("add.invalid %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.modifiers.front().syntax.range);
  EXPECT_EQ(selected.error().message, "Unknown modifier '.invalid'.");
}

TEST(ResolveRet, SelectsBareVariantAndRejectsModifiersAndOperands) {
  const auto bare_ast = parse_instruction("ret;");
  const auto bare = resolve<Ret>(bare_ast);
  ASSERT_TRUE(bare.has_value()) << bare.error().message;
  EXPECT_TRUE(std::holds_alternative<Ret::Bare>(bare->variant));

  const auto modifier_ast = parse_instruction("ret.uni;");
  const auto modifier = resolve<Ret>(modifier_ast);
  ASSERT_FALSE(modifier.has_value());
  EXPECT_EQ(modifier.error().range,
            modifier_ast.modifiers.front().syntax.range);
  EXPECT_EQ(modifier.error().message, "Unknown modifier '.uni'.");

  const auto operand_ast = parse_instruction("ret %r0;");
  const auto operand = resolve<Ret>(operand_ast);
  ASSERT_FALSE(operand.has_value());
  EXPECT_EQ(operand.error().range, operand_ast.range);
  EXPECT_EQ(operand.error().message,
            "Operands do not match any layout of instruction variant 'Bare'.");
}

TEST(ResolveExit, SelectsBareAndPredicatedVariantsAndRejectsInvalidSyntax) {
  const auto bare_ast = parse_instruction("exit;");
  const auto bare = resolve<Exit>(bare_ast);
  ASSERT_TRUE(bare.has_value()) << bare.error().message;
  EXPECT_TRUE(std::holds_alternative<Exit::Bare>(bare->variant));
  EXPECT_FALSE(bare->execution_predicate.has_value());

  const auto predicated_ast = parse_instruction("@%p0 exit;");
  const auto predicated = resolve<Exit>(predicated_ast);
  ASSERT_TRUE(predicated.has_value()) << predicated.error().message;
  EXPECT_TRUE(std::holds_alternative<Exit::Bare>(predicated->variant));
  EXPECT_TRUE(predicated->execution_predicate.has_value());

  const auto modifier_ast = parse_instruction("exit.uni;");
  const auto modifier = resolve<Exit>(modifier_ast);
  ASSERT_FALSE(modifier.has_value());
  EXPECT_EQ(modifier.error().range,
            modifier_ast.modifiers.front().syntax.range);
  EXPECT_EQ(modifier.error().message, "Unknown modifier '.uni'.");

  const auto operand_ast = parse_instruction("exit %r0;");
  const auto operand = resolve<Exit>(operand_ast);
  ASSERT_FALSE(operand.has_value());
  EXPECT_EQ(operand.error().range, operand_ast.range);
  EXPECT_EQ(operand.error().message,
            "Operands do not match any layout of instruction variant 'Bare'.");
}

TEST(ResolveTrap, SelectsBareAndPredicatedVariantsAndRejectsInvalidSyntax) {
  const auto bare_ast = parse_instruction("trap;");
  const auto bare = resolve<Trap>(bare_ast);
  ASSERT_TRUE(bare.has_value()) << bare.error().message;
  EXPECT_TRUE(std::holds_alternative<Trap::Bare>(bare->variant));
  EXPECT_FALSE(bare->execution_predicate.has_value());

  const auto predicated_ast = parse_instruction("@%p0 trap;");
  const auto predicated = resolve<Trap>(predicated_ast);
  ASSERT_TRUE(predicated.has_value()) << predicated.error().message;
  EXPECT_TRUE(std::holds_alternative<Trap::Bare>(predicated->variant));
  EXPECT_TRUE(predicated->execution_predicate.has_value());

  const auto modifier_ast = parse_instruction("trap.uni;");
  const auto modifier = resolve<Trap>(modifier_ast);
  ASSERT_FALSE(modifier.has_value());
  EXPECT_EQ(modifier.error().range,
            modifier_ast.modifiers.front().syntax.range);
  EXPECT_EQ(modifier.error().message, "Unknown modifier '.uni'.");

  const auto operand_ast = parse_instruction("trap %r0;");
  const auto operand = resolve<Trap>(operand_ast);
  ASSERT_FALSE(operand.has_value());
  EXPECT_EQ(operand.error().range, operand_ast.range);
  EXPECT_EQ(operand.error().message,
            "Operands do not match any layout of instruction variant 'Bare'.");
}

TEST(SelectVariantAdd, ReportsDuplicateModifierKind) {
  const auto ast = parse_instruction("add.u32.u32 %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.modifiers.back().syntax.range);
  EXPECT_EQ(selected.error().message, "Duplicate 'type' modifier.");
}

TEST(ResolveAnd, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("and.b32 %r0, %r1, 1;");
  const auto resolved = resolve<And>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* and_b32 = std::get_if<And::B32>(&resolved->variant);
  ASSERT_NE(and_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(and_b32->src2.value));
}

TEST(ResolveOr, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("or.b32 %r0, %r1, 1;");
  const auto resolved = resolve<Or>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* or_b32 = std::get_if<Or::B32>(&resolved->variant);
  ASSERT_NE(or_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(or_b32->src2.value));
}

TEST(ResolveXor, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("xor.b32 %r0, %r1, 1;");
  const auto resolved = resolve<Xor>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* xor_b32 = std::get_if<Xor::B32>(&resolved->variant);
  ASSERT_NE(xor_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(xor_b32->src2.value));
}

TEST(ResolveNot, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("not.b32 %r0, 1;");
  const auto resolved = resolve<Not>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* not_b32 = std::get_if<Not::B32>(&resolved->variant);
  ASSERT_NE(not_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(not_b32->src.value));
}

TEST(ResolveShl, SelectsB32VariantAndAcceptsImmediateAmount) {
  const auto ast = parse_instruction("shl.b32 %r0, %r1, 1;");
  const auto resolved = resolve<Shl>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* shl_b32 = std::get_if<Shl::B32>(&resolved->variant);
  ASSERT_NE(shl_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(shl_b32->amount.value));
}

TEST(ResolveShr, SelectsU32VariantAndAcceptsImmediateAmount) {
  const auto ast = parse_instruction("shr.u32 %r0, %r1, 1;");
  const auto resolved = resolve<Shr>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* shr_u32 = std::get_if<Shr::U32>(&resolved->variant);
  ASSERT_NE(shr_u32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(shr_u32->amount.value));
}

TEST(ResolveSet, SelectsFrozenCommonScalarVariants) {
  const auto eq = resolve<Set>(parse_instruction("set.eq.u32.u32 %r0, %r1, 16;"));
  ASSERT_TRUE(eq.has_value()) << eq.error().message;
  const auto* eq_u32_u32 = std::get_if<Set::EqU32U32>(&eq->variant);
  ASSERT_NE(eq_u32_u32, nullptr);
  EXPECT_EQ(eq_u32_u32->comparison.value, ComparisonOperator::Eq);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(eq_u32_u32->src2.value));

  const auto lt_and = resolve<Set>(
      parse_instruction("set.lt.and.f32.s32 %f0, %s0, -1, !%p0;"));
  ASSERT_TRUE(lt_and.has_value()) << lt_and.error().message;
  const auto* lt_and_f32_s32 = std::get_if<Set::LtAndF32S32>(&lt_and->variant);
  ASSERT_NE(lt_and_f32_s32, nullptr);
  EXPECT_EQ(lt_and_f32_s32->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and_f32_s32->boolean.value, BooleanOperator::And);
  EXPECT_TRUE(lt_and_f32_s32->combine.value.negated);
}

TEST(ResolveSet, RejectsUnfrozenDtypeStypeAndBooleanForms) {
  for (const auto source : {
           "set.eq.f32.u32 %f0, %r0, %r1;",
           "set.eq.and.u32.u32 %r0, %r1, %r2, %p0;",
           "set.lt.and.f32.u32 %f0, %r0, %r1, %p0;",
       }) {
    const auto selected = selectVariant<Set>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveSetp, SelectsFrozenLtU32Variants) {
  const auto simple_ast = parse_instruction("setp.lt.u32 %p0, %r0, 16;");
  const auto simple = resolve<Setp>(simple_ast);
  ASSERT_TRUE(simple.has_value()) << simple.error().message;
  const auto* lt = std::get_if<Setp::LtU32>(&simple->variant);
  ASSERT_NE(lt, nullptr);
  EXPECT_EQ(lt->comparison.value, ComparisonOperator::Lt);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(lt->src2.value));

  const auto combined_ast =
      parse_instruction("setp.lt.and.u32 %p0, %r0, 16, !%p1;");
  const auto combined = resolve<Setp>(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and = std::get_if<Setp::LtAndU32>(&combined->variant);
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  EXPECT_TRUE(lt_and->combine.value.negated);
}

TEST(ResolveSetp, SelectsM12GeS32Variant) {
  const auto resolved =
      resolve<Setp>(parse_instruction("setp.ge.s32 %p0, %r0, -1;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* ge = std::get_if<Setp::GeS32>(&resolved->variant);
  ASSERT_NE(ge, nullptr);
  EXPECT_EQ(ge->comparison.value, ComparisonOperator::Ge);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(ge->src2.value));
}

TEST(ResolveSetp, SelectsFrozenDualPredicateVariants) {
  const auto equality_ast =
      parse_instruction("setp.eq.u32 %p0|%p1, %r0, %r1;");
  const auto equality = resolve<Setp>(equality_ast);
  ASSERT_TRUE(equality.has_value()) << equality.error().message;
  const auto* eq = std::get_if<Setp::EqU32Pair>(&equality->variant);
  ASSERT_NE(eq, nullptr);
  EXPECT_EQ(eq->comparison.value, ComparisonOperator::Eq);
  EXPECT_EQ(eq->dst.value.first.register_ref.spelling, "%p0");
  EXPECT_EQ(eq->dst.value.second.register_ref.spelling, "%p1");

  const auto combined_ast =
      parse_instruction("setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2;");
  const auto combined = resolve<Setp>(combined_ast);
  ASSERT_TRUE(combined.has_value()) << combined.error().message;
  const auto* lt_and = std::get_if<Setp::LtAndS32Pair>(&combined->variant);
  ASSERT_NE(lt_and, nullptr);
  EXPECT_EQ(lt_and->comparison.value, ComparisonOperator::Lt);
  EXPECT_EQ(lt_and->boolean.value, BooleanOperator::And);
  EXPECT_FALSE(lt_and->combine.value.negated);
  EXPECT_EQ(lt_and->combine.value.register_ref.spelling, "%p2");
}

TEST(ResolveSetp, RejectsUnfrozenDualPredicateForms) {
  for (const auto source : {
           "setp.eq.u32 %p0|_, %r0, %r1;",
           "setp.lt.and.s32 %p0|%p1, %s0, %s1, !%p2;",
       }) {
    const auto selected = resolve<Setp>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }

  const auto non_predicate =
      resolve<Setp>(parse_instruction("setp.eq.u32 %r0|%p1, %r0, %r1;"));
  EXPECT_FALSE(non_predicate.has_value());
}

TEST(ResolveSetp, RejectsUnfrozenGeS32Forms) {
  for (const auto source : {
           "setp.ge.u32 %p0, %r0, %r1;",
           "setp.ge.and.s32 %p0, %r0, %r1, %p1;",
           "setp.ge.s32 %p0|%p1, %r0, %r1;",
           "setp.ge.s32 %r0, %r1, %r2;",
       }) {
    const auto selected = resolve<Setp>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveSelp, SelectsFrozenU32Variant) {
  const auto ast = parse_instruction("selp.u32 %r0, %r1, 0, %p0;");
  const auto resolved = resolve<Selp>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* selp = std::get_if<Selp::U32>(&resolved->variant);
  ASSERT_NE(selp, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(selp->src_false.value));
  EXPECT_FALSE(selp->predicate.value.negated);
}

TEST(ResolveSlct, SelectsFrozenNumericSelectorVariants) {
  const auto integer =
      resolve<Slct>(parse_instruction("slct.u32.s32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(integer.has_value()) << integer.error().message;
  const auto* u32_s32 = std::get_if<Slct::U32S32>(&integer->variant);
  ASSERT_NE(u32_s32, nullptr);
  EXPECT_EQ(u32_s32->selector.value.register_class,
            ResolvedRegisterClass::General);

  const auto floating = resolve<Slct>(
      parse_instruction("slct.ftz.u64.f32 %rd0, %rd1, %rd2, %f0;"));
  ASSERT_TRUE(floating.has_value()) << floating.error().message;
  EXPECT_NE(std::get_if<Slct::FtzU64F32>(&floating->variant), nullptr);
  EXPECT_TRUE(Slct::FtzU64F32::ftz);
}

TEST(ResolveSlct, RejectsUnfrozenModifierForms) {
  for (const auto source : {
           "slct.ftz.u32.s32 %r0, %r1, %r2, %r3;",
           "slct.u64.f32 %rd0, %rd1, %rd2, %f0;",
           "slct.ftz.u64.s32 %rd0, %rd1, %rd2, %r0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Slct>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveCvta, SelectsFrozenGlobalU64Variants) {
  const auto to_generic = resolve<Cvta>(parse_instruction("cvta.global.u64 %rd0, %rd1;"));
  ASSERT_TRUE(to_generic.has_value()) << to_generic.error().message;
  const auto* global = std::get_if<Cvta::GlobalU64>(&to_generic->variant);
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(Cvta::GlobalU64::state_space, MemoryStateSpace::Global);
  EXPECT_EQ(Cvta::GlobalU64::type, ScalarType::U64);

  const auto to_global =
      resolve<Cvta>(parse_instruction("cvta.to.global.u64 %rd0, %rd1;"));
  ASSERT_TRUE(to_global.has_value()) << to_global.error().message;
  const auto* to = std::get_if<Cvta::ToGlobalU64>(&to_global->variant);
  ASSERT_NE(to, nullptr);
  EXPECT_TRUE(Cvta::ToGlobalU64::to);
}

TEST(ResolveCvta, RejectsWrongModifierOrderOrU32) {
  for (const auto source : {"cvta.global.to.u64 %rd0, %rd1;",
                            "cvta.u64.global %rd0, %rd1;",
                            "cvta.global.u32 %r0, %r1;",
                            "cvta.to.global.u32 %r0, %r1;"}) {
    const auto selected = selectVariant<Cvta>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveMul, SelectsFrozenLoU32VariantAndImmediateSource) {
  const auto resolved = resolve<Mul>(parse_instruction("mul.lo.u32 %r0, %r1, 7;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mul = std::get_if<Mul::LoU32>(&resolved->variant);
  ASSERT_NE(mul, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mul->src2.value));
}

TEST(ResolveMul, SelectsM12HiAndWideU32Variants) {
  const auto hi = resolve<Mul>(parse_instruction("mul.hi.u32 %r0, %r1, %r2;"));
  ASSERT_TRUE(hi.has_value()) << hi.error().message;
  const auto* hi_variant = std::get_if<Mul::HiU32>(&hi->variant);
  ASSERT_NE(hi_variant, nullptr);
  EXPECT_TRUE(Mul::HiU32::hi);
  EXPECT_EQ(Mul::HiU32::type, ScalarType::U32);

  const auto wide =
      resolve<Mul>(parse_instruction("mul.wide.u32 %rd0, %r1, %r2;"));
  ASSERT_TRUE(wide.has_value()) << wide.error().message;
  const auto* wide_variant = std::get_if<Mul::WideU32>(&wide->variant);
  ASSERT_NE(wide_variant, nullptr);
  EXPECT_TRUE(Mul::WideU32::wide);
  EXPECT_EQ(Mul::WideU32::type, ScalarType::U32);
}

TEST(ResolveMul, SelectsM12WideS32Variant) {
  const auto resolved =
      resolve<Mul>(parse_instruction("mul.wide.s32 %rd0, %r1, -7;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* wide = std::get_if<Mul::WideS32>(&resolved->variant);
  ASSERT_NE(wide, nullptr);
  EXPECT_TRUE(Mul::WideS32::wide);
  EXPECT_EQ(Mul::WideS32::type, ScalarType::S32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(wide->src2.value));
}

TEST(ResolveMul, RejectsUnfrozenVariants) {
  for (const auto source : {"mul.u32 %r0, %r1, %r2;",
                            "mul.lo.s32 %r0, %r1, %r2;",
                            "mul.wide.s64 %rd0, %r1, %r2;"}) {
    const auto selected = selectVariant<Mul>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveMul, SelectsFrozenRnF32Variant) {
  const auto resolved = resolve<Mul>(parse_instruction("mul.rn.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Mul::RnF32>(&resolved->variant), nullptr);
  EXPECT_EQ(Mul::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Mul::RnF32::type, ScalarType::F32);
}

TEST(ResolveMul, RejectsUnfrozenFloatingVariants) {
  for (const auto source : {"mul.f32 %f0, %f1, %f2;",
                            "mul.rz.f32 %f0, %f1, %f2;",
                            "mul.rn.f64 %fd0, %fd1, %fd2;"}) {
    const auto selected = selectVariant<Mul>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveMul, RejectsImmediateFloatingOperand) {
  const auto resolved = resolve<Mul>(parse_instruction("mul.rn.f32 %f0, 1.0, %f2;"));
  ASSERT_FALSE(resolved.has_value());
}

TEST(ResolveMad, SelectsFrozenLoU32VariantAndImmediateSource) {
  const auto resolved = resolve<Mad>(parse_instruction("mad.lo.u32 %r0, %r1, 7, %r2;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* mad = std::get_if<Mad::LoU32>(&resolved->variant);
  ASSERT_NE(mad, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mad->src2.value));
}

TEST(ResolveMad, SelectsM12LoWideAndRnVariants) {
  const auto lo =
      resolve<Mad>(parse_instruction("mad.lo.s32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(lo.has_value()) << lo.error().message;
  ASSERT_NE(std::get_if<Mad::LoS32>(&lo->variant), nullptr);
  EXPECT_TRUE(Mad::LoS32::lo);
  EXPECT_EQ(Mad::LoS32::type, ScalarType::S32);

  const auto wide = resolve<Mad>(
      parse_instruction("mad.wide.u32 %rd0, %r1, %r2, %rd3;"));
  ASSERT_TRUE(wide.has_value()) << wide.error().message;
  ASSERT_NE(std::get_if<Mad::WideU32>(&wide->variant), nullptr);
  EXPECT_TRUE(Mad::WideU32::wide);
  EXPECT_EQ(Mad::WideU32::type, ScalarType::U32);

  const auto rn =
      resolve<Mad>(parse_instruction("mad.rn.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(rn.has_value()) << rn.error().message;
  ASSERT_NE(std::get_if<Mad::RnF32>(&rn->variant), nullptr);
  EXPECT_EQ(Mad::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Mad::RnF32::type, ScalarType::F32);
}

TEST(ResolveMad, RejectsUnfrozenVariants) {
  for (const auto source : {"mad.u32 %r0, %r1, %r2, %r3;",
                            "mad.hi.u32 %r0, %r1, %r2, %r3;",
                            "mad.lo.sat.s32 %r0, %r1, %r2, %r3;",
                            "mad.rz.f32 %f0, %f1, %f2, %f3;",
                            "mad.lo.cc.u32 %r0, %r1, %r2, %r3;"}) {
    const auto selected = selectVariant<Mad>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveFma, SelectsFrozenRnF32Variant) {
  const auto resolved = resolve<Fma>(parse_instruction("fma.rn.f32 %f0, %f1, %f2, %f3;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Fma::RnF32>(&resolved->variant), nullptr);
  EXPECT_EQ(Fma::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Fma::RnF32::type, ScalarType::F32);
}

TEST(ResolveFma, SelectsM12RnF64AndF16Variants) {
  const auto f64 =
      resolve<Fma>(parse_instruction("fma.rn.f64 %d0, %d1, %d2, %d3;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  ASSERT_NE(std::get_if<Fma::RnF64>(&f64->variant), nullptr);
  EXPECT_EQ(Fma::RnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(Fma::RnF64::type, ScalarType::F64);

  const auto f16 =
      resolve<Fma>(parse_instruction("fma.rn.f16 %h0, %h1, %h2, %h3;"));
  ASSERT_TRUE(f16.has_value()) << f16.error().message;
  ASSERT_NE(std::get_if<Fma::RnF16>(&f16->variant), nullptr);
  EXPECT_EQ(Fma::RnF16::rounding, RoundingMode::Rn);
  EXPECT_EQ(Fma::RnF16::type, ScalarType::F16);
}

TEST(ResolveFma, RejectsUnfrozenVariants) {
  for (const auto source : {"fma.f32 %f0, %f1, %f2, %f3;",
                            "fma.rz.f32 %f0, %f1, %f2, %f3;",
                            "fma.rz.f64 %d0, %d1, %d2, %d3;",
                            "fma.rz.f16 %h0, %h1, %h2, %h3;",
                            "fma.rn.ftz.f32 %f0, %f1, %f2, %f3;",
                            "fma.rn.sat.f32 %f0, %f1, %f2, %f3;"}) {
    const auto selected = selectVariant<Fma>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveFma, RejectsImmediateOperands) {
  for (const auto source : {"fma.rn.f32 %f0, 1.0, %f2, %f3;",
                            "fma.rn.f64 %d0, 1.0, %d2, %d3;",
                            "fma.rn.f16 %h0, 1.0, %h2, %h3;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Fma>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveDiv, SelectsFrozenU32VariantAndAcceptsZeroImmediate) {
  const auto resolved = resolve<Div>(parse_instruction("div.u32 %r0, %r1, 0;"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* div = std::get_if<Div::U32>(&resolved->variant);
  ASSERT_NE(div, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(div->src2.value));
}

TEST(ResolveDiv, SelectsM12S32AndRnFloatingVariants) {
  const auto s32 = resolve<Div>(parse_instruction("div.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Div::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Div::S32::type, ScalarType::S32);

  const auto f32 =
      resolve<Div>(parse_instruction("div.rn.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Div::RnF32>(&f32->variant), nullptr);
  EXPECT_EQ(Div::RnF32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Div::RnF32::type, ScalarType::F32);

  const auto f64 =
      resolve<Div>(parse_instruction("div.rn.f64 %d0, %d1, %d2;"));
  ASSERT_TRUE(f64.has_value()) << f64.error().message;
  ASSERT_NE(std::get_if<Div::RnF64>(&f64->variant), nullptr);
  EXPECT_EQ(Div::RnF64::rounding, RoundingMode::Rn);
  EXPECT_EQ(Div::RnF64::type, ScalarType::F64);
}

TEST(ResolveDiv, RejectsUnfrozenVariants) {
  for (const auto source : {"div.rz.f32 %f0, %f1, %f2;",
                            "div.approx.f32 %f0, %f1, %f2;",
                            "div.full.f64 %d0, %d1, %d2;",
                            "div.rn.f16 %h0, %h1, %h2;",
                            "div.sat.u32 %r0, %r1, %r2;"}) {
    const auto selected = selectVariant<Div>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveRem, SelectsFrozenVariantsAndAcceptsZeroDivisor) {
  const auto s32 = resolve<Rem>(parse_instruction("rem.s32 %r0, %r1, 0;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  const auto* signed_rem = std::get_if<Rem::S32>(&s32->variant);
  ASSERT_NE(signed_rem, nullptr);
  EXPECT_EQ(Rem::S32::type, ScalarType::S32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(signed_rem->src2.value));

  const auto u32 = resolve<Rem>(parse_instruction("rem.u32 %r0, %r1, %r2;"));
  ASSERT_TRUE(u32.has_value()) << u32.error().message;
  ASSERT_NE(std::get_if<Rem::U32>(&u32->variant), nullptr);
  EXPECT_EQ(Rem::U32::type, ScalarType::U32);
}

TEST(ResolveMin, SelectsFrozenSignedAndNaNVariants) {
  const auto s32 = resolve<Min>(parse_instruction("min.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Min::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Min::S32::type, ScalarType::S32);

  const auto nan =
      resolve<Min>(parse_instruction("min.NaN.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(nan.has_value()) << nan.error().message;
  ASSERT_NE(std::get_if<Min::NanF32>(&nan->variant), nullptr);
  EXPECT_TRUE(Min::NanF32::nan);
  EXPECT_EQ(Min::NanF32::type, ScalarType::F32);
}

TEST(ResolveMin, RejectsUnfrozenVariants) {
  for (const auto source : {"min.relu.s32 %r0, %r1, %r2;",
                            "min.f32 %f0, %f1, %f2;",
                            "min.ftz.f32 %f0, %f1, %f2;",
                            "min.xorsign.abs.f32 %f0, %f1, %f2;",
                            "min.abs.f32 %f0, %f1, %f2;",
                            "min.nan.f32 %f0, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Min>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Min>(parse_instruction("min.NaN.f32 %f0, %f1, %f2, %f3;")).has_value());
}

TEST(ResolveMax, SelectsFrozenSignedAndNaNVariants) {
  const auto s32 = resolve<Max>(parse_instruction("max.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Max::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Max::S32::type, ScalarType::S32);

  const auto nan =
      resolve<Max>(parse_instruction("max.NaN.f32 %f0, %f1, %f2;"));
  ASSERT_TRUE(nan.has_value()) << nan.error().message;
  ASSERT_NE(std::get_if<Max::NanF32>(&nan->variant), nullptr);
  EXPECT_TRUE(Max::NanF32::nan);
  EXPECT_EQ(Max::NanF32::type, ScalarType::F32);
}

TEST(ResolveMax, RejectsUnfrozenVariants) {
  for (const auto source : {"max.relu.s32 %r0, %r1, %r2;",
                            "max.f32 %f0, %f1, %f2;",
                            "max.ftz.f32 %f0, %f1, %f2;",
                            "max.xorsign.abs.f32 %f0, %f1, %f2;",
                            "max.abs.f32 %f0, %f1, %f2;",
                            "max.nan.f32 %f0, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Max>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Max>(parse_instruction("max.NaN.f32 %f0, %f1, %f2, %f3;")).has_value());
}

TEST(ResolveAbs, SelectsFrozenSignedAndFloatVariants) {
  const auto s32 = resolve<Abs>(parse_instruction("abs.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Abs::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Abs::S32::type, ScalarType::S32);

  const auto f32 = resolve<Abs>(parse_instruction("abs.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Abs::F32>(&f32->variant), nullptr);
  EXPECT_EQ(Abs::F32::type, ScalarType::F32);
}

TEST(ResolveAbs, RejectsUnfrozenAndInvalidForms) {
  for (const auto source : {"abs.sat.s32 %r0, %r1;",
                            "abs.ftz.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Abs>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Abs>(parse_instruction("abs.s32 %r0;")).has_value());
  EXPECT_FALSE(resolve<Abs>(parse_instruction("abs.f32 %f0, %f1, %f2;")).has_value());
}

TEST(ResolveNeg, SelectsFrozenScalarAndPackedVariants) {
  const auto s32 = resolve<Neg>(parse_instruction("neg.s32 %r0, %r1;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  ASSERT_NE(std::get_if<Neg::S32>(&s32->variant), nullptr);
  EXPECT_EQ(Neg::S32::type, ScalarType::S32);

  const auto f32 = resolve<Neg>(parse_instruction("neg.f32 %f0, %f1;"));
  ASSERT_TRUE(f32.has_value()) << f32.error().message;
  ASSERT_NE(std::get_if<Neg::F32>(&f32->variant), nullptr);
  EXPECT_EQ(Neg::F32::type, ScalarType::F32);

  const auto f16x2 = resolve<Neg>(parse_instruction("neg.f16x2 %r0, %r1;"));
  ASSERT_TRUE(f16x2.has_value()) << f16x2.error().message;
  ASSERT_NE(std::get_if<Neg::F16x2>(&f16x2->variant), nullptr);
  EXPECT_EQ(Neg::F16x2::type, ScalarType::F16x2);
}

TEST(ResolveNeg, RejectsUnfrozenForms) {
  for (const auto source : {"neg.ftz.f32 %f0, %f1;",
                            "neg.bf16x2 %r0, %r1;",
                            "neg.sat.s32 %r0, %r1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Neg>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveLop3, SelectsFrozenB32LutVariant) {
  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 0x1a;",
                            "lop3.b32 %r0, %r1, %r2, %r3, 0;",
                            "lop3.b32 %r0, %r1, %r2, %r3, 255;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Lop3>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_NE(std::get_if<Lop3::B32>(&resolved->variant), nullptr);
    EXPECT_EQ(Lop3::B32::type, ScalarType::B32);
  }
}

TEST(ResolveLop3, RejectsUnfrozenPredicateExtensionAndNonImmediateLut) {
  EXPECT_FALSE(selectVariant<Lop3>(parse_instruction(
      "lop3.and.b32 %r0, %r1, %r2, %r3, 0x1a, %p0;")).has_value());
  EXPECT_FALSE(resolve<Lop3>(parse_instruction(
      "lop3.b32 %r0, %r1, %r2, %r3, %r4;")).has_value());
}

TEST(ResolveBfe, SelectsFrozenU32VariantAndRejectsNonImmediateBounds) {
  for (const auto source : {"bfe.u32 %r0, %r1, 0, 8;",
                            "bfe.u32 %r0, %r1, 255, 255;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Bfe>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_NE(std::get_if<Bfe::U32>(&resolved->variant), nullptr);
    EXPECT_EQ(Bfe::U32::type, ScalarType::U32);
  }
  for (const auto source : {"bfe.u32 %r0, %r1, %r2, 8;",
                            "bfe.u32 %r0, %r1, 8, %r2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Bfe>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveBfi, SelectsFrozenB32VariantAndRejectsNonImmediateBounds) {
  for (const auto source : {"bfi.b32 %r0, %r1, %r2, 0, 8;",
                            "bfi.b32 %r0, %r1, %r2, 255, 255;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Bfi>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    ASSERT_NE(std::get_if<Bfi::B32>(&resolved->variant), nullptr);
    EXPECT_EQ(Bfi::B32::type, ScalarType::B32);
  }
  for (const auto source : {"bfi.b32 %r0, %r1, %r2, %r3, 8;",
                            "bfi.b32 %r0, %r1, %r2, 8, %r3;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Bfi>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveBrev, SelectsFrozenB32VariantAndRejectsB64) {
  const auto brev = resolve<Brev>(parse_instruction("brev.b32 %r0, %r1;"));
  ASSERT_TRUE(brev.has_value()) << brev.error().message;
  ASSERT_NE(std::get_if<Brev::B32>(&brev->variant), nullptr);
  EXPECT_EQ(Brev::B32::type, ScalarType::B32);
  EXPECT_FALSE(selectVariant<Brev>(parse_instruction("brev.b64 %rd0, %rd1;")).has_value());
}

TEST(ResolveShf, SelectsFrozenDirectionAndModeVariants) {
  const auto left = resolve<Shf>(parse_instruction("shf.l.clamp.b32 %r0, %r1, %r2, 8;"));
  ASSERT_TRUE(left.has_value()) << left.error().message;
  ASSERT_NE(std::get_if<Shf::LClampB32>(&left->variant), nullptr);
  const auto right = resolve<Shf>(parse_instruction("shf.r.wrap.b32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(right.has_value()) << right.error().message;
  ASSERT_NE(std::get_if<Shf::RWrapB32>(&right->variant), nullptr);
}

TEST(ResolveShf, RejectsUnfrozenDirectionAndModeVariants) {
  for (const auto source : {"shf.l.wrap.b32 %r0, %r1, %r2, 8;",
                            "shf.r.clamp.b32 %r0, %r1, %r2, 8;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Shf>(parse_instruction(source)).has_value());
  }
}

TEST(ResolvePrmt, SelectsFrozenGenericAndF4eVariants) {
  for (const auto source : {"prmt.b32 %r0, %r1, %r2, 0x5410;", "prmt.b32 %r0, %r1, %r2, 0;", "prmt.b32 %r0, %r1, %r2, 65535;"})
    EXPECT_TRUE(resolve<Prmt>(parse_instruction(source)).has_value()) << source;
  EXPECT_TRUE(resolve<Prmt>(parse_instruction("prmt.b32.f4e %r0, %r1, %r2, %r3;")).has_value());
}

TEST(ResolvePrmt, RejectsWrongSelectorFormsAndModes) {
  EXPECT_FALSE(resolve<Prmt>(parse_instruction("prmt.b32 %r0, %r1, %r2, %r3;")).has_value());
  EXPECT_FALSE(resolve<Prmt>(parse_instruction("prmt.b32.f4e %r0, %r1, %r2, 0;")).has_value());
  EXPECT_FALSE(selectVariant<Prmt>(parse_instruction("prmt.b32.b4e %r0, %r1, %r2, %r3;")).has_value());
}

TEST(ResolvePopc, SelectsFrozenB32VariantAndRejectsB64) {
  const auto popc = resolve<Popc>(parse_instruction("popc.b32 %r0, %r1;"));
  ASSERT_TRUE(popc.has_value()) << popc.error().message;
  ASSERT_NE(std::get_if<Popc::B32>(&popc->variant), nullptr);
  EXPECT_EQ(Popc::B32::type, ScalarType::B32);
  EXPECT_FALSE(selectVariant<Popc>(parse_instruction("popc.b64 %rd0, %rd1;")).has_value());
}

TEST(ResolveClz, SelectsFrozenBitWidthVariantsAndRejectsUnfrozenType) {
  const auto b32 = resolve<Clz>(parse_instruction("clz.b32 %r0, %r1;"));
  ASSERT_TRUE(b32.has_value()) << b32.error().message;
  EXPECT_NE(std::get_if<Clz::B32>(&b32->variant), nullptr);
  const auto b64 = resolve<Clz>(parse_instruction("clz.b64 %r0, %rd1;"));
  ASSERT_TRUE(b64.has_value()) << b64.error().message;
  EXPECT_NE(std::get_if<Clz::B64>(&b64->variant), nullptr);
  EXPECT_FALSE(selectVariant<Clz>(parse_instruction("clz.u32 %r0, %r1;")).has_value());
}

TEST(ResolveBfind, SelectsFrozenShiftamtU32AndRejectsPlainForm) {
  const auto bfind = resolve<Bfind>(parse_instruction("bfind.shiftamt.u32 %r0, %r1;"));
  ASSERT_TRUE(bfind.has_value()) << bfind.error().message;
  ASSERT_NE(std::get_if<Bfind::ShiftamtU32>(&bfind->variant), nullptr);
  EXPECT_TRUE(Bfind::ShiftamtU32::shiftamt);
  const auto plain = parse_instruction("bfind.u32 %r0, %r1;");
  EXPECT_FALSE(selectVariant<Bfind>(plain).has_value());
}

TEST(ResolveIsspacep, SelectsFrozenGlobalU64AndRejectsOtherForms) {
  const auto ast = parse_instruction("isspacep.global %p0, %rd0;");
  const auto resolved = resolve<Isspacep>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* global = std::get_if<Isspacep::GlobalU64>(&resolved->variant);
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(Isspacep::GlobalU64::state_space, MemoryStateSpace::Global);
  EXPECT_EQ(global->src.value.register_class, ResolvedRegisterClass::General);

  for (const auto source : {"isspacep %p0, %rd0;",
                            "isspacep.shared %p0, %rd0;",
                            "isspacep.global %r0, %rd0;",
                            "isspacep.global %p0, [%rd0];"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Isspacep>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveCvt, SelectsFrozenS32U32Variant) {
  const auto ast = parse_instruction("cvt.s32.u32 %s0, %r0;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* cvt = std::get_if<Cvt::S32U32>(&resolved->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::S32U32::dst_type, ScalarType::S32);
  EXPECT_EQ(Cvt::S32U32::src_type, ScalarType::U32);
}

TEST(ResolveCvt, SelectsFrozenRnF32F64Variant) {
  const auto ast = parse_instruction("cvt.rn.f32.f64 %f0, %fd0;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* cvt = std::get_if<Cvt::RnF32F64>(&resolved->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::RnF32F64::rounding, RoundingMode::Rn);
  EXPECT_EQ(Cvt::RnF32F64::dst_type, ScalarType::F32);
  EXPECT_EQ(Cvt::RnF32F64::src_type, ScalarType::F64);
}

TEST(ResolveCvt, SelectsFrozenMixedVariants) {
  const auto to_float = resolve<Cvt>(parse_instruction("cvt.rn.f32.u32 %f0, %r0;"));
  ASSERT_TRUE(to_float.has_value()) << to_float.error().message;
  EXPECT_NE(std::get_if<Cvt::RnF32U32>(&to_float->variant), nullptr);

  const auto to_integer =
      resolve<Cvt>(parse_instruction("cvt.rzi.u32.f32 %r0, %f0;"));
  ASSERT_TRUE(to_integer.has_value()) << to_integer.error().message;
  const auto* cvt = std::get_if<Cvt::RziU32F32>(&to_integer->variant);
  ASSERT_NE(cvt, nullptr);
  EXPECT_EQ(Cvt::RziU32F32::rounding, RoundingMode::Rzi);
  EXPECT_EQ(Cvt::RziU32F32::dst_type, ScalarType::U32);
  EXPECT_EQ(Cvt::RziU32F32::src_type, ScalarType::F32);
}

TEST(ResolveCvt, SelectsM12RnS32AndPackedF16x2Variants) {
  const auto scalar = resolve<Cvt>(parse_instruction("cvt.rn.f32.s32 %f0, %r0;"));
  ASSERT_TRUE(scalar.has_value()) << scalar.error().message;
  ASSERT_NE(std::get_if<Cvt::RnF32S32>(&scalar->variant), nullptr);

  const auto packed =
      resolve<Cvt>(parse_instruction("cvt.rn.f16x2.f32 %r0, %f0, %f1;"));
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  ASSERT_NE(std::get_if<Cvt::RnF16x2F32>(&packed->variant), nullptr);
  EXPECT_EQ(Cvt::RnF16x2F32::rounding, RoundingMode::Rn);
  EXPECT_EQ(Cvt::RnF16x2F32::dst_type, ScalarType::F16x2);
  EXPECT_EQ(Cvt::RnF16x2F32::src_type, ScalarType::F32);
}

TEST(ResolveCvt, RejectsM12UnfrozenAndPackedTwoOperandForms) {
  for (const auto source : {"cvt.rz.f32.s32 %f0, %r0;",
                            "cvt.rn.f32.s16 %f0, %r0;",
                            "cvt.rz.f16x2.f32 %r0, %f0, %f1;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Cvt>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Cvt>(parse_instruction("cvt.rn.f16x2.f32 %r0, %f0;")).has_value());
}

TEST(ResolveCvt, SelectsM12PackedSatVariantAndRejectsUnfrozenForms) {
  const auto ast =
      parse_instruction("cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2, %r3;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Cvt::PackSatU8S32B32>(&resolved->variant), nullptr);
  EXPECT_TRUE(Cvt::PackSatU8S32B32::pack);
  EXPECT_TRUE(Cvt::PackSatU8S32B32::saturate);
  EXPECT_EQ(Cvt::PackSatU8S32B32::dst_type, ScalarType::U8);
  EXPECT_EQ(Cvt::PackSatU8S32B32::src_type, ScalarType::S32);
  EXPECT_EQ(Cvt::PackSatU8S32B32::carry_type, ScalarType::B32);

  const auto dispatched = resolveInstruction(ast);
  ASSERT_TRUE(dispatched.has_value()) << dispatched.error().message;
  EXPECT_TRUE(std::holds_alternative<Cvt>(*dispatched));

  for (const auto source : {"cvt.sat.u8.s32.b32 %r0, %r1, %r2, %r3;",
                            "cvt.pack.u8.s32.b32 %r0, %r1, %r2, %r3;",
                            "cvt.pack.sat.u16.s32.b32 %r0, %r1, %r2, %r3;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Cvt>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(resolve<Cvt>(
                   parse_instruction("cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2;"))
                   .has_value());
}

TEST(ResolveLd, SelectsM12GlobalNcL1NoAllocateAndRejectsUnfrozenForms) {
  const auto ast =
      parse_instruction("ld.global.nc.L1::no_allocate.u32 %r0, [%rd0];");
  const auto resolved = resolve<Ld>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Ld::GlobalNcL1NoAllocateU32>(&resolved->variant),
            nullptr);
  EXPECT_EQ(Ld::GlobalNcL1NoAllocateU32::state_space,
            MemoryStateSpace::Global);
  EXPECT_TRUE(Ld::GlobalNcL1NoAllocateU32::nc);
  EXPECT_EQ(Ld::GlobalNcL1NoAllocateU32::eviction_priority,
            EvictionPriority::NoAllocate);
  EXPECT_EQ(Ld::GlobalNcL1NoAllocateU32::type, ScalarType::U32);

  const auto dispatched = resolveInstruction(ast);
  ASSERT_TRUE(dispatched.has_value()) << dispatched.error().message;
  EXPECT_TRUE(std::holds_alternative<Ld>(*dispatched));

  for (const auto source : {
           "ld.global.L1::no_allocate.u32 %r0, [%rd0];",
           "ld.global.nc.L2::evict_first.u32 %r0, [%rd0];",
           "ld.global.nc.L1::evict_first.u32 %r0, [%rd0];",
           "ld.global.nc.L1::no_allocate.b32 %r0, [%rd0];",
           "ld.global.ca.nc.L1::no_allocate.u32 %r0, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Ld>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveCvt, RejectsUnfrozenFloatVariants) {
  for (const auto source : {"cvt.f32.f64 %f0, %fd0;",
                            "cvt.rz.f32.f64 %f0, %fd0;",
                            "cvt.rn.f64.f32 %fd0, %f0;"}) {
    const auto selected = selectVariant<Cvt>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveCvt, RejectsUnfrozenMixedVariants) {
  for (const auto source : {"cvt.rz.f32.u32 %f0, %r0;",
                            "cvt.rn.u32.f32 %r0, %f0;",
                            "cvt.rzi.f32.u32 %f0, %r0;"}) {
    const auto selected = selectVariant<Cvt>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

TEST(ResolveAdd, RejectsMismatchedOpcode) {
  const auto ast = parse_instruction("sub.u32 %r0, %r1, %r2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.opcode.syntax.range);
  EXPECT_EQ(resolved.error().message, "Cannot resolve opcode 'sub' as 'add'.");
}

TEST(ResolveInstruction, DispatchesByOpcodeIntoGeneratedVariant) {
  const ResolvedModule empty_module{};
  EXPECT_TRUE(empty_module.functions.empty());

  const auto add_ast = parse_instruction("add.u32 %r0, %r1, %r2;");
  const auto add = resolveInstruction(add_ast);
  ASSERT_TRUE(add.has_value()) << add.error().message;
  EXPECT_TRUE(std::holds_alternative<Add>(*add));

  const auto sub_ast = parse_instruction("sub.u32 %r0, %r1, %r2;");
  const auto sub = resolveInstruction(sub_ast);
  ASSERT_TRUE(sub.has_value()) << sub.error().message;
  EXPECT_TRUE(std::holds_alternative<Sub>(*sub));

  const auto ret_ast = parse_instruction("ret;");
  const auto ret = resolveInstruction(ret_ast);
  ASSERT_TRUE(ret.has_value()) << ret.error().message;
  EXPECT_TRUE(std::holds_alternative<Ret>(*ret));

  const auto exit_ast = parse_instruction("exit;");
  const auto exit_instruction = resolveInstruction(exit_ast);
  ASSERT_TRUE(exit_instruction.has_value()) << exit_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Exit>(*exit_instruction));

  const auto trap_ast = parse_instruction("trap;");
  const auto trap = resolveInstruction(trap_ast);
  ASSERT_TRUE(trap.has_value()) << trap.error().message;
  EXPECT_TRUE(std::holds_alternative<Trap>(*trap));

  const auto and_ast = parse_instruction("and.b32 %r0, %r1, %r2;");
  const auto and_instruction = resolveInstruction(and_ast);
  ASSERT_TRUE(and_instruction.has_value()) << and_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<And>(*and_instruction));

  const auto or_ast = parse_instruction("or.b32 %r0, %r1, %r2;");
  const auto or_instruction = resolveInstruction(or_ast);
  ASSERT_TRUE(or_instruction.has_value()) << or_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Or>(*or_instruction));

  const auto xor_ast = parse_instruction("xor.b32 %r0, %r1, %r2;");
  const auto xor_instruction = resolveInstruction(xor_ast);
  ASSERT_TRUE(xor_instruction.has_value()) << xor_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Xor>(*xor_instruction));

  const auto not_ast = parse_instruction("not.b32 %r0, %r1;");
  const auto not_instruction = resolveInstruction(not_ast);
  ASSERT_TRUE(not_instruction.has_value()) << not_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Not>(*not_instruction));

  const auto shl_ast = parse_instruction("shl.b32 %r0, %r1, %r2;");
  const auto shl_instruction = resolveInstruction(shl_ast);
  ASSERT_TRUE(shl_instruction.has_value()) << shl_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Shl>(*shl_instruction));

  const auto shr_ast = parse_instruction("shr.u32 %r0, %r1, %r2;");
  const auto shr_instruction = resolveInstruction(shr_ast);
  ASSERT_TRUE(shr_instruction.has_value()) << shr_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Shr>(*shr_instruction));

  const auto setp_ast = parse_instruction("setp.lt.u32 %p0, %r0, %r1;");
  const auto setp_instruction = resolveInstruction(setp_ast);
  ASSERT_TRUE(setp_instruction.has_value()) << setp_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Setp>(*setp_instruction));

  const auto selp_ast = parse_instruction("selp.u32 %r0, %r1, %r2, %p0;");
  const auto selp_instruction = resolveInstruction(selp_ast);
  ASSERT_TRUE(selp_instruction.has_value()) << selp_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Selp>(*selp_instruction));

  const auto cvt_ast = parse_instruction("cvt.s32.u32 %s0, %r0;");
  const auto cvt_instruction = resolveInstruction(cvt_ast);
  ASSERT_TRUE(cvt_instruction.has_value()) << cvt_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Cvt>(*cvt_instruction));
}

TEST(ResolveInstruction, RejectsUnknownOpcode) {
  const auto ast = parse_instruction("unknown.u32 %r0, %r1, %r2;");

  const auto resolved = resolveInstruction(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.opcode.syntax.range);
  EXPECT_EQ(resolved.error().message, "Unknown PTX opcode 'unknown'.");
}

TEST(ResolveInstruction, RejectsMalformedMetadataCallWithGenericLayoutError) {
  const auto resolved = resolveInstruction(indirect_metadata_instruction("metadata"));

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant "
            "'Direct'.");
}

TEST(ResolveLoadStore, PreservesCacheValuesAndOmittedSentinel) {
  const auto cached_load_ast = parse_instruction("ld.cg.u32 %r0, [%rd0];");
  const auto cached_load = resolve<Ld>(cached_load_ast);
  ASSERT_TRUE(cached_load.has_value()) << cached_load.error().message;
  const auto* load_variant =
      std::get_if<Ld::GenericScalar>(&cached_load->variant);
  ASSERT_NE(load_variant, nullptr);
  EXPECT_EQ(load_variant->cache.value, CacheOperator::Cg);
  ASSERT_EQ(load_variant->cache.locs.size(), 1u);
  EXPECT_EQ(load_variant->cache.locs.front(),
            cached_load_ast.modifiers.front().syntax.range);

  const auto omitted_load_ast = parse_instruction("ld.u32 %r0, [%rd0];");
  const auto omitted_load = resolve<Ld>(omitted_load_ast);
  ASSERT_TRUE(omitted_load.has_value()) << omitted_load.error().message;
  const auto* omitted_load_variant =
      std::get_if<Ld::GenericScalar>(&omitted_load->variant);
  ASSERT_NE(omitted_load_variant, nullptr);
  EXPECT_EQ(omitted_load_variant->cache.value, CacheOperator::Unspecified);
  EXPECT_TRUE(omitted_load_variant->cache.locs.empty());

  const auto cached_store_ast =
      parse_instruction("st.global.wb.u32 [%rd0], %r0;");
  const auto cached_store = resolve<St>(cached_store_ast);
  ASSERT_TRUE(cached_store.has_value()) << cached_store.error().message;
  const auto* store_variant =
      std::get_if<St::ExplicitScalar>(&cached_store->variant);
  ASSERT_NE(store_variant, nullptr);
  EXPECT_EQ(store_variant->cache.value, CacheOperator::Wb);
  ASSERT_EQ(store_variant->cache.locs.size(), 1u);
  EXPECT_EQ(store_variant->cache.locs.front(),
            cached_store_ast.modifiers[1].syntax.range);

  const auto omitted_store_ast =
      parse_instruction("st.global.u32 [%rd0], %r0;");
  const auto omitted_store = resolve<St>(omitted_store_ast);
  ASSERT_TRUE(omitted_store.has_value()) << omitted_store.error().message;
  const auto* omitted_store_variant =
      std::get_if<St::ExplicitScalar>(&omitted_store->variant);
  ASSERT_NE(omitted_store_variant, nullptr);
  EXPECT_EQ(omitted_store_variant->cache.value, CacheOperator::Unspecified);
  EXPECT_TRUE(omitted_store_variant->cache.locs.empty());
}

TEST(ResolveLoadStore, PreservesMemoryConsistencyDefaultsAndExplicitWeak) {
  const auto omitted_ast = parse_instruction("ld.u32 %r0, [%rd0];");
  const auto omitted = resolve<Ld>(omitted_ast);
  ASSERT_TRUE(omitted.has_value()) << omitted.error().message;
  const auto* omitted_variant = std::get_if<Ld::GenericScalar>(&omitted->variant);
  ASSERT_NE(omitted_variant, nullptr);
  EXPECT_EQ(omitted_variant->semantics.value, MemoryConsistency::Omitted);
  EXPECT_TRUE(omitted_variant->semantics.locs.empty());
  EXPECT_EQ(omitted_variant->scope.value, MemoryScope::None);
  EXPECT_TRUE(omitted_variant->scope.locs.empty());

  const auto weak_ast = parse_instruction("ld.weak.u32 %r0, [%rd0];");
  const auto weak = resolve<Ld>(weak_ast);
  ASSERT_TRUE(weak.has_value()) << weak.error().message;
  const auto* weak_variant = std::get_if<Ld::GenericScalar>(&weak->variant);
  ASSERT_NE(weak_variant, nullptr);
  EXPECT_EQ(weak_variant->semantics.value, MemoryConsistency::Weak);
  ASSERT_EQ(weak_variant->semantics.locs.size(), 1U);
  EXPECT_EQ(weak_variant->semantics.locs.front(),
            weak_ast.modifiers.front().syntax.range);

  const auto acquire = selectVariant<Ld>(
      parse_instruction("ld.acquire.gpu.u32 %r0, [%rd0];"));
  ASSERT_TRUE(acquire.has_value()) << acquire.error().message;
  EXPECT_EQ(*acquire, Ld::VariantType::GenericScalar);
  const auto release = selectVariant<St>(
      parse_instruction("st.release.sys.u32 [%rd0], %r0;"));
  ASSERT_TRUE(release.has_value()) << release.error().message;
  EXPECT_EQ(*release, St::VariantType::GenericScalar);
}

TEST(ResolveLoadStore, ChecksMemoryConsistencyCrossRules) {
  const checker::Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 90},
      .instruction_range = SourceRange{},
  };
  const auto missing_scope = resolve<Ld>(
      parse_instruction("ld.relaxed.u32 %r0, [%rd0];"));
  ASSERT_TRUE(missing_scope.has_value()) << missing_scope.error().message;
  const auto missing_scope_check = checker::check(*missing_scope, context);
  ASSERT_FALSE(missing_scope_check.has_value());
  EXPECT_EQ(missing_scope_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto conflicting_cache = resolve<Ld>(
      parse_instruction("ld.volatile.ca.u32 %r0, [%rd0];"));
  ASSERT_TRUE(conflicting_cache.has_value()) << conflicting_cache.error().message;
  const auto cache_check = checker::check(*conflicting_cache, context);
  ASSERT_FALSE(cache_check.has_value());
  EXPECT_EQ(cache_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto relaxed_local = resolve<Ld>(
      parse_instruction("ld.local.relaxed.cta.u32 %r0, [%rd0];"));
  ASSERT_TRUE(relaxed_local.has_value()) << relaxed_local.error().message;
  const auto relaxed_local_check = checker::check(*relaxed_local, context);
  ASSERT_FALSE(relaxed_local_check.has_value());
  EXPECT_EQ(relaxed_local_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto unknown_generic = resolve<Ld>(
      parse_instruction("ld.acquire.gpu.u32 %r0, [%rd0];"));
  ASSERT_TRUE(unknown_generic.has_value()) << unknown_generic.error().message;
  EXPECT_TRUE(checker::check(*unknown_generic, context).has_value());
}

TEST(CollectActualModifiersAdd, BindsSpellingsToSelectedVariantSlots) {
  const auto ast = parse_instruction("add.rz.f32.bf16.sat %f0, %h1, %f2;");
  const auto& instruction = Add::get_syntax_descriptor();
  const auto mixed = std::ranges::find_if(
      instruction.variants,
      [](auto variant) { return variant.variant_name == "MixedF32"; });
  ASSERT_NE(mixed, instruction.variants.end());

  const auto actual = collect_actual_modifiers(ast, *mixed);

  ASSERT_TRUE(actual.has_value()) << actual.error().message;
  ASSERT_EQ(actual->size(), 4U);
  EXPECT_EQ(actual->at("rounding"), &ast.modifiers[0]);
  EXPECT_EQ(actual->at("result_type"), &ast.modifiers[1]);
  EXPECT_EQ(actual->at("input_type"), &ast.modifiers[2]);
  EXPECT_EQ(actual->at("sat"), &ast.modifiers[3]);
}

TEST(CollectActualModifiersAdd, RejectsOutOfOrderMixedSlots) {
  const auto ast = parse_instruction("add.rz.f32.sat.bf16 %f0, %h1, %f2;");
  const auto& instruction = Add::get_syntax_descriptor();
  const auto mixed = std::ranges::find_if(
      instruction.variants,
      [](auto variant) { return variant.variant_name == "MixedF32"; });
  ASSERT_NE(mixed, instruction.variants.end());

  const auto actual = collect_actual_modifiers(ast, *mixed);

  ASSERT_FALSE(actual.has_value());
  EXPECT_EQ(actual.error().message,
            "Modifier combination does not match instruction variant 'MixedF32'.");
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
      {.variant_name = "Repeated", .modifiers = modifiers, .operand_layouts = layouts},
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

TEST(ResolvedDescriptorAdd, OwnsResolvedFieldBindings) {
  const auto& descriptor = Add::get_resolved_descriptor();

  ASSERT_EQ(descriptor.opcode_name, "add");
  ASSERT_EQ(descriptor.variants.size(), 9U);

  const auto packed_optional_sat_it =
      std::ranges::find_if(descriptor.variants, [](const auto& variant) {
        return variant.variant_name == "PackedOptionalSat";
      });
  ASSERT_NE(packed_optional_sat_it, descriptor.variants.end());
  const auto& packed_optional_sat = *packed_optional_sat_it;
  EXPECT_EQ(packed_optional_sat.variant_name, "PackedOptionalSat");
  ASSERT_EQ(packed_optional_sat.fields.size(), 2U);
  EXPECT_EQ(packed_optional_sat.fields[0].field_id, "saturate");
  EXPECT_EQ(packed_optional_sat.fields[0].value_kind,
            check_end::ResolvedValueKind::Bool);

  ASSERT_EQ(packed_optional_sat.modifier_bindings.size(), 2U);
  EXPECT_EQ(packed_optional_sat.modifier_bindings[0].source_kind_id, "sat");
  EXPECT_EQ(packed_optional_sat.modifier_bindings[0].target_field_id,
            "saturate");

  ASSERT_EQ(packed_optional_sat.operand_layouts.size(), 1U);
  const auto& layout = packed_optional_sat.operand_layouts[0];
  ASSERT_EQ(layout.fields.size(), 3U);
  EXPECT_EQ(layout.fields[0].field_id, "dst");
  const auto& bindings = layout.bindings;
  ASSERT_EQ(bindings.size(), 3U);
  EXPECT_EQ(bindings[2].target_field_id, "src2");
  EXPECT_EQ(bindings[2].type_expression.kind,
            check_end::OperandTypeExpressionKind::ModifierField);
  EXPECT_EQ(bindings[2].type_expression.modifier_field_id, "type");
  EXPECT_EQ(bindings[0].role, check_end::OperandRole::Destination);
  EXPECT_EQ(bindings[0].access, check_end::OperandAccess::Write);
  EXPECT_EQ(bindings[0].allowed_shapes, check_end::OperandShape::Register);
  EXPECT_EQ(bindings[1].role, check_end::OperandRole::Source);
  EXPECT_EQ(bindings[1].access, check_end::OperandAccess::Read);
  EXPECT_EQ(bindings[1].allowed_shapes, check_end::OperandShape::Register |
                                            check_end::OperandShape::Immediate);

  const auto sat_it = std::ranges::find_if(
      descriptor.variants,
      [](const auto& variant) { return variant.variant_name == "Sat"; });
  ASSERT_NE(sat_it, descriptor.variants.end());
  const auto& sat = *sat_it;
  ASSERT_EQ(sat.modifier_bindings.size(), 2U);
  EXPECT_EQ(sat.modifier_bindings[0].source_kind_id, "sat");
  EXPECT_EQ(sat.modifier_bindings[0].target_field_id, "saturate");
  EXPECT_EQ(sat.modifier_bindings[1].source_kind_id, "type");
  EXPECT_EQ(sat.modifier_bindings[1].target_field_id, "type");
}

TEST(ResolveAdd, BuildsFloatingVariantWithTypedRoundingAndDefaults) {
  const auto default_ast = parse_instruction("add.f32 %f0, %f1, 1.5;");
  const auto default_resolved = resolve<Add>(default_ast);
  ASSERT_TRUE(default_resolved.has_value()) << default_resolved.error().message;
  const auto* default_add =
      std::get_if<Add::FloatF32>(&default_resolved->variant);
  ASSERT_NE(default_add, nullptr);
  EXPECT_EQ(default_add->rounding.value, RoundingMode::Rn);
  EXPECT_TRUE(default_add->rounding.locs.empty());
  EXPECT_FALSE(default_add->ftz.value);
  EXPECT_TRUE(default_add->ftz.locs.empty());
  EXPECT_FALSE(default_add->saturate.value);
  EXPECT_TRUE(default_add->saturate.locs.empty());
  EXPECT_EQ(Add::FloatF32::type, ScalarType::F32);
  const auto* immediate =
      std::get_if<ResolvedImmediate>(&default_add->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::F32);
  EXPECT_EQ(immediate->bits, 0x3fc00000U);

  const auto explicit_ast =
      parse_instruction("add.rz.ftz.sat.f32 %f0, %f1, %f2;");
  const auto explicit_resolved = resolve<Add>(explicit_ast);
  ASSERT_TRUE(explicit_resolved.has_value())
      << explicit_resolved.error().message;
  const auto* explicit_add =
      std::get_if<Add::FloatF32>(&explicit_resolved->variant);
  ASSERT_NE(explicit_add, nullptr);
  EXPECT_EQ(explicit_add->rounding.value, RoundingMode::Rz);
  ASSERT_EQ(explicit_add->rounding.locs.size(), 1U);
  EXPECT_EQ(explicit_add->rounding.locs.front(),
            explicit_ast.modifiers[0].syntax.range);
  EXPECT_TRUE(explicit_add->ftz.value);
  EXPECT_TRUE(explicit_add->saturate.value);
}

TEST(ResolveAdd, BuildsMixedPrecisionVariantWithTwoTypeSlots) {
  const auto ast = parse_instruction("add.rz.f32.bf16.sat %f0, %h1, %f2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::MixedF32>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->rounding.value, RoundingMode::Rz);
  EXPECT_EQ(Add::MixedF32::result_type, ScalarType::F32);
  EXPECT_EQ(add->input_type.value, ScalarType::BF16);
  EXPECT_TRUE(add->saturate.value);
  EXPECT_EQ(add->dst.value.spelling, "%f0");
  EXPECT_EQ(add->src.value.spelling, "%h1");
  EXPECT_EQ(add->addend.value.spelling, "%f2");
  EXPECT_EQ(add->input_type.locs.front(), ast.modifiers[2].syntax.range);
}

TEST(ResolveSub, BuildsIntegerAndMixedPrecisionVariants) {
  const auto integer_ast = parse_instruction("sub.sat.s32 %r4, %r5, -1;");
  const auto integer_resolved = resolve<Sub>(integer_ast);
  ASSERT_TRUE(integer_resolved.has_value()) << integer_resolved.error().message;
  const auto* integer =
      std::get_if<Sub::OptionalSat>(&integer_resolved->variant);
  ASSERT_NE(integer, nullptr);
  EXPECT_TRUE(integer->saturate.value);
  ASSERT_EQ(integer->saturate.locs.size(), 1U);
  EXPECT_EQ(integer->type.value, ScalarType::S32);
  const auto* immediate = std::get_if<ResolvedImmediate>(&integer->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::S32);

  const auto mixed_ast =
      parse_instruction("sub.rz.f32.bf16.sat %f0, %h1, %f2;");
  const auto mixed_resolved = resolve<Sub>(mixed_ast);
  ASSERT_TRUE(mixed_resolved.has_value()) << mixed_resolved.error().message;
  const auto* mixed = std::get_if<Sub::MixedF32>(&mixed_resolved->variant);
  ASSERT_NE(mixed, nullptr);
  EXPECT_EQ(mixed->rounding.value, RoundingMode::Rz);
  EXPECT_EQ(Sub::MixedF32::result_type, ScalarType::F32);
  EXPECT_EQ(mixed->input_type.value, ScalarType::BF16);
  EXPECT_TRUE(mixed->saturate.value);
  EXPECT_EQ(mixed->subtrahend.value.spelling, "%f2");
}

TEST(SelectVariantAdd, RejectsFloatingModifierOutsideItsForm) {
  const auto ast = parse_instruction("add.ftz.f64 %fd0, %fd1, %fd2;");
  const auto selected = selectVariant<Add>(ast);
  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().message,
            "No variant of instruction 'add' accepts this modifier "
            "combination.");

  const auto mixed_ast = parse_instruction("add.ftz.f32.f16 %f0, %h1, %f2;");
  const auto mixed_selected = selectVariant<Add>(mixed_ast);
  ASSERT_FALSE(mixed_selected.has_value());
  EXPECT_EQ(mixed_selected.error().message,
            "No variant of instruction 'add' accepts this modifier "
            "combination.");
}

TEST(ResolveAdd, RejectsImmediateForRegisterOnlyPackedFloatingForm) {
  const auto ast = parse_instruction("add.f16 %h0, %h1, 1.0;");
  const auto resolved = resolve<Add>(ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant 'Half'.");
}

TEST(SelectVariantAdd, ReportsUnmatchedModifierCombination) {
  const auto ast = parse_instruction("add.sat.u64 %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.range);
  EXPECT_EQ(
      selected.error().message,
      "No variant of instruction 'add' accepts this modifier combination.");
}

TEST(ResolveAdd, BuildsResolvedIntegerVariantAndPreservesLocations) {
  const auto ast = parse_instruction("add.s32 %r4, %r5, -1;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->operand_layout, (ResolvedOperandLayoutTag{0}));
  EXPECT_EQ(add->type.value, ScalarType::S32);
  ASSERT_EQ(add->type.locs.size(), 1U);
  EXPECT_EQ(add->type.locs.front(), ast.modifiers.front().syntax.range);
  EXPECT_EQ(add->dst.value.spelling, "%r4");
  EXPECT_EQ(add->dst.value.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(add->dst.value.index, 4U);
  const auto& src1 = std::get<ResolvedRegisterRef>(add->src1.value);
  EXPECT_EQ(src1.spelling, "%r5");
  EXPECT_EQ(src1.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(src1.index, 5U);

  const auto* immediate = std::get_if<ResolvedImmediate>(&add->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->bits, 0xffffffffU);
  EXPECT_EQ(immediate->type, ScalarType::S32);
  ASSERT_EQ(add->src2.locs.size(), 1U);
  EXPECT_EQ(add->src2.locs.front(),
            std::get<syntax_ast::AstImmediate>(ast.operands[2]).syntax.range);
}

TEST(ResolveAdd, UsesFixedSatAndResolvedTypeForSatVariant) {
  const auto ast = parse_instruction("add.sat.s32 %r4, %r5, -1;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::Sat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  EXPECT_TRUE(Add::Sat::saturate);
  EXPECT_EQ(add->type.value, ScalarType::S32);
  ASSERT_EQ(add->type.locs.size(), 1U);
  EXPECT_EQ(add->type.locs.front(), ast.modifiers[1].syntax.range);

  const auto* immediate = std::get_if<ResolvedImmediate>(&add->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::S32);
}

TEST(ResolveFieldsAdd, UsesResolvedFieldBindingsAndValueKinds) {
  const auto ast = parse_instruction("add.u32 %r4, %r5, 6;");

  const auto fields =
      resolve_fields(ast, Add::get_syntax_descriptor(),
                     Add::get_resolved_descriptor(), "IntegerNoSat");

  ASSERT_TRUE(fields.has_value()) << fields.error().message;
  EXPECT_EQ(fields->variant_name, "IntegerNoSat");
  EXPECT_EQ(fields->operand_layout, (ResolvedOperandLayoutTag{0}));
  const auto* type =
      std::get_if<WithLocs<ScalarType>>(&fields->modifiers.at("type"));
  ASSERT_NE(type, nullptr);
  EXPECT_EQ(type->value, ScalarType::U32);

  const auto* dst =
      std::get_if<WithLocs<ResolvedRegisterRef>>(&fields->operands.at("dst"));
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(dst->value.spelling, "%r4");
  EXPECT_EQ(dst->value.index, 4U);

  const auto* src1 =
      std::get_if<WithLocs<RegOrImm>>(&fields->operands.at("src1"));
  ASSERT_NE(src1, nullptr);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(src1->value).spelling, "%r5");
  EXPECT_EQ(std::get<ResolvedRegisterRef>(src1->value).index, 5U);

  const auto* src2 =
      std::get_if<WithLocs<RegOrImm>>(&fields->operands.at("src2"));
  ASSERT_NE(src2, nullptr);
  const auto* immediate = std::get_if<ResolvedImmediate>(&src2->value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->bits, 6U);
  EXPECT_EQ(immediate->type, ScalarType::U32);
}

TEST(ResolveAdd, RejectsOperandLayoutBeforeFieldResolution) {
  const auto ast = parse_instruction("add.u32 1, %r1, %r2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.range);
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant "
            "'IntegerNoSat'.");
}

TEST(ResolveAdd, PreservesRegisterSpellingBeyondNumericIndex) {
  const auto ast = parse_instruction("add.u64 %r1, %rd1, %r2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  const auto& dst = add->dst.value;
  const auto& src1 = std::get<ResolvedRegisterRef>(add->src1.value);
  EXPECT_EQ(dst.index, 1U);
  EXPECT_EQ(src1.index, 1U);
  EXPECT_EQ(dst.spelling, "%r1");
  EXPECT_EQ(src1.spelling, "%rd1");
  EXPECT_NE(dst, src1);
}

TEST(ResolveAdd, RejectsPredicateInGeneralRegisterSlot) {
  const auto ast = parse_instruction("add.u32 %p1, %r1, %r2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().range,
      std::get<syntax_ast::AstIdentifierRef>(ast.operands[0]).syntax.range);
  EXPECT_EQ(resolved.error().message,
            "Expected a non-predicate register, got '%p1'.");
}

TEST(ResolveImmediateLiteral, SupportsIntegerSuffixesAndTargetWidth) {
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

  const auto out_of_range = parse_immediate("65536");
  const auto rejected =
      resolve_immediate_literal(out_of_range, ScalarType::U16);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().range, out_of_range.syntax.range);
  EXPECT_EQ(rejected.error().message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");
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

TEST(ResolveCallLiteral, TypesAgainstTheFormalAndPreservesSourceRange) {
  const declaration_semantics::FunctionParameterContract u16{.type = ".u16"};
  const auto typed_immediate = parse_immediate("42");
  const auto typed = resolve_call_literal(
      ResolvedCallLiteral{.spelling = typed_immediate.syntax.text,
                          .kind = typed_immediate.kind},
      typed_immediate.syntax.range, u16);
  ASSERT_TRUE(typed.has_value()) << typed.error().message;
  EXPECT_EQ(typed->value,
            (ResolvedImmediate{.bits = 42, .type = ScalarType::U16}));
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

  const declaration_semantics::FunctionParameterContract u32{.type = ".u32"};
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
      .type = ".v2"};
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

TEST(ResolveAdd, PreservesOptionalModifierPresence) {
  const auto unsaturated_ast = parse_instruction("add.u8x4 %r0, %r1, %r2;");
  const auto unsaturated = resolve<Add>(unsaturated_ast);
  ASSERT_TRUE(unsaturated.has_value()) << unsaturated.error().message;
  const auto* unsaturated_add =
      std::get_if<Add::PackedOptionalSat>(&unsaturated->variant);
  ASSERT_NE(unsaturated_add, nullptr);
  EXPECT_FALSE(unsaturated_add->saturate.value);
  EXPECT_TRUE(unsaturated_add->saturate.locs.empty());

  const auto saturated_ast = parse_instruction("add.sat.u8x4 %r0, %r1, %r2;");
  const auto saturated = resolve<Add>(saturated_ast);
  ASSERT_TRUE(saturated.has_value()) << saturated.error().message;
  const auto* saturated_add =
      std::get_if<Add::PackedOptionalSat>(&saturated->variant);
  ASSERT_NE(saturated_add, nullptr);
  EXPECT_TRUE(saturated_add->saturate.value);
  ASSERT_EQ(saturated_add->saturate.locs.size(), 1U);
  EXPECT_EQ(saturated_add->saturate.locs.front(),
            saturated_ast.modifiers.front().syntax.range);
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
  const auto fields = resolve_fields(ast, syntax_descriptor, resolved_descriptor,
                                     "Comparison");
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

TEST(ResolveBar, BuildsPredicateReductionWithThreadCount) {
  const auto ast = parse_instruction("bar.cta.red.and.pred %p0, 1, 64, !%p1;");

  const auto resolved = resolve<Bar>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* bar = std::get_if<Bar::CtaRedAndPred>(&resolved->variant);
  ASSERT_NE(bar, nullptr);
  EXPECT_EQ(bar->operand_layout, (ResolvedOperandLayoutTag{1}));
  ASSERT_TRUE(
      std::holds_alternative<Bar::CtaRedAndPred::WithThreadCountOperands>(
          bar->operands));
  const auto& operands =
      std::get<Bar::CtaRedAndPred::WithThreadCountOperands>(bar->operands);
  EXPECT_EQ(operands.dst.value.register_ref.spelling, "%p0");
  EXPECT_EQ(operands.dst.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.dst.value.register_ref.index, 0U);
  EXPECT_FALSE(operands.dst.value.negated);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.barrier.value).bits, 1U);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.thread_count.value).bits, 64U);
  EXPECT_EQ(operands.predicate.value.register_ref.spelling, "%p1");
  EXPECT_EQ(operands.predicate.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.predicate.value.register_ref.index, 1U);
  EXPECT_TRUE(operands.predicate.value.negated);
  ASSERT_EQ(operands.predicate.locs.size(), 1U);
  EXPECT_EQ(operands.predicate.locs.front(),
            std::get<syntax_ast::AstPredicateOperand>(ast.operands[3]).range);

  const checker::Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(*resolved, context).has_value());
}

TEST(ResolveBar, RejectsGeneralRegisterInPredicateSlot) {
  const auto ast = parse_instruction("bar.red.and.pred %p0, 1, %r1;");

  const auto resolved = resolve<Bar>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().range,
      std::get<syntax_ast::AstIdentifierRef>(ast.operands[2]).syntax.range);
  EXPECT_EQ(resolved.error().message,
            "Expected a predicate register, got '%r1'.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
