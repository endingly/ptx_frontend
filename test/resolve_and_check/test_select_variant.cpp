#include <gtest/gtest.h>

#include <limits>

#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.error().message;
  return std::move(*ast);
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
  expect_variant("add.sat.s32 %r0, %r1, %r2;", Add::VariantType::SatS32);
  expect_variant("add.u16x2 %r0, %r1, %r2;",
                 Add::VariantType::SimdNoSatSm90);
  expect_variant("add.u8x4 %r0, %r1, %r2;",
                 Add::VariantType::PackedOptionalSatSm120);
  expect_variant("add.sat.u32 %r0, %r1, %r2;",
                 Add::VariantType::SatSm120);
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
  expect_variant("bar.red.popc.u32 %r0, 1, %p1;",
                 Bar::VariantType::RedPopcU32);
  expect_variant("bar.cta.red.popc.u32 %r0, 1, !%p1;",
                 Bar::VariantType::CtaRedPopcU32);
  expect_variant("bar.red.and.pred %p0, 1, %p1;",
                 Bar::VariantType::RedAndPred);
  expect_variant("bar.cta.red.and.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedAndPred);
  expect_variant("bar.red.or.pred %p0, 1, %p1;",
                 Bar::VariantType::RedOrPred);
  expect_variant("bar.cta.red.or.pred %p0, 1, !%p1;",
                 Bar::VariantType::CtaRedOrPred);
}

TEST(SelectVariantAdd, ReportsUnknownModifier) {
  const auto ast = parse_instruction("add.invalid %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.modifiers.front().syntax.range);
  EXPECT_EQ(selected.error().message, "Unknown modifier '.invalid'.");
}

TEST(SelectVariantAdd, ReportsDuplicateModifierKind) {
  const auto ast = parse_instruction("add.u32.u32 %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.modifiers.back().syntax.range);
  EXPECT_EQ(selected.error().message, "Duplicate 'type' modifier.");
}

TEST(CollectActualModifiersAdd, TypeAdapterUsesDescriptorImplementation) {
  const auto ast = parse_instruction("add.sat.u32 %r0, %r1, %r2;");

  const auto by_type = collect_actual_modifiers<Add>(ast);
  const auto by_descriptor =
      collect_actual_modifiers(ast, Add::get_syntax_descriptor());

  ASSERT_TRUE(by_type.has_value()) << by_type.error().message;
  ASSERT_TRUE(by_descriptor.has_value()) << by_descriptor.error().message;
  EXPECT_EQ(by_type->size(), by_descriptor->size());
  EXPECT_EQ(by_type->at("sat"), by_descriptor->at("sat"));
  EXPECT_EQ(by_type->at("type"), by_descriptor->at("type"));
}

TEST(ResolvedDescriptorAdd, OwnsResolvedFieldBindings) {
  const auto& descriptor = Add::get_resolved_descriptor();

  ASSERT_EQ(descriptor.opcode_name, "add");
  ASSERT_EQ(descriptor.variants.size(), 5U);

  const auto& packed_optional_sat = descriptor.variants[3];
  EXPECT_EQ(packed_optional_sat.variant_name, "PackedOptionalSatSm120");
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
  EXPECT_EQ(bindings[1].allowed_shapes,
            check_end::OperandShape::Register |
                check_end::OperandShape::Immediate);

  const auto& sat_s32 = descriptor.variants[1];
  ASSERT_EQ(sat_s32.modifier_bindings.size(), 2U);
  EXPECT_EQ(sat_s32.modifier_bindings[0].source_kind_id, "sat");
  EXPECT_EQ(sat_s32.modifier_bindings[0].target_field_id, "saturate");
  EXPECT_EQ(sat_s32.modifier_bindings[1].source_kind_id, "type");
  EXPECT_EQ(sat_s32.modifier_bindings[1].target_field_id, "type");
}

TEST(SelectVariantAdd, ReportsUnmatchedModifierCombination) {
  const auto ast = parse_instruction("add.sat.u64 %r0, %r1, %r2;");

  const auto selected = selectVariant<Add>(ast);

  ASSERT_FALSE(selected.has_value());
  EXPECT_EQ(selected.error().range, ast.range);
  EXPECT_EQ(selected.error().message,
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
  EXPECT_EQ(add->dst.value, (ResolvedRegisterId{4}));
  EXPECT_EQ(std::get<ResolvedRegisterId>(add->src1.value),
            (ResolvedRegisterId{5}));

  const auto* immediate = std::get_if<ResolvedImmediate>(&add->src2.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->bits, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(immediate->type, ScalarType::S32);
  ASSERT_EQ(add->src2.locs.size(), 1U);
  EXPECT_EQ(add->src2.locs.front(),
            std::get<syntax_ast::AstImmediate>(ast.operands[2]).syntax.range);
}

TEST(ResolveAdd, UsesFixedModifierConstantsForSatS32) {
  const auto ast = parse_instruction("add.sat.s32 %r4, %r5, -1;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::SatS32>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  EXPECT_TRUE(Add::SatS32::saturate);
  EXPECT_EQ(Add::SatS32::type, ScalarType::S32);

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

  const auto* dst = std::get_if<WithLocs<ResolvedRegisterId>>(
      &fields->operands.at("dst"));
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(dst->value, (ResolvedRegisterId{4}));

  const auto* src1 =
      std::get_if<WithLocs<RegOrImm>>(&fields->operands.at("src1"));
  ASSERT_NE(src1, nullptr);
  EXPECT_EQ(std::get<ResolvedRegisterId>(src1->value),
            (ResolvedRegisterId{5}));

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

TEST(ResolveAdd, PreservesOptionalModifierPresence) {
  const auto unsaturated_ast = parse_instruction("add.u8x4 %r0, %r1, %r2;");
  const auto unsaturated = resolve<Add>(unsaturated_ast);
  ASSERT_TRUE(unsaturated.has_value()) << unsaturated.error().message;
  const auto* unsaturated_add =
      std::get_if<Add::PackedOptionalSatSm120>(&unsaturated->variant);
  ASSERT_NE(unsaturated_add, nullptr);
  EXPECT_FALSE(unsaturated_add->saturate.value);
  EXPECT_TRUE(unsaturated_add->saturate.locs.empty());

  const auto saturated_ast =
      parse_instruction("add.sat.u8x4 %r0, %r1, %r2;");
  const auto saturated = resolve<Add>(saturated_ast);
  ASSERT_TRUE(saturated.has_value()) << saturated.error().message;
  const auto* saturated_add =
      std::get_if<Add::PackedOptionalSatSm120>(&saturated->variant);
  ASSERT_NE(saturated_add, nullptr);
  EXPECT_TRUE(saturated_add->saturate.value);
  ASSERT_EQ(saturated_add->saturate.locs.size(), 1U);
  EXPECT_EQ(saturated_add->saturate.locs.front(),
            saturated_ast.modifiers.front().syntax.range);
}

TEST(ResolveBar, BuildsPredicateReductionWithThreadCount) {
  const auto ast =
      parse_instruction("bar.cta.red.and.pred %p0, 1, 64, !%p1;");

  const auto resolved = resolve<Bar>(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* bar = std::get_if<Bar::CtaRedAndPred>(&resolved->variant);
  ASSERT_NE(bar, nullptr);
  EXPECT_EQ(bar->operand_layout, (ResolvedOperandLayoutTag{1}));
  ASSERT_TRUE(std::holds_alternative<
              Bar::CtaRedAndPred::WithThreadCountOperands>(bar->operands));
  const auto& operands = std::get<Bar::CtaRedAndPred::WithThreadCountOperands>(
      bar->operands);
  EXPECT_EQ(operands.dst.value.register_id, (ResolvedRegisterId{0}));
  EXPECT_FALSE(operands.dst.value.negated);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.barrier.value).bits, 1U);
  EXPECT_EQ(std::get<ResolvedImmediate>(operands.thread_count.value).bits, 64U);
  EXPECT_EQ(operands.predicate.value.register_id, (ResolvedRegisterId{1}));
  EXPECT_TRUE(operands.predicate.value.negated);
  ASSERT_EQ(operands.predicate.locs.size(), 1U);
  EXPECT_EQ(operands.predicate.locs.front(),
            std::get<syntax_ast::AstPredicateOperand>(ast.operands[3])
                .syntax.range);

  const checker::Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(*resolved, context).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
