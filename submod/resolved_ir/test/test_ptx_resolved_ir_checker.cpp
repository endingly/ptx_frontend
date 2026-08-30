#include <gtest/gtest.h>

#include <array>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::checker {
namespace {

const SourceRange kInstructionRange{{4, 3}, {4, 17}};

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

TEST(ResolvedIrChecker, UsesCatalogEnabledFamilyFeaturesForProductionAvailability) {
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

TEST(ResolvedIrChecker, KeepsFamilyFeatureRequirementsDistinctFromExactTargets) {
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

TEST(ResolvedIrChecker, ChecksGeneratedBareRetAvailability) {
  PtxSyntaxParser parser("ret;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto ret = resolve<Ret>(*ast);
  ASSERT_TRUE(ret.has_value()) << ret.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*ret, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*ret, supported_target).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedBareExitAvailability) {
  PtxSyntaxParser parser("exit;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto exit_instruction = resolve<Exit>(*ast);
  ASSERT_TRUE(exit_instruction.has_value()) << exit_instruction.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*exit_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*exit_instruction, supported_target).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedBareTrapAvailability) {
  PtxSyntaxParser parser("trap;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto trap = resolve<Trap>(*ast);
  ASSERT_TRUE(trap.has_value()) << trap.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*trap, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*trap, supported_target).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedAndB32Availability) {
  PtxSyntaxParser parser("and.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto and_instruction = resolve<And>(*ast);
  ASSERT_TRUE(and_instruction.has_value()) << and_instruction.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*and_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*and_instruction, supported_target).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedOrB32Availability) {
  PtxSyntaxParser parser("or.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto or_instruction = resolve<Or>(*ast);
  ASSERT_TRUE(or_instruction.has_value()) << or_instruction.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*or_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*or_instruction, supported_target).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedXorB32Availability) {
  PtxSyntaxParser parser("xor.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto xor_instruction = resolve<Xor>(*ast);
  ASSERT_TRUE(xor_instruction.has_value()) << xor_instruction.error().message;
  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*xor_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);
  EXPECT_TRUE(check(*xor_instruction,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedNotB32Availability) {
  PtxSyntaxParser parser("not.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto not_instruction = resolve<Not>(*ast);
  ASSERT_TRUE(not_instruction.has_value()) << not_instruction.error().message;
  const Context old_target{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range};
  const auto unavailable = check(*not_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);
  EXPECT_TRUE(check(*not_instruction,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedShlB32Availability) {
  PtxSyntaxParser parser("shl.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto shl = resolve<Shl>(*ast);
  ASSERT_TRUE(shl.has_value()) << shl.error().message;
  const auto rejected = check(*shl, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0}, .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(check(*shl, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}, .instruction_range = ast->range}).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedShrU32Availability) {
  PtxSyntaxParser parser("shr.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto shr = resolve<Shr>(*ast);
  ASSERT_TRUE(shr.has_value()) << shr.error().message;
  const auto rejected = check(*shr, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0}, .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(check(*shr, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}, .instruction_range = ast->range}).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedSetAvailability) {
  for (const auto source : {
           "set.eq.u32.u32 %r0, %r1, %r2;",
           "set.lt.and.f32.s32 %f0, %s0, %s1, !%p0;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto set = resolve<Set>(*ast);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    const auto rejected = check(
        *set, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                      .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(
                    *set,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                             .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedSetpLtU32Availability) {
  PtxSyntaxParser parser("setp.lt.u32 %p0, %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto setp = resolve<Setp>(*ast);
  ASSERT_TRUE(setp.has_value()) << setp.error().message;
  const auto rejected = check(
      *setp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                     .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(check(*setp,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedSetpDualPredicateAvailability) {
  for (const auto source : {
           "setp.eq.u32 %p0|%p1, %r0, %r1;",
           "setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto setp = resolve<Setp>(*ast);
    ASSERT_TRUE(setp.has_value()) << setp.error().message;
    const auto rejected = check(
        *setp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                        .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(*setp,
                      Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                              .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedSlctAvailability) {
  for (const auto source : {
           "slct.u32.s32 %r0, %r1, %r2, %r3;",
           "slct.ftz.u64.f32 %rd0, %rd1, %rd2, %f0;",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto slct = resolve<Slct>(*ast);
    ASSERT_TRUE(slct.has_value()) << slct.error().message;
    const auto rejected = check(
        *slct, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                       .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(*slct,
                      Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                              .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedSelpU32Availability) {
  PtxSyntaxParser parser("selp.u32 %r0, %r1, %r2, %p0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto selp = resolve<Selp>(*ast);
  ASSERT_TRUE(selp.has_value()) << selp.error().message;
  const auto rejected = check(
      *selp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                     .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*selp,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtaGlobalU64Availability) {
  PtxSyntaxParser parser("cvta.to.global.u64 %rd0, %rd1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvta = resolve<Cvta>(*ast);
  ASSERT_TRUE(cvta.has_value()) << cvta.error().message;
  const auto old_ptx = check(
      *cvta, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *cvta, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*cvta,
                    Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulLoU32Availability) {
  PtxSyntaxParser parser("mul.lo.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected = check(
      *mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mul,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulHiU32Availability) {
  PtxSyntaxParser parser("mul.hi.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected = check(
      *mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mul,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulWideU32Availability) {
  PtxSyntaxParser parser("mul.wide.u32 %rd0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected = check(
      *mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mul,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulWideS32Availability) {
  PtxSyntaxParser parser("mul.wide.s32 %rd0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected = check(
      *mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mul,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMulRnF32Availability) {
  PtxSyntaxParser parser("mul.rn.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mul = resolve<Mul>(*ast);
  ASSERT_TRUE(mul.has_value()) << mul.error().message;
  const auto rejected = check(
      *mul, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mul,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMadLoU32Availability) {
  PtxSyntaxParser parser("mad.lo.u32 %r0, %r1, %r2, %r3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mad = resolve<Mad>(*ast);
  ASSERT_TRUE(mad.has_value()) << mad.error().message;
  const auto rejected = check(
      *mad, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*mad,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMadLoS32AndWideU32Availability) {
  for (const auto source : {"mad.lo.s32 %r0, %r1, %r2, %r3;",
                            "mad.wide.u32 %rd0, %r1, %r2, %rd3;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto mad = resolve<Mad>(*ast);
    ASSERT_TRUE(mad.has_value()) << mad.error().message;
    const auto rejected = check(
        *mad, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                      .instruction_range = ast->range});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(*mad,
                      Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                              .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedMadRnF32Availability) {
  PtxSyntaxParser parser("mad.rn.f32 %f0, %f1, %f2, %f3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto mad = resolve<Mad>(*ast);
  ASSERT_TRUE(mad.has_value()) << mad.error().message;
  const auto old_ptx = check(
      *mad, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *mad, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*mad,
                    Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedFmaRnF32Availability) {
  PtxSyntaxParser parser("fma.rn.f32 %f0, %f1, %f2, %f3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto fma = resolve<Fma>(*ast);
  ASSERT_TRUE(fma.has_value()) << fma.error().message;
  const auto old_ptx = check(
      *fma, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *fma, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*fma,
                    Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedFmaRnF64Availability) {
  PtxSyntaxParser parser("fma.rn.f64 %d0, %d1, %d2, %d3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto fma = resolve<Fma>(*ast);
  ASSERT_TRUE(fma.has_value()) << fma.error().message;
  const auto old_ptx = check(
      *fma, Context{.target = {.ptx_version = {1, 3}, .sm_version = 13},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *fma, Context{.target = {.ptx_version = {1, 4}, .sm_version = 12},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*fma,
                    Context{.target = {.ptx_version = {1, 4}, .sm_version = 13},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedFmaRnF16Availability) {
  PtxSyntaxParser parser("fma.rn.f16 %h0, %h1, %h2, %h3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto fma = resolve<Fma>(*ast);
  ASSERT_TRUE(fma.has_value()) << fma.error().message;
  const auto old_ptx = check(
      *fma, Context{.target = {.ptx_version = {4, 1}, .sm_version = 53},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *fma, Context{.target = {.ptx_version = {4, 2}, .sm_version = 52},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*fma,
                    Context{.target = {.ptx_version = {4, 2}, .sm_version = 53},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivU32Availability) {
  PtxSyntaxParser parser("div.u32 %r0, %r1, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto rejected = check(
      *div, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*div,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedRemAvailability) {
  PtxSyntaxParser parser("rem.s32 %r0, %r1, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto rem = resolve<Rem>(*ast);
  ASSERT_TRUE(rem.has_value()) << rem.error().message;
  const auto rejected = check(
      *rem, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*rem,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMinAvailability) {
  PtxSyntaxParser integer_parser("min.s32 %r0, %r1, %r2;");
  const auto integer_ast = integer_parser.parseInstruction();
  ASSERT_TRUE(integer_ast.has_value()) << integer_ast.diagnostics.front().message;
  const auto integer_min = resolve<Min>(*integer_ast);
  ASSERT_TRUE(integer_min.has_value()) << integer_min.error().message;
  const auto old_integer = check(
      *integer_min, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = integer_ast->range});
  ASSERT_FALSE(old_integer.has_value());
  EXPECT_EQ(old_integer.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*integer_min,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = integer_ast->range})
                  .has_value());

  PtxSyntaxParser nan_parser("min.NaN.f32 %f0, %f1, %f2;");
  const auto nan_ast = nan_parser.parseInstruction();
  ASSERT_TRUE(nan_ast.has_value()) << nan_ast.diagnostics.front().message;
  const auto nan_min = resolve<Min>(*nan_ast);
  ASSERT_TRUE(nan_min.has_value()) << nan_min.error().message;
  const auto old_ptx = check(
      *nan_min, Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *nan_min, Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*nan_min,
                    Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                            .instruction_range = nan_ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMaxAvailability) {
  PtxSyntaxParser integer_parser("max.s32 %r0, %r1, %r2;");
  const auto integer_ast = integer_parser.parseInstruction();
  ASSERT_TRUE(integer_ast.has_value()) << integer_ast.diagnostics.front().message;
  const auto integer_max = resolve<Max>(*integer_ast);
  ASSERT_TRUE(integer_max.has_value()) << integer_max.error().message;
  const auto old_integer = check(
      *integer_max, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = integer_ast->range});
  ASSERT_FALSE(old_integer.has_value());
  EXPECT_EQ(old_integer.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*integer_max,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = integer_ast->range})
                  .has_value());

  PtxSyntaxParser nan_parser("max.NaN.f32 %f0, %f1, %f2;");
  const auto nan_ast = nan_parser.parseInstruction();
  ASSERT_TRUE(nan_ast.has_value()) << nan_ast.diagnostics.front().message;
  const auto nan_max = resolve<Max>(*nan_ast);
  ASSERT_TRUE(nan_max.has_value()) << nan_max.error().message;
  const auto old_ptx = check(
      *nan_max, Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *nan_max, Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                        .instruction_range = nan_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*nan_max,
                    Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                            .instruction_range = nan_ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedAbsAvailability) {
  for (const auto source : {"abs.s32 %r0, %r1;", "abs.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto abs = resolve<Abs>(*ast);
    ASSERT_TRUE(abs.has_value()) << abs.error().message;
    const auto old_ptx = check(
        *abs, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(*abs,
                      Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                              .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedNegAvailability) {
  for (const auto source : {"neg.s32 %r0, %r1;", "neg.f32 %f0, %f1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto neg = resolve<Neg>(*ast);
    ASSERT_TRUE(neg.has_value()) << neg.error().message;
    const auto old_ptx = check(
        *neg, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_TRUE(check(*neg,
                      Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                              .instruction_range = ast->range})
                    .has_value());
  }

  PtxSyntaxParser packed_parser("neg.f16x2 %r0, %r1;");
  const auto packed_ast = packed_parser.parseInstruction();
  ASSERT_TRUE(packed_ast.has_value()) << packed_ast.diagnostics.front().message;
  const auto packed = resolve<Neg>(*packed_ast);
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  const auto old_ptx = check(
      *packed, Context{.target = {.ptx_version = {5, 9}, .sm_version = 53},
                       .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *packed, Context{.target = {.ptx_version = {6, 0}, .sm_version = 52},
                       .instruction_range = packed_ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*packed,
                    Context{.target = {.ptx_version = {6, 0}, .sm_version = 53},
                            .instruction_range = packed_ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedLop3AvailabilityAndLutRange) {
  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 0;",
                            "lop3.b32 %r0, %r1, %r2, %r3, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto lop3 = resolve<Lop3>(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto old_ptx = check(
        *lop3, Context{.target = {.ptx_version = {4, 2}, .sm_version = 50},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *lop3, Context{.target = {.ptx_version = {4, 3}, .sm_version = 49},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*lop3,
                      Context{.target = {.ptx_version = {4, 3}, .sm_version = 50},
                              .instruction_range = ast->range})
                    .has_value());
  }

  for (const auto source : {"lop3.b32 %r0, %r1, %r2, %r3, 256;",
                            "lop3.b32 %r0, %r1, %r2, %r3, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto lop3 = resolve<Lop3>(*ast);
    ASSERT_TRUE(lop3.has_value()) << lop3.error().message;
    const auto checked = check(
        *lop3, Context{.target = {.ptx_version = {4, 3}, .sm_version = 50},
                       .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedShfAvailability) {
  for (const auto source : {"shf.l.clamp.b32 %r0, %r1, %r2, 8;",
                            "shf.r.wrap.b32 %r0, %r1, %r2, %r3;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto shf = resolve<Shf>(*ast);
    ASSERT_TRUE(shf.has_value()) << shf.error().message;
    const auto old_ptx = check(
        *shf, Context{.target = {.ptx_version = {3, 0}, .sm_version = 32},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *shf, Context{.target = {.ptx_version = {3, 1}, .sm_version = 31},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*shf,
                      Context{.target = {.ptx_version = {3, 1}, .sm_version = 32},
                              .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedPrmtAvailabilityAndSelectorRange) {
  for (const auto source : {"prmt.b32 %r0, %r1, %r2, 0;", "prmt.b32 %r0, %r1, %r2, 65535;"}) {
    PtxSyntaxParser parser(source); const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()); const auto prmt = resolve<Prmt>(*ast); ASSERT_TRUE(prmt.has_value());
    EXPECT_TRUE(check(*prmt, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}}).has_value());
    EXPECT_FALSE(check(*prmt, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20}}).has_value());
    EXPECT_FALSE(check(*prmt, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19}}).has_value());
  }
  for (const auto source : {"prmt.b32 %r0, %r1, %r2, 65536;", "prmt.b32 %r0, %r1, %r2, -1;"}) {
    PtxSyntaxParser parser(source); const auto ast = parser.parseInstruction(); ASSERT_TRUE(ast.has_value());
    const auto prmt = resolve<Prmt>(*ast); ASSERT_TRUE(prmt.has_value());
    EXPECT_FALSE(check(*prmt, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}}).has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedPopcAvailability) {
  PtxSyntaxParser parser("popc.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto popc = resolve<Popc>(*ast);
  ASSERT_TRUE(popc.has_value()) << popc.error().message;
  const auto old_ptx = check(
      *popc, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *popc, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*popc, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                                   .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedClzAvailability) {
  for (const auto source : {"clz.b32 %r0, %r1;", "clz.b64 %r0, %rd1;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto clz = resolve<Clz>(*ast);
    ASSERT_TRUE(clz.has_value()) << clz.error().message;
    const auto old_ptx = check(
        *clz, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *clz, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                       .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*clz, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                                    .instruction_range = ast->range})
                    .has_value());
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedBfindAvailability) {
  PtxSyntaxParser parser("bfind.shiftamt.u32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto bfind = resolve<Bfind>(*ast);
  ASSERT_TRUE(bfind.has_value()) << bfind.error().message;
  const auto old_ptx = check(
      *bfind, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                      .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *bfind, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                      .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*bfind, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                                     .instruction_range = ast->range})
                  .has_value());
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

TEST(ResolvedIrChecker, ChecksGeneratedBfeAvailabilityAndImmediateRanges) {
  for (const auto source : {"bfe.u32 %r0, %r1, 0, 8;",
                            "bfe.u32 %r0, %r1, 255, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfe = resolve<Bfe>(*ast);
    ASSERT_TRUE(bfe.has_value()) << bfe.error().message;
    const auto old_ptx = check(
        *bfe, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                             .instruction_range = ast->range})
                    .has_value());
  }

  for (const auto source : {"bfe.u32 %r0, %r1, 256, 8;",
                            "bfe.u32 %r0, %r1, 8, 256;",
                            "bfe.u32 %r0, %r1, -1, 8;",
                            "bfe.u32 %r0, %r1, 8, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfe = resolve<Bfe>(*ast);
    ASSERT_TRUE(bfe.has_value()) << bfe.error().message;
    const auto checked = check(
        *bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                      .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedBfiAvailabilityAndImmediateRanges) {
  for (const auto source : {"bfi.b32 %r0, %r1, %r2, 0, 8;",
                            "bfi.b32 %r0, %r1, %r2, 255, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfi = resolve<Bfi>(*ast);
    ASSERT_TRUE(bfi.has_value()) << bfi.error().message;
    const auto old_ptx = check(
        *bfi, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = check(
        *bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                      .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(check(*bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                                     .instruction_range = ast->range})
                    .has_value());
  }
  for (const auto source : {"bfi.b32 %r0, %r1, %r2, 256, 8;",
                            "bfi.b32 %r0, %r1, %r2, 8, 256;",
                            "bfi.b32 %r0, %r1, %r2, -1, 8;",
                            "bfi.b32 %r0, %r1, %r2, 8, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfi = resolve<Bfi>(*ast);
    ASSERT_TRUE(bfi.has_value()) << bfi.error().message;
    const auto checked = check(
        *bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                      .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

TEST(ResolvedIrChecker, ChecksGeneratedBrevAvailability) {
  PtxSyntaxParser parser("brev.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto brev = resolve<Brev>(*ast);
  ASSERT_TRUE(brev.has_value()) << brev.error().message;
  const auto old_ptx = check(
      *brev, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind, CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *brev, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                     .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind, CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*brev, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                                    .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF32Availability) {
  PtxSyntaxParser parser("div.rn.f32 %f0, %f1, %f2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx = check(
      *div, Context{.target = {.ptx_version = {1, 3}, .sm_version = 20},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 19},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*div,
                    Context{.target = {.ptx_version = {1, 4}, .sm_version = 20},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedDivRnF64Availability) {
  PtxSyntaxParser parser("div.rn.f64 %d0, %d1, %d2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto div = resolve<Div>(*ast);
  ASSERT_TRUE(div.has_value()) << div.error().message;
  const auto old_ptx = check(
      *div, Context{.target = {.ptx_version = {1, 3}, .sm_version = 13},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = check(
      *div, Context{.target = {.ptx_version = {1, 4}, .sm_version = 12},
                    .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*div,
                    Context{.target = {.ptx_version = {1, 4}, .sm_version = 13},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtS32U32Availability) {
  PtxSyntaxParser parser("cvt.s32.u32 %s0, %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected = check(
      *cvt, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*cvt,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtRnF32F64Availability) {
  PtxSyntaxParser parser("cvt.rn.f32.f64 %f0, %fd0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected = check(
      *cvt, Context{.target = {.ptx_version = {1, 0}, .sm_version = 12},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(check(*cvt,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 13},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedIsspacepGlobalU64Availability) {
  PtxSyntaxParser parser("isspacep.global %p0, %rd0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto isspacep = resolve<Isspacep>(*ast);
  ASSERT_TRUE(isspacep.has_value()) << isspacep.error().message;
  EXPECT_FALSE(check(*isspacep,
                     Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                             .instruction_range = ast->range})
                   .has_value());
  EXPECT_FALSE(check(*isspacep,
                     Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                             .instruction_range = ast->range})
                   .has_value());
  EXPECT_TRUE(check(*isspacep,
                    Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedCvtRziU32F32Availability) {
  PtxSyntaxParser parser("cvt.rzi.u32.f32 %r0, %f0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  const auto rejected = check(
      *cvt, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                    .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(check(*cvt,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedM12CvtAvailability) {
  PtxSyntaxParser scalar_parser("cvt.rn.f32.s32 %f0, %r0;");
  const auto scalar_ast = scalar_parser.parseInstruction();
  ASSERT_TRUE(scalar_ast.has_value()) << scalar_ast.diagnostics.front().message;
  const auto scalar = resolve<Cvt>(*scalar_ast);
  ASSERT_TRUE(scalar.has_value()) << scalar.error().message;
  EXPECT_FALSE(check(*scalar,
                     Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                             .instruction_range = scalar_ast->range})
                   .has_value());
  EXPECT_TRUE(check(*scalar,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = scalar_ast->range})
                  .has_value());

  PtxSyntaxParser packed_parser("cvt.rn.f16x2.f32 %r0, %f0, %f1;");
  const auto packed_ast = packed_parser.parseInstruction();
  ASSERT_TRUE(packed_ast.has_value()) << packed_ast.diagnostics.front().message;
  const auto packed = resolve<Cvt>(*packed_ast);
  ASSERT_TRUE(packed.has_value()) << packed.error().message;
  EXPECT_FALSE(check(*packed,
                     Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                             .instruction_range = packed_ast->range})
                   .has_value());
  EXPECT_FALSE(check(*packed,
                     Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                             .instruction_range = packed_ast->range})
                   .has_value());
  EXPECT_TRUE(check(*packed,
                    Context{.target = {.ptx_version = {7, 0}, .sm_version = 80},
                            .instruction_range = packed_ast->range})
                  .has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedM12CvtPackAvailability) {
  PtxSyntaxParser parser("cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2, %r3;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvt = resolve<Cvt>(*ast);
  ASSERT_TRUE(cvt.has_value()) << cvt.error().message;
  EXPECT_FALSE(check(*cvt,
                     Context{.target = {.ptx_version = {6, 4}, .sm_version = 72},
                             .instruction_range = ast->range})
                   .has_value());
  EXPECT_FALSE(check(*cvt,
                     Context{.target = {.ptx_version = {6, 5}, .sm_version = 71},
                             .instruction_range = ast->range})
                   .has_value());
  EXPECT_TRUE(check(*cvt,
                    Context{.target = {.ptx_version = {6, 5}, .sm_version = 72},
                            .instruction_range = ast->range})
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
    return check_operands(
        descriptors, {}, std::span<const OperandView>{&operand, 1}, {},
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
  auto rejected = check_operands(
      descriptors, fields, std::span<const OperandView>{&operand, 1}, {},
      context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  operand.vector_arity = 2;
  operand.vector_sink_count = 1;
  rejected = check_operands(descriptors, fields,
                            std::span<const OperandView>{&operand, 1}, {},
                            context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::InvalidVectorOperand);

  operand.vector_sink_count = 0;
  operand.vector_element_types = {ScalarType::U8, ScalarType::U32};
  rejected = check_operands(descriptors, fields,
                            std::span<const OperandView>{&operand, 1}, {},
                            context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);

  operand.vector_element_types = {ScalarType::F32, ScalarType::U32};
  rejected = check_operands(descriptors, fields,
                            std::span<const OperandView>{&operand, 1}, {},
                            context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::OperandTypeMismatch);

  operand.vector_element_types = {ScalarType::U16, ScalarType::U32};
  rejected = check_operands(descriptors, {}, std::span<const OperandView>{&operand, 1},
                            {}, context);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::MissingVectorArityField);
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
      .allowed_element_shapes = OperandShape::Register | OperandShape::Immediate,
  }};
  const Context context{.target = {}, .instruction_range = kInstructionRange};
  OperandView coordinate{
      .field_id = "coordinate",
      .actual_shape = OperandShape::Vector,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const auto check_tensor = [&] {
    return check_operands(tensor, {}, std::span<const OperandView>{&coordinate, 1},
                          {}, context);
  };

  auto rejected = check_tensor();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind, CheckDiagnosticKind::InvalidVectorOperand);

  coordinate.vector_arity = 6;
  rejected = check_tensor();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind, CheckDiagnosticKind::InvalidVectorOperand);

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
    return check_operands(matrix, {}, std::span<const OperandView>{&fragment, 1},
                          {}, context);
  };

  fragment.vector_arity = 1;
  fragment.vector_element_shapes.fill(OperandShape::Register);
  EXPECT_TRUE(check_matrix().has_value());

  fragment.vector_arity = 64;
  EXPECT_TRUE(check_matrix().has_value());

  fragment.vector_arity = 65;
  auto rejected = check_matrix();
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind, CheckDiagnosticKind::InvalidVectorOperand);

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
      .vector_element_types = {ScalarType::U64, ScalarType::U64, ScalarType::U64,
                               ScalarType::U64, ScalarType::U64, ScalarType::U64,
                               ScalarType::U64, ScalarType::U64},
      .vector_arity = 8,
      .locations = std::span<const SourceRange>{&kInstructionRange, 1},
  };
  const Context context{.target = {}, .instruction_range = kInstructionRange};

  const auto rejected = check_operands(
      descriptors, {}, std::span<const OperandView>{&operand, 1}, {},
      context);

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
      .availability = {
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
      .parameter_constraint = {
          .direction = ParameterDirection::Input,
          .function_availability = {
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
    return check_operands(
        descriptors, fields, std::span<const OperandView>{&operand, 1}, {},
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
      .parameter_constraint = {
          .direction = ParameterDirection::Return,
          .function_availability = {
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
    return check_operands(
        descriptors, fields, std::span<const OperandView>{&actual, 1}, {},
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

TEST(ResolvedIrChecker, GeneratedSubWrapperUsesValueAvailability) {
  PtxSyntaxParser parser("sub.sat.u8x4 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolve<Sub>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Sub::OptionalSat>(&resolved->variant), nullptr);

  constexpr std::array<std::string_view, 1> family{"sm_120f"};
  const Context unsupported_context{
      .target = {.ptx_version = {9, 1}, .sm_version = 100,
                 .enabled_family_features = family},
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
      .target = {.ptx_version = {9, 2}, .sm_version = 120,
                 .enabled_family_features = family},
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

TEST(ResolvedIrChecker, GeneratedBarWrapperRejectsMismatchedLayoutPayload) {
  PtxSyntaxParser parser("bar.sync 1, 128;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  auto resolved = resolve<Bar>(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto* bar = std::get_if<Bar::Sync>(&resolved->variant);
  ASSERT_NE(bar, nullptr);
  ASSERT_TRUE(std::holds_alternative<Bar::Sync::BarrierAndThreadCountOperands>(
      bar->operands));
  EXPECT_EQ(bar->operand_layout, (ResolvedOperandLayoutTag{2}));

  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 120},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*resolved, context).has_value());

  bar->operand_layout = ResolvedOperandLayoutTag{0};
  const auto result = check(*resolved, context);

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1U);
  EXPECT_EQ(result.error().front().kind,
            CheckDiagnosticKind::OperandLayoutPayloadMismatch);
  EXPECT_EQ(result.error().front().range, ast->range);
}

TEST(ResolvedIrChecker, GeneratedBarWrapperChecksLayoutAvailability) {
  PtxSyntaxParser immediate_parser("bar.sync 1;");
  const auto immediate_ast = immediate_parser.parseInstruction();
  ASSERT_TRUE(immediate_ast.has_value())
      << immediate_ast.diagnostics.front().message;
  auto immediate = resolve<Bar>(*immediate_ast);
  ASSERT_TRUE(immediate.has_value()) << immediate.error().message;

  const Context sm10_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
      .instruction_range = immediate_ast->range,
  };
  EXPECT_TRUE(check(*immediate, sm10_context).has_value());

  PtxSyntaxParser register_parser("bar.sync %r1;");
  const auto register_ast = register_parser.parseInstruction();
  ASSERT_TRUE(register_ast.has_value())
      << register_ast.diagnostics.front().message;
  auto register_barrier = resolve<Bar>(*register_ast);
  ASSERT_TRUE(register_barrier.has_value()) << register_barrier.error().message;

  const auto unsupported = check(*register_barrier, sm10_context);
  ASSERT_FALSE(unsupported.has_value());
  ASSERT_EQ(unsupported.error().size(), 2U);
  EXPECT_EQ(unsupported.error()[0].kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unsupported.error()[1].kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  const Context sm20_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = register_ast->range,
  };
  EXPECT_TRUE(check(*register_barrier, sm20_context).has_value());
}

TEST(ResolvedIrChecker, ChecksGeneratedMemoryConsistencyCrossRules) {
  constexpr VariantDescriptor::MemoryConsistencyDescriptor descriptor{
      .semantics_field_id = "semantics",
      .scope_field_id = "scope",
      .mmio_field_id = "mmio",
      .cache_field_id = "cache",
      .address_field_id = "address",
  };
  const Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 90},
      .instruction_range = kInstructionRange,
  };
  const FieldView invalid_fields[] = {
      {.field_id = "semantics", .memory_consistency = MemoryConsistency::Relaxed},
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
      {.field_id = "semantics", .memory_consistency = MemoryConsistency::Relaxed},
      {.field_id = "scope", .memory_scope = MemoryScope::Sys},
      {.field_id = "mmio", .bool_value = true},
      {.field_id = "cache", .cache_operator = CacheOperator::Unspecified},
  };
  EXPECT_TRUE(check_memory_consistency(descriptor, valid_fields, global_address,
                                       context)
                  .has_value());

  constexpr VariantDescriptor::MemoryConsistencyDescriptor vector_descriptor{
      .semantics_field_id = "semantics",
      .scope_field_id = "scope",
      .cache_field_id = "cache",
      .address_field_id = "address",
  };
  const FieldView vector_fields[] = {
      {.field_id = "semantics", .memory_consistency = MemoryConsistency::Relaxed},
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
  const auto missing_vector = check_address_alignment(
      missing_vector_descriptor, scalar_fields, std::span{&address, 1}, context);
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
      .availability = {.minimum_ptx_version = {8, 8}, .minimum_sm_version = 100},
  };
  const FieldView fields[] = {{.field_id = "type", .scalar_type = ScalarType::U32}};
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
  EXPECT_TRUE(check_memory_vector(descriptor, fields, operands, supported)
                  .has_value());

  auto old_ptx = supported;
  old_ptx.target.ptx_version = {8, 7};
  const auto ptx_rejected = check_memory_vector(descriptor, fields, operands, old_ptx);
  ASSERT_FALSE(ptx_rejected.has_value());
  EXPECT_EQ(ptx_rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  auto old_sm = supported;
  old_sm.target.sm_version = 90;
  const auto sm_rejected = check_memory_vector(descriptor, fields, operands, old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);

  operands[1].address_state_space = MemoryStateSpace::Shared;
  const auto non_global = check_memory_vector(descriptor, fields, operands, supported);
  ASSERT_FALSE(non_global.has_value());
  EXPECT_EQ(non_global.error().front().kind, CheckDiagnosticKind::RuleViolation);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
