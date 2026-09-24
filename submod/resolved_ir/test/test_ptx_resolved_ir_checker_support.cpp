#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Stable source range used to check diagnostic placement. */
const SourceRange kInstructionRange{{4, 3}, {4, 17}};

/** Availability rule deliberately rejected by the fixture target. */
constexpr AvailabilityDescriptor kRejectedDnfAvailability{
    .any_of = {{
        {.has_exact_target = true,
         .exact_target_architecture = {100},
         .exact_target_flavor = base::TargetFlavor::ArchitectureSpecific,
         .capabilities = {"tensor"},
         .capability_count = 1},
    }},
    .any_of_count = 1,
};

/** Build a target context that cannot satisfy the test DNF availability. */
Context rejected_dnf_context() {
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .identity =
                     base::TargetIdentity{
                         {100}, base::TargetFlavor::Generic, "sm_100"}},
      .instruction_range = kInstructionRange,
  };
}

/** Assert that availability failure reports one stable diagnostic. */
void expect_single_unsupported_availability(const CheckResult& result) {
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::UnsupportedAvailability);
}

constexpr ModifierValueAvailabilityDescriptor kModifierValueAvailabilities[] = {
    {
        .kind_id = "type",
        .value_kind = ModifierValueKind::ScalarType,
        .scalar_type = ScalarType::U32,
        .availability =
            {
                .minimum_ptx_version = {2, 0},
                .minimum_sm_version = 20,
            },
    },
};

constexpr VariantDescriptor kVariants[] = {
    {
        .variant_name = "PackedOptionalSat",
        .availability =
            {
                .minimum_ptx_version = {9, 2},
                .minimum_sm_version = 120,
                .required_family = "sm_120f",
            },
        .modifier_value_availabilities = kModifierValueAvailabilities,
        .rule_id = "integer_arith.add_packed",
    },
};

constexpr InstructionDescriptor kInstruction{
    .opcode_name = "add",
    .variants = kVariants,
};

TEST(ResolvedIrChecker, AcceptsAvailableVariant) {
  constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const Context context{
      .target = {.ptx_version = {9, 2},
                 .sm_version = 120,
                 .enabled_family_features = families},
      .instruction_range = kInstructionRange,
  };

  const auto result = check_common(kInstruction, "PackedOptionalSat", context);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(is_available(kVariants[0].availability, context.target));
}

TEST(ResolvedIrChecker,
     UsesCatalogEnabledFamilyFeaturesForProductionAvailability) {
  const auto sm120f = base::find_target_profile("sm_120f");
  ASSERT_TRUE(sm120f.has_value());

  const auto target_info = [](const base::TargetProfile& profile) {
    return TargetInfo{
        .ptx_version = {9, 2},
        .sm_version = profile.identity.architecture.number,
        .enabled_family_features = profile.enabled_family_features,
        .identity = profile.identity,
        .capabilities = profile.capabilities,
    };
  };
  EXPECT_TRUE(is_available(kVariants[0].availability, target_info(*sm120f)));
  auto without_family = target_info(*sm120f);
  without_family.enabled_family_features = {};
  EXPECT_FALSE(is_available(kVariants[0].availability, without_family));
}

TEST(ResolvedIrChecker,
     KeepsFamilyFeatureRequirementsDistinctFromExactTargets) {
  const auto target_info = [](std::string_view spelling) {
    const auto profile = base::find_target_profile(spelling);
    EXPECT_TRUE(profile.has_value()) << spelling;
    if (!profile)
      return TargetInfo{};
    return TargetInfo{
        .ptx_version = {9, 3},
        .sm_version = profile->identity.architecture.number,
        .enabled_family_features = profile->enabled_family_features,
        .identity = profile->identity,
        .capabilities = profile->capabilities,
    };
  };
  constexpr AvailabilityDescriptor sm100f_family{
      .minimum_ptx_version = {8, 0},
      .minimum_sm_version = 100,
      .required_family = "sm_100f",
  };

  EXPECT_TRUE(is_available(sm100f_family, target_info("sm_100a")));
  EXPECT_TRUE(is_available(sm100f_family, target_info("sm_100f")));
  EXPECT_TRUE(is_available(sm100f_family, target_info("sm_103a")));
  EXPECT_TRUE(is_available(sm100f_family, target_info("sm_103f")));
  EXPECT_FALSE(is_available(sm100f_family, target_info("sm_100")));
  EXPECT_FALSE(is_available(sm100f_family, target_info("sm_103")));
  EXPECT_FALSE(is_available(sm100f_family, target_info("sm_90a")));
  EXPECT_FALSE(is_available(sm100f_family, target_info("sm_120f")));

  constexpr AvailabilityDescriptor sm103f_family{
      .minimum_ptx_version = {9, 3},
      .minimum_sm_version = 103,
      .required_family = "sm_103f",
  };
  EXPECT_TRUE(is_available(sm103f_family, target_info("sm_103a")));
  EXPECT_TRUE(is_available(sm103f_family, target_info("sm_103f")));
  EXPECT_FALSE(is_available(sm103f_family, target_info("sm_100a")));
  EXPECT_FALSE(is_available(sm103f_family, target_info("sm_100f")));
  EXPECT_FALSE(is_available(sm103f_family, target_info("sm_103")));
  EXPECT_FALSE(is_available(sm103f_family, target_info("sm_120f")));

  constexpr AvailabilityDescriptor sm100f_dnf_family{
      .any_of = {{
          {.minimum_ptx_version = {8, 8},
           .minimum_sm_version = 100,
           .required_family = "sm_100f"},
      }},
      .any_of_count = 1,
  };
  EXPECT_TRUE(is_available(sm100f_dnf_family, target_info("sm_100a")));
  EXPECT_TRUE(is_available(sm100f_dnf_family, target_info("sm_100f")));
  EXPECT_TRUE(is_available(sm100f_dnf_family, target_info("sm_103a")));
  EXPECT_TRUE(is_available(sm100f_dnf_family, target_info("sm_103f")));
  EXPECT_FALSE(is_available(sm100f_dnf_family, target_info("sm_100")));
  EXPECT_FALSE(is_available(sm100f_dnf_family, target_info("sm_103")));
  EXPECT_FALSE(is_available(sm100f_dnf_family, target_info("sm_90a")));
  EXPECT_FALSE(is_available(sm100f_dnf_family, target_info("sm_120f")));

  constexpr AvailabilityDescriptor exact_sm100a{
      .any_of = {{
          {.has_exact_target = true,
           .exact_target_architecture = {100},
           .exact_target_flavor = base::TargetFlavor::ArchitectureSpecific},
      }},
      .any_of_count = 1,
  };
  constexpr AvailabilityDescriptor exact_sm103{
      .any_of = {{
          {.has_exact_target = true,
           .exact_target_architecture = {103},
           .exact_target_flavor = base::TargetFlavor::Generic},
      }},
      .any_of_count = 1,
  };
  constexpr AvailabilityDescriptor exact_sm103f{
      .any_of = {{
          {.has_exact_target = true,
           .exact_target_architecture = {103},
           .exact_target_flavor = base::TargetFlavor::FamilySpecific},
      }},
      .any_of_count = 1,
  };
  constexpr AvailabilityDescriptor exact_sm103a{
      .any_of = {{
          {.has_exact_target = true,
           .exact_target_architecture = {103},
           .exact_target_flavor = base::TargetFlavor::ArchitectureSpecific},
      }},
      .any_of_count = 1,
  };
  constexpr AvailabilityDescriptor cluster_capability{
      .any_of = {{
          {.capabilities = {"cluster"}, .capability_count = 1},
      }},
      .any_of_count = 1,
  };
  EXPECT_TRUE(is_available(exact_sm100a, target_info("sm_100a")));
  EXPECT_FALSE(is_available(exact_sm100a, target_info("sm_100f")));
  EXPECT_TRUE(is_available(exact_sm103, target_info("sm_103")));
  EXPECT_FALSE(is_available(exact_sm103, target_info("sm_103f")));
  EXPECT_FALSE(is_available(exact_sm103, target_info("sm_103a")));
  EXPECT_FALSE(is_available(exact_sm103f, target_info("sm_103")));
  EXPECT_TRUE(is_available(exact_sm103f, target_info("sm_103f")));
  EXPECT_FALSE(is_available(exact_sm103f, target_info("sm_103a")));
  EXPECT_FALSE(is_available(exact_sm103a, target_info("sm_103")));
  EXPECT_FALSE(is_available(exact_sm103a, target_info("sm_103f")));
  EXPECT_TRUE(is_available(exact_sm103a, target_info("sm_103a")));
  EXPECT_TRUE(is_available(cluster_capability, target_info("sm_100")));
}

TEST(ResolvedIrChecker, EvaluatesBoundedAvailabilityDnf) {
  constexpr AvailabilityDescriptor availability{
      .any_of = {{
          {.minimum_ptx_version = {9, 0},
           .minimum_sm_version = 100,
           .has_exact_target = true,
           .exact_target_architecture = {100},
           .exact_target_flavor = base::TargetFlavor::ArchitectureSpecific,
           .capabilities = {"tensor", "cluster"},
           .capability_count = 2},
          {.minimum_ptx_version = {9, 2}, .minimum_sm_version = 120},
      }},
      .any_of_count = 2,
  };
  constexpr std::array<std::string_view, 2> capabilities{"tensor", "cluster"};
  TargetInfo exact{
      .ptx_version = {9, 0},
      .sm_version = 100,
      .identity =
          base::TargetIdentity{
              {100}, base::TargetFlavor::ArchitectureSpecific, "sm_100a"},
      .capabilities = capabilities,
  };
  EXPECT_TRUE(is_available(availability, exact));

  TargetInfo generic = exact;
  generic.identity =
      base::TargetIdentity{{100}, base::TargetFlavor::Generic, "sm_100"};
  EXPECT_FALSE(is_available(availability, generic));
  generic.identity = base::TargetIdentity{
      {100}, base::TargetFlavor::FamilySpecific, "sm_100f"};
  EXPECT_FALSE(is_available(availability, generic));

  TargetInfo missing_capability = exact;
  constexpr std::array<std::string_view, 1> one_capability{"tensor"};
  missing_capability.capabilities = one_capability;
  EXPECT_FALSE(is_available(availability, missing_capability));
  TargetInfo missing_identity = exact;
  missing_identity.identity.reset();
  EXPECT_FALSE(is_available(availability, missing_identity));

  TargetInfo fallback{.ptx_version = {9, 2}, .sm_version = 120};
  EXPECT_TRUE(is_available(availability, fallback));

  const VariantDescriptor variant{.variant_name = "Dnf",
                                  .availability = availability};
  const auto rejected = check_availability(
      variant,
      Context{.target = generic, .instruction_range = kInstructionRange});
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedAvailability);
}

TEST(ResolvedIrChecker, RejectsDnfAtEveryAvailabilityCheckerEntrypoint) {
  const Context context = rejected_dnf_context();

  static constexpr OperandLayoutDescriptor layouts[] = {{
      .layout_name = "Dnf",
      .availability = kRejectedDnfAvailability,
  }};
  constexpr VariantDescriptor layout_variant{
      .variant_name = "Dnf",
      .operand_layouts = layouts,
  };
  expect_single_unsupported_availability(
      check_operand_layout_availability(layout_variant, 0, context));

  constexpr ModifierValueAvailabilityDescriptor modifier_descriptors[] = {{
      .kind_id = "flag",
      .value_kind = ModifierValueKind::Bool,
      .bool_value = true,
      .availability = kRejectedDnfAvailability,
  }};
  constexpr ModifierValueView modifier_values[] = {{
      .kind_id = "flag",
      .value_kind = ModifierValueKind::Bool,
      .bool_value = true,
      .is_present = true,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  expect_single_unsupported_availability(check_modifier_value_availability(
      modifier_descriptors, modifier_values, context));

  constexpr OperandDescriptor value_descriptors[] = {{
      .target_field_id = "value",
      .role = OperandRole::Source,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Register,
  }};
  const OperandView value_operand{
      .field_id = "value",
      .actual_shape = OperandShape::Register,
      .value_availability = kRejectedDnfAvailability,
      .value_name = "late_value",
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  expect_single_unsupported_availability(check_operands(
      value_descriptors, {}, std::span{&value_operand, 1}, {}, context));

  static constexpr AddressStateSpaceDescriptor state_spaces[] = {{
      .state_space = MemoryStateSpace::Global,
      .availability = kRejectedDnfAvailability,
  }};
  constexpr OperandDescriptor state_descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .allowed_address_state_spaces = state_spaces,
  }};
  const OperandView state_operand{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Global,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  expect_single_unsupported_availability(check_operands(
      state_descriptors, {}, std::span{&state_operand, 1}, {}, context));

  constexpr OperandDescriptor parameter_descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .state_space_modifier_field_id = "state_space",
      .parameter_constraint =
          {
              .direction = ParameterDirection::Input,
              .function_availability = kRejectedDnfAvailability,
          },
  }};
  constexpr FieldView parameter_fields[] = {{
      .field_id = "state_space",
      .memory_state_space = MemoryStateSpace::Parameter,
  }};
  const OperandView parameter_operand{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Parameter,
      .enclosing_function_kind = EnclosingFunctionKind::Device,
      .parameter_direction = ParameterDirection::Input,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  expect_single_unsupported_availability(
      check_operands(parameter_descriptors, parameter_fields,
                     std::span{&parameter_operand, 1}, {}, context));

  constexpr VariantDescriptor::MemoryVectorDescriptor memory_vector{
      .type_field_id = "type",
      .vector_field_id = "vector",
      .address_field_id = "address",
      .availability = kRejectedDnfAvailability,
  };
  constexpr FieldView vector_fields[] = {{
      .field_id = "type",
      .scalar_type = ScalarType::U32,
  }};
  const OperandView vector_operands[] = {
      {.field_id = "vector",
       .actual_shape = OperandShape::Vector,
       .vector_arity = 8,
       .locations = std::span<const SourceRange>{&kInstructionRange, 1}},
      {.field_id = "address",
       .actual_shape = OperandShape::Address,
       .locations = std::span<const SourceRange>{&kInstructionRange, 1}},
  };
  expect_single_unsupported_availability(check_memory_vector(
      memory_vector, vector_fields, vector_operands, context));
}




TEST(ResolvedIrChecker, RejectsZeroImmediateMultipleDivisor) {
  constexpr VariantDescriptor::ImmediateMultipleOfDescriptor descriptor{
      .operand_field_id = "count",
      .divisor = 0,
  };
  const OperandView operand{
      .field_id = "count",
      .actual_shape = OperandShape::Immediate,
      .immediate_type = ScalarType::U32,
      .immediate_bits = 24,
      .immediate_is_negative = false,
  };
  const auto checked = check_immediate_multiple_of(
      descriptor, std::span<const OperandView>{&operand, 1},
      Context{.instruction_range = kInstructionRange});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, CheckDiagnosticKind::RuleViolation);
}

/** Verifies fixed rules consume integer source bits before use-width narrowing. */
TEST(ResolvedIrChecker, PreservesIntegerSourceBitsForFixedConstraints) {
  static constexpr std::array<uint64_t, 1> allowed_values{0};
  constexpr VariantDescriptor::ImmediateValueDescriptor value_descriptor{
      .operand_field_id = "control",
      .allowed_values = allowed_values,
  };
  constexpr VariantDescriptor::ImmediateRangeDescriptor range_descriptor{
      .operand_field_id = "control",
      .minimum = 0,
      .has_maximum = true,
      .maximum = 1,
  };
  constexpr VariantDescriptor::ImmediateMultipleOfDescriptor
      multiple_descriptor{
          .operand_field_id = "control",
          .divisor = 3,
      };
  PtxSyntaxParser high_word_parser("mov.u32 %r0, 4294967296;");
  const auto high_word_ast = high_word_parser.parseInstruction();
  ASSERT_TRUE(high_word_ast.has_value())
      << high_word_ast.diagnostics.front().message;
  const auto high_word = resolve_immediate_literal(
      std::get<syntax_ast::AstImmediate>(high_word_ast->operands.back()),
      ScalarType::U32);
  ASSERT_TRUE(high_word.has_value()) << high_word.error().message;
  EXPECT_EQ(high_word->bits, 0U);
  ASSERT_TRUE(high_word->integer_source_bits.has_value());
  OperandView control{
      .field_id = "control",
      .actual_shape = OperandShape::Immediate,
      .immediate_type = ScalarType::U32,
      .immediate_bits = high_word->bits,
      .immediate_is_negative = high_word->is_negative,
      .integer_source_bits = high_word->integer_source_bits,
  };
  const auto operands = std::span<const OperandView>{&control, 1};
  EXPECT_FALSE(
      check_immediate_value(value_descriptor, operands,
                            Context{.instruction_range = kInstructionRange})
          .has_value());
  EXPECT_FALSE(
      check_immediate_range(range_descriptor, operands,
                            Context{.instruction_range = kInstructionRange})
          .has_value());

  EXPECT_FALSE(check_immediate_multiple_of(
                   multiple_descriptor, operands,
                   Context{.instruction_range = kInstructionRange})
                   .has_value());

  PtxSyntaxParser minus_zero_parser("mov.u32 %r0, -0;");
  const auto minus_zero_ast = minus_zero_parser.parseInstruction();
  ASSERT_TRUE(minus_zero_ast.has_value())
      << minus_zero_ast.diagnostics.front().message;
  const auto minus_zero = resolve_immediate_literal(
      std::get<syntax_ast::AstImmediate>(minus_zero_ast->operands.back()),
      ScalarType::U32);
  ASSERT_TRUE(minus_zero.has_value()) << minus_zero.error().message;
  EXPECT_FALSE(minus_zero->is_negative);
  control.immediate_bits = minus_zero->bits;
  control.immediate_is_negative = minus_zero->is_negative;
  control.integer_source_bits = minus_zero->integer_source_bits;
  EXPECT_TRUE(
      check_immediate_value(value_descriptor, operands,
                            Context{.instruction_range = kInstructionRange})
          .has_value());
  EXPECT_TRUE(
      check_immediate_range(range_descriptor, operands,
                            Context{.instruction_range = kInstructionRange})
          .has_value());
  EXPECT_TRUE(check_immediate_multiple_of(
                  multiple_descriptor, operands,
                  Context{.instruction_range = kInstructionRange})
                  .has_value());
}


TEST(ResolvedIrChecker, AccumulatesTargetAvailabilityDiagnostics) {
  constexpr std::array<std::string_view, 1> families{"sm_100"};
  const Context context{
      .target = {.ptx_version = {9, 1},
                 .sm_version = 100,
                 .enabled_family_features = families},
      .instruction_range = kInstructionRange,
  };

  const auto result = check_common(kInstruction, "PackedOptionalSat", context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 3U);
  EXPECT_EQ(result.error()[0].kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(result.error()[1].kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error()[2].kind,
            CheckDiagnosticKind::UnsupportedTargetFamily);
  EXPECT_EQ(result.error()[0].range, kInstructionRange);
}

TEST(ResolvedIrChecker, DiagnosesMissingGeneratedVariantDescriptor) {
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_common(kInstruction, "Missing", context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::MissingVariantDescriptor);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksSelectedModifierValueAvailability) {
  constexpr std::array<ModifierValueView, 1> values{{
      {
          .kind_id = "type",
          .value_kind = ModifierValueKind::ScalarType,
          .scalar_type = ScalarType::U32,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = {{1, 1}, {1, 8}},
  };

  const auto result = check_modifier_value_availability(
      kModifierValueAvailabilities, values, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2U);
  EXPECT_EQ(result.error()[0].kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(result.error()[1].kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error()[0].range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksComparisonOperatorValueAvailability) {
  constexpr ModifierValueAvailabilityDescriptor descriptors[] = {{
      .kind_id = "comparison",
      .value_kind = ModifierValueKind::ComparisonOperator,
      .comparison_operator = ComparisonOperator::Lt,
      .availability = {.minimum_sm_version = 20},
  }};
  constexpr std::array<ModifierValueView, 1> values{{
      {
          .kind_id = "comparison",
          .value_kind = ModifierValueKind::ComparisonOperator,
          .comparison_operator = ComparisonOperator::Lt,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = kInstructionRange,
  };

  const auto result =
      check_modifier_value_availability(descriptors, values, context);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksBooleanOperatorValueAvailability) {
  constexpr ModifierValueAvailabilityDescriptor descriptors[] = {{
      .kind_id = "boolean",
      .value_kind = ModifierValueKind::BooleanOperator,
      .boolean_operator = BooleanOperator::Xor,
      .availability = {.minimum_sm_version = 20},
  }};
  constexpr std::array<ModifierValueView, 1> values{{
      {
          .kind_id = "boolean",
          .value_kind = ModifierValueKind::BooleanOperator,
          .boolean_operator = BooleanOperator::Xor,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = kInstructionRange,
  };

  const auto result =
      check_modifier_value_availability(descriptors, values, context);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksEvictionPriorityValueAvailability) {
  constexpr ModifierValueAvailabilityDescriptor descriptors[] = {{
      .kind_id = "eviction_priority",
      .value_kind = ModifierValueKind::EvictionPriority,
      .eviction_priority = EvictionPriority::EvictLast,
      .availability =
          {
              .minimum_ptx_version = {7, 4},
              .minimum_sm_version = 70,
          },
  }};
  constexpr std::array<ModifierValueView, 1> evict_last{{
      {
          .kind_id = "eviction_priority",
          .value_kind = ModifierValueKind::EvictionPriority,
          .eviction_priority = EvictionPriority::EvictLast,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const Context new_context{
      .target = {.ptx_version = {7, 4}, .sm_version = 70},
      .instruction_range = kInstructionRange,
  };
  EXPECT_TRUE(
      check_modifier_value_availability(descriptors, evict_last, new_context)
          .has_value());

  const Context old_context{
      .target = {.ptx_version = {7, 3}, .sm_version = 60},
      .instruction_range = kInstructionRange,
  };
  const auto rejected =
      check_modifier_value_availability(descriptors, evict_last, old_context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2U);
  EXPECT_EQ(rejected.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  constexpr std::array<ModifierValueView, 1> evict_first{{
      {
          .kind_id = "eviction_priority",
          .value_kind = ModifierValueKind::EvictionPriority,
          .eviction_priority = EvictionPriority::EvictFirst,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  EXPECT_TRUE(
      check_modifier_value_availability(descriptors, evict_first, old_context)
          .has_value());
}

TEST(ResolvedIrChecker, IgnoresOmittedCacheSentinelAndChecksExplicitCache) {
  constexpr ModifierValueAvailabilityDescriptor descriptors[] = {{
      .kind_id = "cache",
      .value_kind = ModifierValueKind::CacheOperator,
      .cache_operator = CacheOperator::Ca,
      .availability =
          {
              .minimum_ptx_version = {2, 0},
              .minimum_sm_version = 20,
          },
  }};
  constexpr std::array<ModifierValueView, 1> omitted{{
      {
          .kind_id = "cache",
          .value_kind = ModifierValueKind::CacheOperator,
          .cache_operator = CacheOperator::Unspecified,
          .is_present = false,
      },
  }};
  const Context old_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = {{1, 1}, {1, 8}},
  };
  EXPECT_TRUE(
      check_modifier_value_availability(descriptors, omitted, old_context)
          .has_value());

  constexpr std::array<ModifierValueView, 1> explicit_cache{{
      {
          .kind_id = "cache",
          .value_kind = ModifierValueKind::CacheOperator,
          .cache_operator = CacheOperator::Ca,
          .is_present = true,
          .locations = std::span<const SourceRange>{&kInstructionRange, 1},
      },
  }};
  const auto rejected = check_modifier_value_availability(
      descriptors, explicit_cache, old_context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2U);
  EXPECT_EQ(rejected.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(rejected.error()[0].range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksFixedScalarOperandTypeDescriptor) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "barrier",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::U32,
          },
      .role = OperandRole::Barrier,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Immediate,
  }};
  constexpr OperandView operands[] = {{
      .field_id = "barrier",
      .actual_shape = OperandShape::Immediate,
      .immediate_type = ScalarType::F32,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_operands(descriptors, {}, operands, {}, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, AppliesRegisterWidthPolicyFromOperandDescriptor) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "value",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::U16,
          },
      .register_width_policy = base::ScalarTypeSizePolicy::EqualOrWider,
      .role = OperandRole::Source,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Register,
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView operand{
      .field_id = "value",
      .actual_shape = OperandShape::Register,
      .register_type = ScalarType::U32,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const auto check_operand = [&](ScalarType actual_type) {
    operand.register_type = actual_type;
    return check_operands(descriptors, {},
                          std::span<const OperandView>{&operand, 1}, {},
                          context);
  };

  EXPECT_TRUE(check_operand(ScalarType::U32).has_value());
  EXPECT_TRUE(check_operand(ScalarType::B64).has_value());

  const auto narrow = check_operand(ScalarType::U8);
  ASSERT_FALSE(narrow.has_value());
  ASSERT_EQ(narrow.error().size(), 1u);
  EXPECT_EQ(narrow.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);

  const auto float_integer = check_operand(ScalarType::F32);
  ASSERT_FALSE(float_integer.has_value());
  ASSERT_EQ(float_integer.error().size(), 1u);
  EXPECT_EQ(float_integer.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedIrChecker, ChecksDynamicVectorArityAndElementPolicy) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "dst",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::U16,
          },
      .register_width_policy = base::ScalarTypeSizePolicy::EqualOrWider,
      .role = OperandRole::Destination,
      .access = OperandAccess::Write,
      .allowed_shapes = OperandShape::Vector,
      .vector_arity_modifier_field_id = "vector",
      .vector_type_policy = VectorTypePolicy::Element,
  }};
  constexpr FieldView fields[] = {{
      .field_id = "vector",
      .vector_arity = VectorArity::V2,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView operand{
      .field_id = "dst",
      .actual_shape = OperandShape::Vector,
      .vector_element_types = {ScalarType::U16, ScalarType::U32},
      .vector_arity = 2,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };

  EXPECT_TRUE(check_operands(descriptors, fields,
                             std::span<const OperandView>{&operand, 1}, {},
                             context)
                  .has_value());

  operand.vector_arity = 4;
  auto rejected =
      check_operands(descriptors, fields,
                     std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  operand.vector_arity = 2;
  operand.vector_sink_count = 1;
  rejected =
      check_operands(descriptors, fields,
                     std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  operand.vector_sink_count = 0;
  operand.vector_element_types = {ScalarType::U8, ScalarType::U32};
  rejected =
      check_operands(descriptors, fields,
                     std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);

  operand.vector_element_types = {ScalarType::F32, ScalarType::U32};
  rejected =
      check_operands(descriptors, fields,
                     std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);

  operand.vector_element_types = {ScalarType::U16, ScalarType::U32};
  rejected = check_operands(
      descriptors, {}, std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::MissingVectorArityField);
}

TEST(ResolvedIrChecker, ChecksVectorSinkPayloadRequirement) {
  constexpr std::array<uint8_t, 1> allowed_vector_arities = {2};
  const OperandDescriptor descriptors[] = {{
      .target_field_id = "dst",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::B32,
          },
      .role = OperandRole::Destination,
      .access = OperandAccess::Write,
      .allowed_shapes = OperandShape::Vector,
      .allowed_vector_arities = allowed_vector_arities,
      .vector_type_policy = VectorTypePolicy::Element,
      .allow_vector_sink = true,
      .vector_sink_payload_bits = 256,
  }};
  const OperandView operand{
      .field_id = "dst",
      .actual_shape = OperandShape::Vector,
      .vector_element_types = {ScalarType::B32, ScalarType::B32},
      .vector_arity = 2,
      .vector_sink_count = 1,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto rejected = check_operands(
      descriptors, {}, std::span<const OperandView>{&operand, 1}, {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);
}

TEST(ResolvedIrChecker, ChecksModernBracePackCardinalityAndElementShapes) {
  constexpr OperandDescriptor tensor[] = {{
      .target_field_id = "coordinate",
      .type_expression = {.kind = OperandTypeExpressionKind::None},
      .role = OperandRole::Source,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Vector,
      .minimum_elements = 1,
      .maximum_elements = 5,
      .allowed_element_shapes =
          OperandShape::Register | OperandShape::Immediate,
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView coordinate{
      .field_id = "coordinate",
      .actual_shape = OperandShape::Vector,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const auto check_tensor = [&] {
    return check_operands(
        tensor, {}, std::span<const OperandView>{&coordinate, 1}, {}, context);
  };

  auto rejected = check_tensor();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  coordinate.vector_arity = 6;
  rejected = check_tensor();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  for (uint8_t arity = 1; arity <= 5; ++arity) {
    coordinate.vector_arity = arity;
    coordinate.vector_element_shapes.fill(OperandShape::Register);
    if (arity > 1)
      coordinate.vector_element_shapes[1] = OperandShape::Immediate;
    EXPECT_TRUE(check_tensor().has_value()) << "arity " << unsigned{arity};
  }

  coordinate.vector_arity = 1;
  coordinate.vector_element_shapes.fill(OperandShape::Address);
  rejected = check_tensor();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);
}

TEST(ResolvedIrChecker, ChecksModernMatrixFragmentShapes) {
  constexpr OperandDescriptor matrix[] = {{
      .target_field_id = "fragment",
      .type_expression = {.kind = OperandTypeExpressionKind::None},
      .role = OperandRole::Source,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Vector,
      .minimum_elements = 1,
      .maximum_elements = 64,
      .allowed_element_shapes = OperandShape::Register,
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView fragment{
      .field_id = "fragment",
      .actual_shape = OperandShape::Vector,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const auto check_matrix = [&] {
    return check_operands(
        matrix, {}, std::span<const OperandView>{&fragment, 1}, {}, context);
  };

  fragment.vector_arity = 1;
  fragment.vector_element_shapes.fill(OperandShape::Register);
  EXPECT_TRUE(check_matrix().has_value());

  fragment.vector_arity = 64;
  EXPECT_TRUE(check_matrix().has_value());

  fragment.vector_arity = 65;
  auto rejected = check_matrix();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  fragment.vector_arity = 1;
  fragment.vector_element_shapes.fill(OperandShape::Immediate);
  rejected = check_matrix();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);

  fragment.vector_element_shapes.fill(OperandShape{});
  fragment.vector_sink_count = 1;
  rejected = check_matrix();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedOperandShape);
}

TEST(ResolvedIrChecker, RejectsOverwideVectorOperandPayload) {
  const std::array<uint8_t, 3> allowed_vector_arities = {2, 4, 8};
  const OperandDescriptor descriptors[] = {{
      .target_field_id = "dst",
      .type_expression =
          {
              .kind = OperandTypeExpressionKind::FixedScalar,
              .fixed_scalar_type = ScalarType::U64,
          },
      .register_width_policy = base::ScalarTypeSizePolicy::EqualOrWider,
      .role = OperandRole::Destination,
      .access = OperandAccess::Write,
      .allowed_shapes = OperandShape::Vector,
      .allowed_vector_arities = allowed_vector_arities,
      .vector_type_policy = VectorTypePolicy::Element,
  }};
  OperandView operand{
      .field_id = "dst",
      .actual_shape = OperandShape::Vector,
      .vector_element_types = {ScalarType::U64, ScalarType::U64,
                               ScalarType::U64, ScalarType::U64,
                               ScalarType::U64, ScalarType::U64,
                               ScalarType::U64, ScalarType::U64},
      .vector_arity = 8,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto rejected = check_operands(
      descriptors, {}, std::span<const OperandView>{&operand, 1}, {}, context);

  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(rejected.error().front().message,
            "Vector operand 'dst' payload width (512 bits) exceeds the "
            "supported 256 bit limit.");
  EXPECT_EQ(rejected.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, DiagnosesMissingStateSpaceField) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .state_space_modifier_field_id = "state_space",
  }};
  constexpr OperandView operands[] = {{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Global,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_operands(descriptors, {}, operands, {}, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::MissingStateSpaceField);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, RejectsAddressOutsideStaticStateSpaceAllowlist) {
  static constexpr AddressStateSpaceDescriptor allowed_state_spaces[] = {{
      .state_space = MemoryStateSpace::Global,
  }};
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .allowed_address_state_spaces = allowed_state_spaces,
  }};
  constexpr OperandView operands[] = {{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Parameter,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto result = check_operands(descriptors, {}, operands, {}, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(result.error().front().range, kInstructionRange);
}

TEST(ResolvedIrChecker, ChecksStaticAddressStateSpaceAvailability) {
  static constexpr AddressStateSpaceDescriptor allowed_state_spaces[] = {{
      .state_space = MemoryStateSpace::Constant,
      .availability =
          {
              .minimum_ptx_version = {3, 1},
              .minimum_sm_version = 30,
              .required_family = "sm_test",
          },
  }};
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .allowed_address_state_spaces = allowed_state_spaces,
  }};
  constexpr OperandView operands[] = {{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Constant,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  }};
  const Context old_context{
      .target = {.ptx_version = {3, 0}, .sm_version = 20},
      .instruction_range = {{1, 1}, {1, 8}},
  };

  const auto rejected =
      check_operands(descriptors, {}, operands, {}, old_context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 3u);
  EXPECT_EQ(rejected.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(rejected.error()[2].kind,
            CheckDiagnosticKind::UnsupportedTargetFamily);
  EXPECT_EQ(rejected.error().front().range, kInstructionRange);

  constexpr std::array<std::string_view, 1> families{"sm_test"};
  auto supported_context = old_context;
  supported_context.target.ptx_version = {3, 1};
  supported_context.target.sm_version = 30;
  supported_context.target.enabled_family_features = families;
  EXPECT_TRUE(check_operands(descriptors, {}, operands, {}, supported_context)
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksInputParameterDirectionAndFunctionAvailability) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .state_space_modifier_field_id = "state_space",
      .parameter_constraint =
          {
              .direction = ParameterDirection::Input,
              .function_availability =
                  {
                      .minimum_ptx_version = {2, 0},
                      .minimum_sm_version = 20,
                  },
          },
  }};
  constexpr FieldView fields[] = {{
      .field_id = "state_space",
      .memory_state_space = MemoryStateSpace::Parameter,
  }};
  const Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
      .instruction_range = {{1, 1}, {1, 8}},
  };
  const auto check_operand = [&](const OperandView& operand,
                                 const Context& context) {
    return check_operands(descriptors, fields,
                          std::span<const OperandView>{&operand, 1}, {},
                          context);
  };

  OperandView input{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Parameter,
      .enclosing_function_kind = EnclosingFunctionKind::Entry,
      .parameter_direction = ParameterDirection::Input,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  EXPECT_TRUE(check_operand(input, old_context).has_value());
  input.enclosing_function_kind = EnclosingFunctionKind::Unknown;
  EXPECT_TRUE(check_operand(input, old_context).has_value());

  input.enclosing_function_kind = EnclosingFunctionKind::Device;
  const auto device_rejected = check_operand(input, old_context);
  ASSERT_FALSE(device_rejected.has_value());
  ASSERT_EQ(device_rejected.error().size(), 2u);
  EXPECT_EQ(device_rejected.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(device_rejected.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  auto supported_context = old_context;
  supported_context.target = {.ptx_version = {2, 0}, .sm_version = 20};
  EXPECT_TRUE(check_operand(input, supported_context).has_value());

  input.parameter_direction = ParameterDirection::Return;
  const auto wrong_direction = check_operand(input, old_context);
  ASSERT_FALSE(wrong_direction.has_value());
  ASSERT_EQ(wrong_direction.error().size(), 1u);
  EXPECT_EQ(wrong_direction.error().front().kind,
            CheckDiagnosticKind::ParameterDirectionMismatch);
  EXPECT_EQ(wrong_direction.error().front().range, kInstructionRange);

  input.address_state_space = MemoryStateSpace::Global;
  const auto wrong_space = check_operand(input, old_context);
  ASSERT_FALSE(wrong_space.has_value());
  ASSERT_EQ(wrong_space.error().size(), 1u);
  EXPECT_EQ(wrong_space.error().front().kind,
            CheckDiagnosticKind::AddressStateSpaceMismatch);
}

TEST(ResolvedIrChecker, ChecksReturnParameterAvailabilityWithoutFunctionKind) {
  constexpr OperandDescriptor descriptors[] = {{
      .target_field_id = "address",
      .role = OperandRole::Address,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Address,
      .state_space_modifier_field_id = "state_space",
      .parameter_constraint =
          {
              .direction = ParameterDirection::Return,
              .function_availability =
                  {
                      .minimum_ptx_version = {2, 0},
                      .minimum_sm_version = 20,
                  },
          },
  }};
  constexpr FieldView fields[] = {{
      .field_id = "state_space",
      .memory_state_space = MemoryStateSpace::Parameter,
  }};
  const Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
      .instruction_range = {{1, 1}, {1, 8}},
  };

  OperandView operand{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .enclosing_function_kind = EnclosingFunctionKind::Unknown,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const auto check_operand = [&](const OperandView& actual) {
    return check_operands(descriptors, fields,
                          std::span<const OperandView>{&actual, 1}, {},
                          old_context);
  };

  const auto unknown_rejected = check_operand(operand);
  ASSERT_FALSE(unknown_rejected.has_value());
  ASSERT_EQ(unknown_rejected.error().size(), 2u);
  EXPECT_EQ(unknown_rejected.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unknown_rejected.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  operand.address_state_space = MemoryStateSpace::Parameter;
  operand.parameter_direction = ParameterDirection::Input;
  const auto wrong_direction = check_operand(operand);
  ASSERT_FALSE(wrong_direction.has_value());
  ASSERT_EQ(wrong_direction.error().size(), 1u);
  EXPECT_EQ(wrong_direction.error().front().kind,
            CheckDiagnosticKind::ParameterDirectionMismatch);
}


TEST(ResolvedIrChecker, ChecksGeneratedMemoryConsistencyCrossRules) {
  static constexpr std::array kMmioSemantics{
      VariantDescriptor::MmioSemanticDescriptor{
          .semantics = MemoryConsistency::Relaxed,
      },
  };
  constexpr VariantDescriptor::MemoryConsistencyDescriptor descriptor{
      .semantics_field_id = "semantics",
      .scope_field_id = "scope",
      .mmio_field_id = "mmio",
      .cache_field_id = "cache",
      .address_field_id = "address",
      .mmio_semantics = kMmioSemantics,
  };
  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 90},
      .instruction_range = kInstructionRange,
  };
  const FieldView invalid_fields[] = {
      {.field_id = "semantics",
       .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::None},
      {.field_id = "mmio", .bool_value = false},
      {.field_id = "cache", .cache_operator = CacheOperator::Unspecified},
  };
  const OperandView global_address[] = {{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_state_space = MemoryStateSpace::Global,
  }};
  const auto missing_scope = check_memory_consistency(
      descriptor, invalid_fields, global_address, context);
  ASSERT_FALSE(missing_scope.has_value());
  EXPECT_EQ(missing_scope.error().front().kind,
            CheckDiagnosticKind::MemoryConsistencyViolation);

  const FieldView valid_fields[] = {
      {.field_id = "semantics",
       .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::Sys},
      {.field_id = "mmio", .bool_value = false},
      {.field_id = "cache", .cache_operator = CacheOperator::Unspecified},
  };
  EXPECT_TRUE(check_memory_consistency(descriptor, valid_fields, global_address,
                                       context)
                  .has_value());
  const FieldView missing_cache_fields[] = {
      {.field_id = "semantics",
       .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::Sys},
      {.field_id = "mmio", .bool_value = false},
  };
  const auto missing_cache = check_memory_consistency(
      descriptor, missing_cache_fields, global_address, context);
  ASSERT_FALSE(missing_cache.has_value());
  EXPECT_EQ(missing_cache.error().front().kind,
            CheckDiagnosticKind::RuleViolation);

  const FieldView missing_mmio_fields[] = {
      {.field_id = "semantics",
       .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::Sys},
      {.field_id = "cache", .cache_operator = CacheOperator::Unspecified},
  };
  const auto missing_mmio = check_memory_consistency(
      descriptor, missing_mmio_fields, global_address, context);
  ASSERT_FALSE(missing_mmio.has_value());
  EXPECT_EQ(missing_mmio.error().front().kind,
            CheckDiagnosticKind::RuleViolation);

  constexpr VariantDescriptor::MemoryConsistencyDescriptor vector_descriptor{
      .semantics_field_id = "semantics",
      .scope_field_id = "scope",
      .cache_field_id = "cache",
      .address_field_id = "address",
  };
  const FieldView vector_fields[] = {
      {.field_id = "semantics",
       .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::Cta},
      {.field_id = "cache", .cache_operator = CacheOperator::Unspecified},
  };
  EXPECT_TRUE(check_memory_consistency(vector_descriptor, vector_fields,
                                       global_address, context)
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksStaticAddressAlignment) {
  constexpr std::array<std::string_view, 1> kAddressFieldIds{{"address"}};
  const AddressAlignmentConstraint scalar_descriptor{
      .address_field_ids = kAddressFieldIds,
      .type_field_id = "type",
  };
  const FieldView scalar_fields[] = {{
      .field_id = "type",
      .scalar_type = ScalarType::U32,
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView address{
      .field_id = "address",
      .actual_shape = OperandShape::Address,
      .address_alignment = 4,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  EXPECT_TRUE(check_address_alignment(scalar_descriptor, scalar_fields,
                                      std::span{&address, 1}, context)
                  .has_value());

  address.address_alignment = 2;
  const auto scalar_mismatch = check_address_alignment(
      scalar_descriptor, scalar_fields, std::span{&address, 1}, context);
  ASSERT_FALSE(scalar_mismatch.has_value());
  EXPECT_EQ(scalar_mismatch.error().front().kind,
            CheckDiagnosticKind::AddressAlignmentMismatch);
  EXPECT_EQ(scalar_mismatch.error().front().range, kInstructionRange);

  const AddressAlignmentConstraint vector_descriptor{
      .address_field_ids = kAddressFieldIds,
      .type_field_id = "type",
      .vector_field_id = "vector",
  };
  const FieldView vector_fields[] = {
      {.field_id = "type", .scalar_type = ScalarType::U32},
      {.field_id = "vector", .vector_arity = VectorArity::V4},
  };
  address.address_alignment = 8;
  const auto vector_mismatch = check_address_alignment(
      vector_descriptor, vector_fields, std::span{&address, 1}, context);
  ASSERT_FALSE(vector_mismatch.has_value());
  EXPECT_EQ(vector_mismatch.error().front().kind,
            CheckDiagnosticKind::AddressAlignmentMismatch);

  const AddressAlignmentConstraint invalid_descriptor{
      .address_field_ids = kAddressFieldIds,
      .type_field_id = "missing_type",
  };
  const auto invalid = check_address_alignment(
      invalid_descriptor, scalar_fields, std::span{&address, 1}, context);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind, CheckDiagnosticKind::RuleViolation);

  const AddressAlignmentConstraint missing_vector_descriptor{
      .address_field_ids = kAddressFieldIds,
      .type_field_id = "type",
      .vector_field_id = "missing_vector",
  };
  const auto missing_vector =
      check_address_alignment(missing_vector_descriptor, scalar_fields,
                              std::span{&address, 1}, context);
  ASSERT_FALSE(missing_vector.has_value());
  EXPECT_EQ(missing_vector.error().front().kind,
            CheckDiagnosticKind::RuleViolation);

  constexpr std::array<std::string_view, 2> kCopyAddressFieldIds{
      {"dst", "src"}};
  const AddressAlignmentConstraint dynamic_descriptor{
      .address_field_ids = kCopyAddressFieldIds,
      .immediate_operand_field_id = "cp_size",
  };
  OperandView copy_operands[] = {
      {.field_id = "dst",
       .actual_shape = OperandShape::Address,
       .address_alignment = 8},
      {.field_id = "src",
       .actual_shape = OperandShape::Address,
       .address_alignment = 8},
      {.field_id = "cp_size",
       .actual_shape = OperandShape::Immediate,
       .immediate_bits = 8},
  };
  EXPECT_TRUE(
      check_address_alignment(dynamic_descriptor, {}, copy_operands, context)
          .has_value());
  copy_operands[2].integer_source_bits = 16;
  EXPECT_EQ(
      check_address_alignment(dynamic_descriptor, {}, copy_operands, context)
          .error()
          .front()
          .kind,
      CheckDiagnosticKind::AddressAlignmentMismatch);
  copy_operands[2].integer_source_bits.reset();
  copy_operands[0].address_alignment = 4;
  EXPECT_EQ(
      check_address_alignment(dynamic_descriptor, {}, copy_operands, context)
          .error()
          .front()
          .kind,
      CheckDiagnosticKind::AddressAlignmentMismatch);

  const AddressAlignmentConstraint fixed_descriptor{
      .address_field_ids = kAddressFieldIds,
      .alignment = 16,
  };
  address.address_alignment = 8;
  EXPECT_EQ(check_address_alignment(fixed_descriptor, {},
                                    std::span{&address, 1}, context)
                .error()
                .front()
                .kind,
            CheckDiagnosticKind::AddressAlignmentMismatch);
}

TEST(ResolvedIrChecker, ChecksGeneratedModernMemoryVectorCrossRules) {
  constexpr VariantDescriptor::MemoryVectorDescriptor descriptor{
      .type_field_id = "type",
      .vector_field_id = "vector",
      .address_field_id = "address",
      .availability = {.minimum_ptx_version = {8, 8},
                       .minimum_sm_version = 100},
  };
  const FieldView fields[] = {
      {.field_id = "type", .scalar_type = ScalarType::U32}};
  OperandView operands[] = {
      {.field_id = "vector",
       .actual_shape = OperandShape::Vector,
       .vector_arity = 8,
       .vector_sink_count = 1},
      {.field_id = "address", .actual_shape = OperandShape::Address},
  };
  const Context supported{
      .target = {.ptx_version = {8, 8}, .sm_version = 100},
      .instruction_range = kInstructionRange,
  };
  EXPECT_TRUE(
      check_memory_vector(descriptor, fields, operands, supported).has_value());

  auto old_ptx = supported;
  old_ptx.target.ptx_version = {8, 7};
  const auto ptx_rejected =
      check_memory_vector(descriptor, fields, operands, old_ptx);
  ASSERT_FALSE(ptx_rejected.has_value());
  EXPECT_EQ(ptx_rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  auto old_sm = supported;
  old_sm.target.sm_version = 90;
  const auto sm_rejected =
      check_memory_vector(descriptor, fields, operands, old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  operands[1].address_state_space = MemoryStateSpace::Shared;
  const auto non_global =
      check_memory_vector(descriptor, fields, operands, supported);
  ASSERT_FALSE(non_global.has_value());
  EXPECT_EQ(non_global.error().front().kind,
            CheckDiagnosticKind::RuleViolation);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
