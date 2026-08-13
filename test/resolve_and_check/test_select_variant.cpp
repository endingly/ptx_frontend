#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>

#include "ptx_ir/resolved/ptx_resolved_ir.hpp"
#include "ptx_ir/syntax/ptx_syntax_parser.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

static_assert(!std::is_nothrow_constructible_v<WithLocs<std::string>,
                                               std::string&&, SourceRange>);

TEST(ScalarTypeMetadata, InvalidHasInvalidKindAndZeroSize) {
  EXPECT_EQ(scalar_kind(ScalarType::Invalid), ScalarKind::Invalid);
  EXPECT_EQ(scalar_size_of(ScalarType::Invalid), 0U);
}

syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.error().message;
  return std::move(*ast);
}

syntax_ast::AstImmediate parse_immediate(std::string_view literal) {
  const auto ast = parse_instruction(std::string("add.u32 %r0, %r1, ") +
                                     std::string(literal) + ";");
  return std::get<syntax_ast::AstImmediate>(ast.operands.back());
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
  expect_variant("add.f32.f16 %f0, %h1, %f2;",
                 Add::VariantType::MixedF32);
  expect_variant("add.rz.f32.bf16.sat %f0, %h1, %f2;",
                 Add::VariantType::MixedF32);
  expect_variant("add.sat.bf16.f32.rz %f0, %h1, %f2;",
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
  expect_variant("sub.sat.s32 %r0, %r1, %r2;",
                 Sub::VariantType::OptionalSat);
  expect_variant("sub.u8x4 %r0, %r1, %r2;",
                 Sub::VariantType::OptionalSat);
  expect_variant("sub.sat.s8x4 %r0, %r1, %r2;",
                 Sub::VariantType::OptionalSat);
  expect_variant("sub.rz.ftz.sat.f32 %f0, %f1, %f2;",
                 Sub::VariantType::FloatF32);
  expect_variant("sub.rp.f32x2 %r0, %r1, %r2;",
                 Sub::VariantType::FloatF32x2);
  expect_variant("sub.rm.f64 %fd0, %fd1, %fd2;",
                 Sub::VariantType::FloatF64);
  expect_variant("sub.rn.ftz.sat.f16x2 %r0, %r1, %r2;",
                 Sub::VariantType::Half);
  expect_variant("sub.bf16 %r0, %r1, %r2;", Sub::VariantType::Bfloat);
  expect_variant("sub.f32.f16 %f0, %h1, %f2;",
                 Sub::VariantType::MixedF32);
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

TEST(ResolveAdd, RejectsMismatchedOpcode) {
  const auto ast = parse_instruction("sub.u32 %r0, %r1, %r2;");

  const auto resolved = resolve<Add>(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.opcode.syntax.range);
  EXPECT_EQ(resolved.error().message, "Cannot resolve opcode 'sub' as 'add'.");
}

TEST(CollectActualModifiersAdd, BindsSpellingsToSelectedVariantSlots) {
  const auto ast =
      parse_instruction("add.rz.f32.bf16.sat %f0, %h1, %f2;");
  const auto& instruction = Add::get_syntax_descriptor();
  const auto mixed = std::ranges::find_if(instruction.variants, [](auto variant) {
    return variant.variant_name == "MixedF32";
  });
  ASSERT_NE(mixed, instruction.variants.end());

  const auto actual = collect_actual_modifiers(ast, *mixed);

  ASSERT_TRUE(actual.has_value()) << actual.error().message;
  ASSERT_EQ(actual->size(), 4U);
  EXPECT_EQ(actual->at("rounding"), &ast.modifiers[0]);
  EXPECT_EQ(actual->at("result_type"), &ast.modifiers[1]);
  EXPECT_EQ(actual->at("input_type"), &ast.modifiers[2]);
  EXPECT_EQ(actual->at("sat"), &ast.modifiers[3]);
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
  const auto ast =
      parse_instruction("add.rz.f32.bf16.sat %f0, %h1, %f2;");

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
  ASSERT_TRUE(integer_resolved.has_value())
      << integer_resolved.error().message;
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

  const auto mixed_ast =
      parse_instruction("add.ftz.f32.f16 %f0, %h1, %f2;");
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
  EXPECT_EQ(
      operands.predicate.locs.front(),
      std::get<syntax_ast::AstPredicateOperand>(ast.operands[3]).syntax.range);

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
