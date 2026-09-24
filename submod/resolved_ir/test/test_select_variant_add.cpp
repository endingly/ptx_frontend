#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
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

TEST(CollectActualModifiersAdd, BindsCanonicalMixedPrecisionSlots) {
  const auto ast = parse_instruction("add.rz.sat.f32.bf16 %f0, %h1, %f2;");
  const auto& instruction = Add::get_syntax_descriptor();
  const auto mixed = std::ranges::find_if(
      instruction.variants,
      [](auto variant) { return variant.variant_name == "MixedF32"; });
  ASSERT_NE(mixed, instruction.variants.end());

  const auto actual = collect_actual_modifiers(ast, *mixed);

  ASSERT_TRUE(actual.has_value()) << actual.error().message;
  ASSERT_EQ(actual->size(), 4U);
  EXPECT_EQ(actual->at("rounding"), &ast.modifiers[0]);
  EXPECT_EQ(actual->at("sat"), &ast.modifiers[1]);
  EXPECT_EQ(actual->at("result_type"), &ast.modifiers[2]);
  EXPECT_EQ(actual->at("input_type"), &ast.modifiers[3]);
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
  EXPECT_EQ(
      actual.error().message,
      "Modifier combination does not match instruction variant 'MixedF32'.");
}

TEST(ResolvedDescriptorAdd, OwnsResolvedFieldBindings) {
  const auto& descriptor = Add::get_resolved_descriptor();

  ASSERT_EQ(descriptor.opcode_name, "add");
  ASSERT_EQ(descriptor.variants.size(), 11U);

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
  // The addend is immediate-capable, so the reference is unwrapped explicitly.
  EXPECT_EQ(std::get<ResolvedRegisterRef>(add->addend.value).spelling, "%f2");
  EXPECT_EQ(add->input_type.locs.front(), ast.modifiers[2].syntax.range);
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, GeneratedAddWrapperUsesYamlAvailability) {
  PtxSyntaxParser parser("add.sat.u8x4 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;

  constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const Context unsupported_context{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .enabled_family_features = families},
      .instruction_range = ast->range,
  };

  const auto unsupported = check(*resolved, unsupported_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_context{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .enabled_family_features = families},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, supported_context).has_value());
}

TEST(ResolvedIrChecker, GeneratedMergedAddVariantsUseValueAvailability) {
  PtxSyntaxParser simd_parser("add.u16x2 %r0, %r1, %r2;");
  const auto simd_ast = simd_parser.parseInstruction();
  ASSERT_TRUE(simd_ast.has_value()) << simd_ast.diagnostics.front().message;

  const auto simd = resolve<Add>(*simd_ast);
  ASSERT_TRUE(simd.has_value()) << simd.error().message;
  ASSERT_NE(std::get_if<Add::IntegerNoSat>(&simd->variant), nullptr);

  const Context old_simd_target{
      .target = {.ptx_version = {7, 9}, .sm_version = 80},
      .instruction_range = simd_ast->range,
  };
  const auto unsupported_simd = check(*simd, old_simd_target);
  ASSERT_FALSE(unsupported_simd.has_value());
  ASSERT_EQ(unsupported_simd.error().size(), 2U);
  EXPECT_EQ(unsupported_simd.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported_simd.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_simd_target{
      .target = {.ptx_version = {8, 0}, .sm_version = 90},
      .instruction_range = simd_ast->range,
  };
  EXPECT_TRUE(check(*simd, supported_simd_target).has_value());

  PtxSyntaxParser sat_parser("add.sat.u32 %r0, %r1, %r2;");
  const auto sat_ast = sat_parser.parseInstruction();
  ASSERT_TRUE(sat_ast.has_value()) << sat_ast.diagnostics.front().message;

  const auto sat = resolve<Add>(*sat_ast);
  ASSERT_TRUE(sat.has_value()) << sat.error().message;
  ASSERT_NE(std::get_if<Add::Sat>(&sat->variant), nullptr);

  constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const Context old_sat_target{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .enabled_family_features = families},
      .instruction_range = sat_ast->range,
  };
  const auto unsupported_sat = check(*sat, old_sat_target);
  ASSERT_FALSE(unsupported_sat.has_value());
  ASSERT_EQ(unsupported_sat.error().size(), 2U);
  EXPECT_EQ(unsupported_sat.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported_sat.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_sat_target{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .enabled_family_features = families},
      .instruction_range = sat_ast->range,
  };
  EXPECT_TRUE(check(*sat, supported_sat_target).has_value());
}

TEST(ResolvedIrChecker, ChecksFloatingAddRoundingValueAvailability) {
  PtxSyntaxParser parser("add.rm.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* add = std::get_if<Add::FloatF32>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  EXPECT_EQ(add->rounding.value, RoundingMode::Rm);

  const Context sm10_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = ast->range,
  };
  const auto unsupported = check(*resolved, sm10_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 1U);
  EXPECT_EQ(unsupported.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(unsupported.error().front().range,
            ast->modifiers.front().syntax.range);

  const Context sm20_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 20},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, sm20_context).has_value());
}

TEST(ResolvedIrChecker, ChecksFloatingAddVariantAvailability) {
  PtxSyntaxParser f64_parser("add.f64 %fd0, %fd1, %fd2;");
  const auto f64_ast = f64_parser.parseInstruction();
  ASSERT_TRUE(f64_ast.has_value()) << f64_ast.diagnostics.front().message;
  const auto f64 = resolve<Add>(*f64_ast);
  ASSERT_TRUE(f64.has_value()) << f64.error().message;

  const Context sm12_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 12},
      .instruction_range = f64_ast->range,
  };
  const auto unsupported_f64 = check(*f64, sm12_context);
  ASSERT_FALSE(unsupported_f64.has_value());
  ASSERT_EQ(unsupported_f64.error().size(), 1U);
  EXPECT_EQ(unsupported_f64.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  PtxSyntaxParser half_parser("add.f16 %h0, %h1, %h2;");
  const auto half_ast = half_parser.parseInstruction();
  ASSERT_TRUE(half_ast.has_value()) << half_ast.diagnostics.front().message;
  const auto half = resolve<Add>(*half_ast);
  ASSERT_TRUE(half.has_value()) << half.error().message;

  const Context old_half_context{
      .target = {.ptx_version = {4, 1}, .sm_version = 52},
      .instruction_range = half_ast->range,
  };
  const auto unsupported_half = check(*half, old_half_context);
  ASSERT_FALSE(unsupported_half.has_value());
  ASSERT_EQ(unsupported_half.error().size(), 2U);
  EXPECT_EQ(unsupported_half.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported_half.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_half_context{
      .target = {.ptx_version = {4, 2}, .sm_version = 53},
      .instruction_range = half_ast->range,
  };
  EXPECT_TRUE(check(*half, supported_half_context).has_value());
}

TEST(ResolvedIrChecker, ChecksMixedPrecisionAddAvailability) {
  PtxSyntaxParser parser("add.rz.f32.bf16.sat %f0, %h1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Add::MixedF32>(&resolved->variant), nullptr);

  const Context old_target{
      .target = {.ptx_version = {8, 5}, .sm_version = 90},
      .instruction_range = ast->range,
  };
  const auto unsupported = check(*resolved, old_target);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context supported_target{
      .target = {.ptx_version = {8, 6}, .sm_version = 100},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, supported_target).has_value());
}

TEST(ResolvedIrChecker, GeneratedAddWrapperChecksImmediateTypeExpression) {
  PtxSyntaxParser parser("add.s32 %r0, %r1, 7;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  auto* immediate = std::get_if<ResolvedImmediate>(&add->src2.value);
  ASSERT_NE(immediate, nullptr);
  immediate->type = ScalarType::F32;

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().range,
            std::get<syntax_ast::AstImmediate>(ast->operands[2]).syntax.range);
}

TEST(ResolvedIrChecker, GeneratedAddWrapperChecksSelectedOperandLayoutTag) {
  PtxSyntaxParser parser("add.s32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto resolved = resolve<Add>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* add = std::get_if<Add::IntegerNoSat>(&resolved->variant);
  ASSERT_NE(add, nullptr);
  add->operand_layout = ResolvedOperandLayoutTag{1};

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::InvalidOperandLayoutTag);
  EXPECT_EQ(result.error().front().range, ast->range);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
