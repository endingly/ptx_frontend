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

TEST(ResolveSelp, SelectsFrozenU32Variant) {
  const auto ast = parse_instruction("selp.u32 %r0, %r1, 0, %p0;");
  const auto resolved = resolve<Selp>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* selp = std::get_if<Selp::U32>(&resolved->variant);
  ASSERT_NE(selp, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(selp->src_false.value));
  EXPECT_FALSE(selp->predicate.value.negated);
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
