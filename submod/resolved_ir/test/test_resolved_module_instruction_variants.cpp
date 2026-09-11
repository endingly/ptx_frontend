#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov::Scalar& mov) {
  return std::get<Mov::Scalar::ScalarOperands>(mov.operands);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov& mov) {
  return scalarMovOperands(std::get<Mov::Scalar>(mov.variant));
}

const Mov::Scalar::PackOperands& packMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

Mov::Scalar::PackOperands& packMovOperands(Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

const Mov::Scalar::UnpackOperands& unpackMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::UnpackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

TEST(ResolvedModule, ResolvesAndChecksM12I05FrozenAddForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u64 %rd<3>;
  .reg .f32 %f<3>;
  add.u32 %r0, %r1, %r2;
  add.s32 %r0, %r1, %r2;
  add.u64 %rd0, %rd1, %rd2;
  add.f32 %f0, %f1, %f2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& u32 = std::get<Add>(body[0]);
  const auto& s32 = std::get<Add>(body[1]);
  const auto& u64 = std::get<Add>(body[2]);
  const auto& f32 = std::get<Add>(body[3]);
  const auto* u32_variant = std::get_if<Add::IntegerNoSat>(&u32.variant);
  const auto* s32_variant = std::get_if<Add::IntegerNoSat>(&s32.variant);
  const auto* u64_variant = std::get_if<Add::IntegerNoSat>(&u64.variant);
  const auto* f32_variant = std::get_if<Add::FloatF32>(&f32.variant);
  ASSERT_NE(u32_variant, nullptr);
  ASSERT_NE(s32_variant, nullptr);
  ASSERT_NE(u64_variant, nullptr);
  ASSERT_NE(f32_variant, nullptr);
  EXPECT_EQ(u32_variant->type.value, ScalarType::U32);
  EXPECT_EQ(s32_variant->type.value, ScalarType::S32);
  EXPECT_EQ(u64_variant->type.value, ScalarType::U64);
  EXPECT_EQ(Add::FloatF32::type, ScalarType::F32);

  for (const std::string_view target : {"sm_80", "sm_90a", "sm_100"}) {
    const auto profile = base::find_target_profile(target);
    ASSERT_TRUE(profile.has_value()) << target;
    const checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
    };
    for (const Add* add : {&u32, &s32, &u64, &f32})
      EXPECT_TRUE(checker::check(*add, context).has_value()) << target;
  }
}

TEST(ResolvedModule, ResolvesAndChecksM12I06FrozenSubForms) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u64 %rd<3>;
  .reg .f32 %f<3>;
  sub.u32 %r0, %r1, %r2;
  sub.s32 %r0, %r1, %r2;
  sub.u64 %rd0, %rd1, %rd2;
  sub.f32 %f0, %f1, %f2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& u32 = std::get<Sub>(body[0]);
  const auto& s32 = std::get<Sub>(body[1]);
  const auto& u64 = std::get<Sub>(body[2]);
  const auto& f32 = std::get<Sub>(body[3]);
  const auto* u32_variant = std::get_if<Sub::IntegerNoSat>(&u32.variant);
  const auto* s32_variant = std::get_if<Sub::OptionalSat>(&s32.variant);
  const auto* u64_variant = std::get_if<Sub::IntegerNoSat>(&u64.variant);
  const auto* f32_variant = std::get_if<Sub::FloatF32>(&f32.variant);
  ASSERT_NE(u32_variant, nullptr);
  ASSERT_NE(s32_variant, nullptr);
  ASSERT_NE(u64_variant, nullptr);
  ASSERT_NE(f32_variant, nullptr);
  EXPECT_EQ(u32_variant->type.value, ScalarType::U32);
  EXPECT_EQ(s32_variant->type.value, ScalarType::S32);
  EXPECT_EQ(u64_variant->type.value, ScalarType::U64);
  EXPECT_EQ(Sub::FloatF32::type, ScalarType::F32);

  for (const std::string_view target : {"sm_80", "sm_90a", "sm_100"}) {
    const auto profile = base::find_target_profile(target);
    ASSERT_TRUE(profile.has_value()) << target;
    const checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
    };
    for (const Sub* sub : {&u32, &s32, &u64, &f32})
      EXPECT_TRUE(checker::check(*sub, context).has_value()) << target;
  }
}

TEST(ResolvedModule, PreservesMixedPrecisionModifierOrderCompatibility) {
  constexpr std::array<std::string_view, 5> roundings{
      "", ".rn", ".rz", ".rm", ".rp"};
  constexpr std::array rounding_modes{
      RoundingMode::Rn, RoundingMode::Rn, RoundingMode::Rz,
      RoundingMode::Rm, RoundingMode::Rp};
  constexpr std::array input_types{"f16", "bf16"};
  constexpr std::array input_type_values{ScalarType::F16, ScalarType::BF16};
  constexpr std::array input_registers{"%h0", "%bf0"};

  std::string source = R"ptx(
.entry kernel() {
  .reg .f32 %f<2>;
  .reg .f16 %h0;
  .reg .b16 %bf0;
)ptx";
  const auto append_instruction = [&](std::string_view opcode,
                                      std::string_view rounding,
                                      std::string_view before_types,
                                      std::string_view input_type,
                                      std::string_view after_types,
                                      std::string_view input_register) {
    source += "  ";
    source += opcode;
    source += rounding;
    source += before_types;
    source += ".f32.";
    source += input_type;
    source += after_types;
    source += " %f0, ";
    source += input_register;
    source += ", %f1;\n";
  };
  const auto append_forms = [&](std::string_view opcode) {
    for (size_t type_index = 0; type_index != input_types.size(); ++type_index) {
      for (const std::string_view rounding : roundings) {
        append_instruction(opcode, rounding, "", input_types[type_index], "",
                           input_registers[type_index]);
        append_instruction(opcode, rounding, ".sat", input_types[type_index],
                           "", input_registers[type_index]);
        append_instruction(opcode, rounding, "", input_types[type_index], ".sat",
                           input_registers[type_index]);
      }
    }
  };
  append_forms("add");
  append_forms("sub");
  source += "}\n";

  const auto parsed_module_1 = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  constexpr size_t forms_per_opcode =
      input_types.size() * roundings.size() * 3U;
  ASSERT_EQ(body.size(), forms_per_opcode * 2U);
  const auto& syntax_function =
      std::get<syntax_ast::AstFunction>(ast.items.back());
  const checker::Context context{
      .target = {.ptx_version = {8, 6}, .sm_version = 100},
      .instruction_range = ast.range,
  };

  const auto expect_fields = [&](const WithLocs<RoundingMode>& rounding,
                                 const WithLocs<ScalarType>& input_type,
                                 const WithLocs<bool>& saturate,
                                 const syntax_ast::AstInstruction& instruction,
                                 const size_t type_index,
                                 const size_t rounding_index,
                                 const bool expected_saturate,
                                 const size_t input_modifier_index,
                                 const size_t saturate_modifier_index) {
    EXPECT_EQ(rounding.value, rounding_modes[rounding_index]);
    if (roundings[rounding_index].empty()) {
      EXPECT_TRUE(rounding.locs.empty());
    } else {
      ASSERT_EQ(rounding.locs.size(), 1U);
      EXPECT_EQ(rounding.locs.front(), instruction.modifiers.front().syntax.range);
    }
    EXPECT_EQ(input_type.value, input_type_values[type_index]);
    ASSERT_EQ(input_type.locs.size(), 1U);
    EXPECT_EQ(input_type.locs.front(),
              instruction.modifiers[input_modifier_index].syntax.range);
    EXPECT_EQ(saturate.value, expected_saturate);
    if (expected_saturate) {
      ASSERT_EQ(saturate.locs.size(), 1U);
      EXPECT_EQ(saturate.locs.front(),
                instruction.modifiers[saturate_modifier_index].syntax.range);
    } else {
      EXPECT_TRUE(saturate.locs.empty());
    }
  };

  const auto expect_instruction =
      [&](const ResolvedInstruction& resolved_instruction,
          const syntax_ast::AstInstruction& syntax_instruction,
          const size_t type_index, const size_t rounding_index,
          const bool expected_saturate, const size_t input_modifier_index,
          const size_t saturate_modifier_index) {
        if (const auto* add = std::get_if<Add>(&resolved_instruction)) {
          const auto& mixed = std::get<Add::MixedF32>(add->variant);
          expect_fields(mixed.rounding, mixed.input_type, mixed.saturate,
                        syntax_instruction, type_index, rounding_index,
                        expected_saturate, input_modifier_index,
                        saturate_modifier_index);
          EXPECT_TRUE(checker::check(*add, context).has_value());
        } else {
          const auto& sub = std::get<Sub>(resolved_instruction);
          const auto& mixed = std::get<Sub::MixedF32>(sub.variant);
          expect_fields(mixed.rounding, mixed.input_type, mixed.saturate,
                        syntax_instruction, type_index, rounding_index,
                        expected_saturate, input_modifier_index,
                        saturate_modifier_index);
          EXPECT_TRUE(checker::check(sub, context).has_value());
        }
      };

  size_t instruction_index = 0;
  for (size_t opcode_index = 0; opcode_index != 2U; ++opcode_index) {
    for (size_t type_index = 0; type_index != input_types.size(); ++type_index) {
      for (size_t rounding_index = 0; rounding_index != roundings.size();
           ++rounding_index) {
        const size_t type_modifier_index =
            roundings[rounding_index].empty() ? 0U : 1U;
        const auto& nonsat_syntax = std::get<syntax_ast::AstInstruction>(
            syntax_function.body[3U + instruction_index]);
        const auto& canonical_syntax = std::get<syntax_ast::AstInstruction>(
            syntax_function.body[4U + instruction_index]);
        const auto& legacy_syntax = std::get<syntax_ast::AstInstruction>(
            syntax_function.body[5U + instruction_index]);

        expect_instruction(body[instruction_index], nonsat_syntax, type_index,
                           rounding_index, false, type_modifier_index + 1U, 0U);
        expect_instruction(body[instruction_index + 1U], canonical_syntax,
                           type_index, rounding_index, true,
                           type_modifier_index + 2U, type_modifier_index);
        expect_instruction(body[instruction_index + 2U], legacy_syntax,
                           type_index, rounding_index, true,
                           type_modifier_index + 1U,
                           type_modifier_index + 2U);
        instruction_index += 3U;
      }
    }
  }
}

TEST(ResolvedModule, ResolvesNegativeUnsignedImmediatesAtTargetWidth) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  add.u32 %r0, %r1, -1;
  mov.u32 %r0, -1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& add = resolvedIntegerAdd(body[0]);
  const auto& add_immediate = std::get<ResolvedImmediate>(add.src2.value);
  EXPECT_EQ(add_immediate.type, ScalarType::U32);
  EXPECT_EQ(add_immediate.bits, 0xffffffffU);
  const auto& mov_immediate = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(mov_immediate.type, ScalarType::U32);
  EXPECT_EQ(mov_immediate.bits, 0xffffffffU);
}

/** Verifies ordinary data operands narrow evaluated 64-bit integer sources. */
TEST(ResolvedModule, ConvertsOrdinaryIntegerDataImmediatesAndMinusZero) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0;
  .reg .u32 %u<2>;
  mov.s32 %s0, 0xffffffff;
  mov.u32 %u0, 4294967296;
  mov.u32 %u1, -0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& signed_word = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[0])).src.value);
  EXPECT_EQ(signed_word.bits, 0xffffffffU);
  EXPECT_EQ(signed_word.integer_source_bits, 0xffffffffU);
  const auto& zero_word = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(zero_word.bits, 0U);
  EXPECT_EQ(zero_word.integer_source_bits, 4294967296U);
  const auto& minus_zero = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[2])).src.value);
  EXPECT_EQ(minus_zero.bits, 0U);
  EXPECT_EQ(minus_zero.integer_source_bits, 0U);
  EXPECT_FALSE(minus_zero.is_negative);
}

/** Keeps address offsets in their strict signed 64-bit domain. */
TEST(ResolvedModule, RejectsOutOfRangeAddressOffsetBeforeNarrowing) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .u32 base;
.entry kernel() {
  .reg .u32 %r0;
  ld.u32 %r0, [base+9223372036854775808];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().front().message,
            "Integer literal '9223372036854775808' is out of range for scalar type 'S64'.");
}

TEST(ResolvedModule, ResolvesBareRetInDeviceFunctionAndEntry) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func device() {
  ret;
}
.entry kernel() {
  ret;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_FALSE(resolved->functions[0].is_entry);
  EXPECT_TRUE(resolved->functions[1].is_entry);
  for (const auto& function : resolved->functions) {
    ASSERT_EQ(function.body.size(), 1u);
    const auto& ret = std::get<Ret>(function.body.front());
    EXPECT_TRUE(checker::check(
                    ret,
                    checker::Context{
                        .target = {.ptx_version = {1, 0}, .sm_version = 0},
                        .instruction_range = ast.range,
                    })
                    .has_value());
  }
}

TEST(ResolvedModule, ResolvesBareAndPredicatedExitInDeviceFunctionAndEntry) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func device() {
  .reg .pred %p0;
  @%p0 exit;
}
.entry kernel() {
  exit;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_FALSE(resolved->functions[0].is_entry);
  EXPECT_TRUE(resolved->functions[1].is_entry);
  const auto& device_exit =
      std::get<Exit>(resolved->functions[0].body.front());
  EXPECT_TRUE(device_exit.execution_predicate.has_value());
  const auto& entry_exit =
      std::get<Exit>(resolved->functions[1].body.front());
  EXPECT_FALSE(entry_exit.execution_predicate.has_value());
}

TEST(ResolvedModule, ResolvesBareAndPredicatedTrapInDeviceFunctionAndEntry) {
  const auto parsed_module_1 = parseModule(R"ptx(
.func device() {
  .reg .pred %p0;
  @%p0 trap;
}
.entry kernel() {
  trap;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_FALSE(resolved->functions[0].is_entry);
  EXPECT_TRUE(resolved->functions[1].is_entry);
  const auto& device_trap =
      std::get<Trap>(resolved->functions[0].body.front());
  EXPECT_TRUE(device_trap.execution_predicate.has_value());
  const auto& entry_trap =
      std::get<Trap>(resolved->functions[1].body.front());
  EXPECT_FALSE(entry_trap.execution_predicate.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksM10CacheHintEvictionSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd<2>;
  .reg .u32 %r<3>;
  ld.global.L1::evict_first.u32 %r0, [%rd0];
  ld.global.L1::evict_last.u32 %r1, [%rd0];
  st.global.L1::evict_first.u32 [%rd0], %r0;
  st.global.L1::evict_last.u32 [%rd0], %r1;
  ld.global.L2::cache_hint.u32 %r2, [%rd0], %rd1;
  st.global.L2::cache_hint.u32 [%rd0], %r2, %rd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);

  const auto& load_first =
      std::get<Ld::GlobalU32L1Evict>(std::get<Ld>(body[0]).variant);
  const auto& load_last =
      std::get<Ld::GlobalU32L1Evict>(std::get<Ld>(body[1]).variant);
  const auto& store_first =
      std::get<St::GlobalU32L1Evict>(std::get<St>(body[2]).variant);
  const auto& store_last =
      std::get<St::GlobalU32L1Evict>(std::get<St>(body[3]).variant);
  EXPECT_EQ(load_first.eviction_priority.value,
            EvictionPriority::EvictFirst);
  EXPECT_EQ(load_last.eviction_priority.value, EvictionPriority::EvictLast);
  EXPECT_EQ(store_first.eviction_priority.value,
            EvictionPriority::EvictFirst);
  EXPECT_EQ(store_last.eviction_priority.value, EvictionPriority::EvictLast);

  const auto& load_hint =
      std::get<Ld::GlobalU32L2CacheHint>(std::get<Ld>(body[4]).variant);
  const auto& store_hint =
      std::get<St::GlobalU32L2CacheHint>(std::get<St>(body[5]).variant);
  EXPECT_TRUE(load_hint.cache_hint);
  EXPECT_TRUE(store_hint.cache_hint);
  EXPECT_EQ(load_hint.cache_policy.value.declared_type, ScalarType::U64);
  EXPECT_EQ(store_hint.cache_policy.value.declared_type, ScalarType::U64);

  const checker::Context l1_target{
      .target = {.ptx_version = {7, 4}, .sm_version = 70},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Ld>(body[0]), l1_target).has_value());
  EXPECT_TRUE(checker::check(std::get<Ld>(body[1]), l1_target).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[2]), l1_target).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[3]), l1_target).has_value());
  const checker::Context l2_target{
      .target = {.ptx_version = {7, 4}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Ld>(body[4]), l2_target).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[5]), l2_target).has_value());

  const auto l1_too_old = checker::check(
      std::get<Ld>(body[0]),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(l1_too_old.has_value());
  EXPECT_EQ(l1_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const auto l2_too_old = checker::check(
      std::get<St>(body[5]),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(l2_too_old.has_value());
  EXPECT_EQ(l2_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
}

TEST(ResolvedModule, ChecksM12LdGlobalNcL1NoAllocateSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  ld.global.nc.L1::no_allocate.u32 %r0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction = std::get<Ld>(resolved->functions.front().body.front());
  const auto& load =
      std::get<Ld::GlobalNcL1NoAllocateU32>(instruction.variant);
  EXPECT_EQ(load.dst.value.declared_type, ScalarType::U32);
  const auto& global_address =
      std::get<ResolvedSymbolRef>(load.address.value.base);
  EXPECT_EQ(global_address.address_state_space,
            syntax_ast::AstStateSpace::Global);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{.target = {.ptx_version = {7, 4},
                                              .sm_version = 70},
                                   .instruction_range = ast.range})
                  .has_value());

  const auto ptx_too_old = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {7, 3}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(ptx_too_old.has_value());
  EXPECT_EQ(ptx_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto sm_too_old = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(sm_too_old.has_value());
  EXPECT_EQ(sm_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.local .align 4 .u32 local_value;
.entry kernel() {
  .reg .u32 %r0;
  ld.global.nc.L1::no_allocate.u32 %r0, [local_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_address.has_value())
      << wrong_address.error().front().message;
  const auto address_checked = checker::check(
      std::get<Ld>(wrong_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(address_checked.has_value());
  EXPECT_EQ(address_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  ld.global.nc.L1::no_allocate.u32 %h0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_dst = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_dst.has_value()) << wrong_dst.error().front().message;
  const auto dst_checked = checker::check(
      std::get<Ld>(wrong_dst->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(dst_checked.has_value());
  EXPECT_EQ(dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsM10CacheHintPolicyWithoutU64Register) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r0;
  .reg .u32 %policy;
  ld.global.L2::cache_hint.u32 %r0, [%rd0], %policy;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& wrong_policy_ast = *parsed_module_1;
  const auto wrong_policy = resolveModule(wrong_policy_ast);
  ASSERT_TRUE(wrong_policy.has_value()) << wrong_policy.error().front().message;
  const auto checked = checker::check(
      std::get<Ld>(wrong_policy->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80},
                       .instruction_range = wrong_policy_ast.range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r0;
  ld.global.L2::cache_hint.u32 %r0, [%rd0];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto missing_policy = resolveModule(*parsed_module_2);
  ASSERT_FALSE(missing_policy.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLduGlobalU32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u64 %wide;
  ldu.global.u32 %wide, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction = std::get<Ldu>(resolved->functions.front().body.front());
  const auto& load = std::get<Ldu::GlobalU32>(instruction.variant);
  EXPECT_EQ(load.state_space, MemoryStateSpace::Global);
  EXPECT_EQ(load.type, ScalarType::U32);
  EXPECT_EQ(load.dst.value.declared_type, ScalarType::U64);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {2, 0}, .sm_version = 0},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 9}, .sm_version = 0},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old.has_value());
  EXPECT_EQ(too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r0;
  ldu.global.u32 %r0, [local_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto local_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_space = checker::check(
      std::get<Ldu>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 0}});
  ASSERT_FALSE(wrong_space.has_value());
  EXPECT_EQ(wrong_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  ldu.global.u32 %h0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto narrow_dst = resolveModule(*parsed_module_3);
  ASSERT_TRUE(narrow_dst.has_value()) << narrow_dst.error().front().message;
  const auto wrong_register = checker::check(
      std::get<Ldu>(narrow_dst->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 0}});
  ASSERT_FALSE(wrong_register.has_value());
  EXPECT_EQ(wrong_register.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  ldu.global.b32 %r0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_type = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_type.has_value());

  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  ldu.global.u32 %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto missing_address = resolveModule(*parsed_module_5);
  ASSERT_FALSE(missing_address.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksPrefetchGlobalL1Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  prefetch.global.L1 [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Prefetch>(resolved->functions.front().body.front());
  const auto& prefetch = std::get<Prefetch::GlobalL1>(instruction.variant);
  EXPECT_EQ(prefetch.state_space, MemoryStateSpace::Global);
  EXPECT_TRUE(prefetch.l1);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {2, 0}, .sm_version = 20},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  prefetch.global.L1 [local_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto local_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Prefetch>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { prefetch.global.L2 [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_level = resolveModule(*parsed_module_3);
  ASSERT_FALSE(wrong_level.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.local .u32 local_value;
.entry kernel() { prefetch.local.L1 [local_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_space = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_space.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { prefetch.global.L1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto missing_address = resolveModule(*parsed_module_5);
  ASSERT_FALSE(missing_address.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { prefetch.global.L1 [global_value], [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto extra_address = resolveModule(*parsed_module_6);
  ASSERT_FALSE(extra_address.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksPrefetchuL1GenericAddressSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  prefetchu.L1 [%rd0];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Prefetchu>(resolved->functions.front().body.front());
  const auto& prefetchu = std::get<Prefetchu::L1>(instruction.variant);
  EXPECT_TRUE(prefetchu.l1);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {2, 0}, .sm_version = 20},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { prefetchu.L1 [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto global_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(global_address.has_value())
      << global_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Prefetchu>(global_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  for (const auto source : {
           ".entry kernel() { .reg .u64 %rd0; prefetchu.L2 [%rd0]; }",
           ".entry kernel() { .reg .u64 %rd0; prefetchu.global.L1 [%rd0]; }",
           ".entry kernel() { .reg .u64 %rd0; prefetchu.L1 %rd0; }",
           ".entry kernel() { prefetchu.L1; }",
           ".entry kernel() { .reg .u64 %rd0; prefetchu.L1 [%rd0], [%rd0]; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    EXPECT_FALSE(resolveModule(*parsed_module_3).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksCreatepolicyFractionalL2EvictLastSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b64 %b<3>;
  createpolicy.fractional.L2::evict_last.b64 %b0, 0.5;
  createpolicy.fractional.L2::evict_last.b64 %b1, 0f3f000000;
  createpolicy.fractional.L2::evict_last.b64 %b2, 0d3fe0000000000000;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  for (const auto& resolved_instruction : body) {
    const auto& instruction = std::get<Createpolicy>(resolved_instruction);
    const auto& policy =
        std::get<Createpolicy::FractionalL2EvictLastB64>(instruction.variant);
    EXPECT_TRUE(policy.fractional);
    EXPECT_EQ(policy.eviction_priority, EvictionPriority::EvictLast);
    EXPECT_EQ(policy.type, ScalarType::B64);
    EXPECT_EQ(policy.dst.value.declared_type, ScalarType::B64);
    EXPECT_EQ(policy.fraction.value.type, ScalarType::F32);
    EXPECT_EQ(policy.fraction.value.bits, 1056964608u);
    EXPECT_TRUE(checker::check(
                    instruction,
                    checker::Context{.target = {.ptx_version = {7, 4},
                                                .sm_version = 80},
                                     .instruction_range = ast.range})
                    .has_value());
  }

  const auto too_old_ptx = checker::check(
      std::get<Createpolicy>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 3}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Createpolicy>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .b64 %b<4>;
  createpolicy.fractional.L2::evict_last.b64 %b0, 0.0;
  createpolicy.fractional.L2::evict_last.b64 %b1, -0.0;
  // PTX-legal fractional values, intentionally outside this frozen 0.5 slice.
  createpolicy.fractional.L2::evict_last.b64 %b2, .25;
  createpolicy.fractional.L2::evict_last.b64 %b3, 1.0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto unfrozen_fractions = resolveModule(*parsed_module_2);
  ASSERT_TRUE(unfrozen_fractions.has_value())
      << unfrozen_fractions.error().front().message;
  for (const auto& resolved_instruction : unfrozen_fractions->functions.front().body) {
    const auto checked = checker::check(
        std::get<Createpolicy>(resolved_instruction),
        checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  createpolicy.fractional.L2::evict_last.b64 %rd0, 0.5;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto compatible_dst = resolveModule(*parsed_module_3);
  ASSERT_TRUE(compatible_dst.has_value())
      << compatible_dst.error().front().message;
  EXPECT_TRUE(checker::check(
      std::get<Createpolicy>(compatible_dst->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}})
                  .has_value());

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  createpolicy.fractional.L2::evict_last.b64 %r0, 0.5;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto narrow_dst = resolveModule(*parsed_module_4);
  ASSERT_TRUE(narrow_dst.has_value()) << narrow_dst.error().front().message;
  const auto narrow_dst_checked = checker::check(
      std::get<Createpolicy>(narrow_dst->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(narrow_dst_checked.has_value());
  EXPECT_EQ(narrow_dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  // This is ISA-legal without .fractional, but intentionally unfrozen here.
  for (const auto source : {
           ".entry kernel() { .reg .b64 %b0; createpolicy.L2::evict_last.b64 %b0, 0.5; }",
           ".entry kernel() { .reg .b64 %b0; createpolicy.fractional.L1::evict_last.b64 %b0, 0.5; }",
           ".entry kernel() { .reg .b64 %b0; createpolicy.fractional.L2::evict_first.b64 %b0, 0.5; }",
           ".entry kernel() { .reg .b64 %b0; createpolicy.fractional.L2::evict_last.u64 %b0, 0.5; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_5 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    EXPECT_FALSE(resolveModule(*parsed_module_5).has_value());
  }

  for (const auto source : {
           "createpolicy.fractional.L2::evict_last.b64 %b0, +inf;",
           "createpolicy.fractional.L2::evict_last.b64 %b0, NaN;",
       }) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto parsed = parser.parseInstruction();
    EXPECT_FALSE(parsed.has_value() && resolve<Createpolicy>(*parsed).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksApplypriorityGlobalL2EvictNormalSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 128 .b8 aligned_value[128];
.entry kernel() {
  .reg .u64 %rd0;
  applypriority.global.L2::evict_normal [%rd0], 128;
  applypriority.global.L2::evict_normal [aligned_value], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  for (const auto& resolved_instruction : body) {
    const auto& instruction = std::get<Applypriority>(resolved_instruction);
    const auto& priority =
        std::get<Applypriority::GlobalL2EvictNormal>(instruction.variant);
    EXPECT_EQ(priority.state_space, MemoryStateSpace::Global);
    EXPECT_EQ(priority.eviction_priority, EvictionPriority::EvictNormal);
    EXPECT_EQ(priority.size.value.type, ScalarType::U32);
    EXPECT_EQ(priority.size.value.bits, 128u);
    EXPECT_TRUE(checker::check(
                    instruction,
                    checker::Context{.target = {.ptx_version = {7, 4},
                                                .sm_version = 80},
                                     .instruction_range = ast.range})
                    .has_value());
  }

  const auto too_old_ptx = checker::check(
      std::get<Applypriority>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 3}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Applypriority>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 128 .b8 aligned_value[128];
.entry kernel() {
  applypriority.global.L2::evict_normal [aligned_value], 64;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_size = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_size.has_value()) << wrong_size.error().front().message;
  const auto size_checked = checker::check(
      std::get<Applypriority>(wrong_size->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(size_checked.has_value());
  EXPECT_EQ(size_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 64 .b8 unaligned_value[128];
.entry kernel() {
  applypriority.global.L2::evict_normal [unaligned_value], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  const auto alignment_checked = checker::check(
      std::get<Applypriority>(unaligned->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(alignment_checked.has_value());
  EXPECT_EQ(alignment_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.local .align 128 .b8 local_value[128];
.entry kernel() {
  applypriority.global.L2::evict_normal [local_value], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto local_address = resolveModule(*parsed_module_4);
  ASSERT_TRUE(local_address.has_value()) << local_address.error().front().message;
  const auto address_checked = checker::check(
      std::get<Applypriority>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(address_checked.has_value());
  EXPECT_EQ(address_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  for (const auto source : {
           ".entry kernel() { .reg .u64 %rd0; .reg .u32 %r0; applypriority.global.L2::evict_normal [%rd0], %r0; }",
           ".entry kernel() { .reg .u64 %rd0; applypriority.L2::evict_normal [%rd0], 128; }",
           ".entry kernel() { .reg .u64 %rd0; applypriority.global.L1::evict_normal [%rd0], 128; }",
           ".entry kernel() { .reg .u64 %rd0; applypriority.global.L2::evict_last [%rd0], 128; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_5 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    EXPECT_FALSE(resolveModule(*parsed_module_5).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksDiscardGlobalL2Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 128 .b8 aligned_value[128];
.entry kernel() {
  .reg .u64 %rd0;
  discard.global.L2 [%rd0], 128;
  discard.global.L2 [aligned_value], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  for (const auto& resolved_instruction : body) {
    const auto& instruction = std::get<Discard>(resolved_instruction);
    const auto& discard = std::get<Discard::GlobalL2>(instruction.variant);
    EXPECT_EQ(discard.state_space, MemoryStateSpace::Global);
    EXPECT_TRUE(discard.l2);
    EXPECT_EQ(discard.size.value.type, ScalarType::U32);
    EXPECT_EQ(discard.size.value.bits, 128u);
    EXPECT_TRUE(checker::check(
                    instruction,
                    checker::Context{.target = {.ptx_version = {7, 4},
                                                .sm_version = 80},
                                     .instruction_range = ast.range})
                    .has_value());
  }

  const auto too_old_ptx = checker::check(
      std::get<Discard>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 3}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Discard>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 128 .b8 aligned_value[128];
.entry kernel() { discard.global.L2 [aligned_value], 64; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_size = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_size.has_value()) << wrong_size.error().front().message;
  const auto size_checked = checker::check(
      std::get<Discard>(wrong_size->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(size_checked.has_value());
  EXPECT_EQ(size_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 64 .b8 unaligned_value[128];
.entry kernel() { discard.global.L2 [unaligned_value], 128; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  const auto alignment_checked = checker::check(
      std::get<Discard>(unaligned->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(alignment_checked.has_value());
  EXPECT_EQ(alignment_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.local .align 128 .b8 local_value[128];
.entry kernel() { discard.global.L2 [local_value], 128; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto local_address = resolveModule(*parsed_module_4);
  ASSERT_TRUE(local_address.has_value()) << local_address.error().front().message;
  const auto address_checked = checker::check(
      std::get<Discard>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80}});
  ASSERT_FALSE(address_checked.has_value());
  EXPECT_EQ(address_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  for (const auto source : {
           ".entry kernel() { .reg .u64 %rd0; .reg .u32 %r0; discard.global.L2 [%rd0], %r0; }",
           ".entry kernel() { .reg .u64 %rd0; discard.L2 [%rd0], 128; }",
           ".entry kernel() { .reg .u64 %rd0; discard.global.L1 [%rd0], 128; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_5 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    EXPECT_FALSE(resolveModule(*parsed_module_5).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksSetmaxnregIncSyncAlignedSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 8.0
.target sm_90a
.entry kernel() {
  setmaxnreg.inc.sync.aligned.u32 24;
  setmaxnreg.inc.sync.aligned.u32 192;
  setmaxnreg.inc.sync.aligned.u32 256;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);

  const auto profile = base::find_target_profile("sm_90a");
  ASSERT_TRUE(profile.has_value());
  const auto context_for = [&ast](std::string_view target,
                                  checker::PtxVersion ptx_version) {
    const auto target_profile = base::find_target_profile(target);
    EXPECT_TRUE(target_profile.has_value()) << target;
    return checker::Context{
        .target = {.ptx_version = ptx_version,
                   .sm_version = target_profile->identity.architecture.number,
                   .enabled_family_features = target_profile->enabled_family_features,
                   .identity = target_profile->identity,
                   .capabilities = target_profile->capabilities},
        .instruction_range = ast.range,
    };
  };
  const checker::Context supported_context{
      .target = {.ptx_version = {8, 0},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = ast.range,
  };
  constexpr std::array<uint64_t, 3> expected_counts{24, 192, 256};
  for (size_t index = 0; index < body.size(); ++index) {
    const auto& resolved_instruction = body[index];
    const auto& instruction = std::get<Setmaxnreg>(resolved_instruction);
    const auto& setmaxnreg =
        std::get<Setmaxnreg::IncSyncAlignedU32>(instruction.variant);
    EXPECT_TRUE(setmaxnreg.inc);
    EXPECT_TRUE(setmaxnreg.sync);
    EXPECT_TRUE(setmaxnreg.aligned);
    EXPECT_EQ(Setmaxnreg::IncSyncAlignedU32::type, ScalarType::U32);
    EXPECT_EQ(setmaxnreg.count.value.type, ScalarType::U32);
    EXPECT_EQ(setmaxnreg.count.value.bits, expected_counts[index]);
    EXPECT_TRUE(checker::check(instruction, supported_context).has_value());
  }

  struct SupportedTarget {
    std::string_view spelling;
    checker::PtxVersion ptx_version;
  };
  constexpr std::array<SupportedTarget, 6> supported_targets{{
      {"sm_90a", {8, 0}},
      {"sm_100a", {8, 6}},
      {"sm_100f", {8, 8}},
      {"sm_103a", {8, 8}},
      {"sm_103f", {8, 8}},
      {"sm_120f", {8, 8}},
  }};
  for (const auto& target : supported_targets) {
    SCOPED_TRACE(target.spelling);
    EXPECT_TRUE(checker::check(std::get<Setmaxnreg>(body.front()),
                               context_for(target.spelling, target.ptx_version))
                    .has_value());
  }
  constexpr std::array<SupportedTarget, 5> below_threshold_targets{{
      {"sm_100a", {8, 5}},
      {"sm_100f", {8, 7}},
      {"sm_103a", {8, 7}},
      {"sm_103f", {8, 7}},
      {"sm_120f", {8, 7}},
  }};
  for (const auto& target : below_threshold_targets) {
    SCOPED_TRACE(target.spelling);
    EXPECT_FALSE(checker::check(std::get<Setmaxnreg>(body.front()),
                                context_for(target.spelling, target.ptx_version))
                     .has_value());
  }

  for (const auto source : {
           "setmaxnreg.inc.sync.aligned.u32 23;",
           "setmaxnreg.inc.sync.aligned.u32 25;",
           "setmaxnreg.inc.sync.aligned.u32 193;",
           "setmaxnreg.inc.sync.aligned.u32 257;",
           "setmaxnreg.inc.sync.aligned.u32 -8;",
       }) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto instruction_ast = parser.parseInstruction();
    ASSERT_TRUE(instruction_ast.has_value()) << instruction_ast.diagnostics.front().message;
    const auto setmaxnreg = resolve<Setmaxnreg>(*instruction_ast);
    ASSERT_TRUE(setmaxnreg.has_value()) << setmaxnreg.error().message;
    const auto checked = checker::check(
        *setmaxnreg, checker::Context{.target = supported_context.target,
                                       .instruction_range = instruction_ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  }

  const auto too_old_ptx = checker::check(
      std::get<Setmaxnreg>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 9},
                                  .sm_version = profile->identity.architecture.number,
                                  .identity = profile->identity},
                       .instruction_range = ast.range});
  EXPECT_FALSE(too_old_ptx.has_value());
  for (const std::string_view target : {"sm_90", "sm_100", "sm_103"}) {
    SCOPED_TRACE(target);
    EXPECT_FALSE(checker::check(std::get<Setmaxnreg>(body.front()),
                                context_for(target, {9, 3}))
                     .has_value());
  }

  for (const auto source : {
           ".entry kernel() { .reg .u32 %r0; setmaxnreg.inc.sync.aligned.u32 %r0; }",
           ".entry kernel() { setmaxnreg.dec.sync.aligned.u32 192; }",
           ".entry kernel() { setmaxnreg.inc.aligned.u32 192; }",
           ".entry kernel() { setmaxnreg.inc.sync.u32 192; }",
           ".entry kernel() { setmaxnreg.inc.sync.aligned.s32 192; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    EXPECT_FALSE(resolveModule(*parsed_module_2).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncCaSharedGlobalSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 16 .b8 global_value[16];
.shared .align 16 .b8 shared_value[16];
.entry kernel() {
  cp.async.ca.shared.global [shared_value], [global_value], 4;
  cp.async.ca.shared.global [shared_value], [global_value], 8;
  cp.async.ca.shared.global [shared_value], [global_value], 16;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;

  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& copy =
      std::get<Cp::AsyncCaSharedGlobal>(std::get<Cp>(body.front()).variant);
  EXPECT_TRUE(copy.async);
  EXPECT_TRUE(copy.ca);
  EXPECT_TRUE(copy.shared);
  EXPECT_TRUE(copy.global);
  EXPECT_EQ(copy.cp_size.value.type, ScalarType::U32);
  EXPECT_EQ(copy.cp_size.value.bits, 4u);
  const checker::Context context{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(instruction), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Cp>(body.front()),
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Cp>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() {
  cp.async.ca.shared.global [global_value], [global_value], 4;
  cp.async.ca.shared.global [shared_value], [shared_value], 4;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_spaces = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_spaces.has_value())
      << wrong_spaces.error().front().message;
  for (const auto& instruction : wrong_spaces->functions.front().body) {
    const auto checked = checker::check(std::get<Cp>(instruction), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() {
  cp.async.ca.shared.global [shared_value], [global_value], 3;
  cp.async.ca.shared.global [shared_value], [global_value], 32;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto invalid_sizes = resolveModule(*parsed_module_3);
  ASSERT_TRUE(invalid_sizes.has_value())
      << invalid_sizes.error().front().message;
  for (const auto& instruction : invalid_sizes->functions.front().body) {
    const auto checked = checker::check(std::get<Cp>(instruction), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_TRUE(std::ranges::any_of(
        checked.error(), [](const checker::CheckDiagnostic& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ImmediateValueMismatch;
        }));
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { .reg .u32 %r0; cp.async.ca.shared.global [shared_value], [global_value], %r0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto register_size = resolveModule(*parsed_module_4);
  ASSERT_FALSE(register_size.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.ca.shared.global [shared_value], [global_value], 4.0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto non_integer_size = resolveModule(*parsed_module_5);
  ASSERT_FALSE(non_integer_size.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.cg.shared.global [shared_value], [global_value], 4; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto wrong_modifier = resolveModule(*parsed_module_6);
  ASSERT_FALSE(wrong_modifier.has_value());
  const auto parsed_module_7 = parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.ca.shared.global [shared_value], [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto missing_size = resolveModule(*parsed_module_7);
  ASSERT_FALSE(missing_size.has_value());
}

TEST(ResolvedModule, ChecksCpAsyncDynamicAddressAlignment) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 16 .b8 global_copy_src[32];
.shared .align 16 .b8 shared_copy_dst[32];
.entry kernel() {
  cp.async.ca.shared.global [shared_copy_dst+4], [global_copy_src+12], 4;
  cp.async.ca.shared.global [shared_copy_dst+8], [global_copy_src+8], 8;
  cp.async.ca.shared.global [shared_copy_dst+16], [global_copy_src+16], 16;
  cp.async.ca.shared.global [shared_copy_dst+2], [global_copy_src+4], 4;
  cp.async.ca.shared.global [shared_copy_dst+4], [global_copy_src+8], 8;
  cp.async.ca.shared.global [shared_copy_dst+16], [global_copy_src+4], 16;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
  };
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  for (size_t index = 0; index < 3; ++index)
    EXPECT_TRUE(checker::check(std::get<Cp>(body[index]), context).has_value());
  for (size_t index = 3; index < body.size(); ++index) {
    const auto checked = checker::check(std::get<Cp>(body[index]), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncMbarrierArriveSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.shared .align 8 .b64 shared_value[2];
.entry kernel() {
  .reg .u64 %rd0;
  cp.async.mbarrier.arrive.b64 [%rd0];
  cp.async.mbarrier.arrive.shared.b64 [shared_value];
  cp.async.mbarrier.arrive.shared::cta.b64 [shared_value+8];
  cp.async.mbarrier.arrive.noinc.b64 [%rd0];
  cp.async.mbarrier.arrive.noinc.shared::cta.b64 [shared_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncMbarrierArriveGenericOrShared>(
      std::get<Cp>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Cp::AsyncMbarrierArriveSharedCta>(
      std::get<Cp>(body[2]).variant));
  const auto& noinc = std::get<Cp::AsyncMbarrierArriveNoincGenericOrShared>(
      std::get<Cp>(body[3]).variant);
  EXPECT_TRUE(noinc.noinc);
  EXPECT_EQ(noinc.type, ScalarType::B64);

  const checker::Context supported{
      .target = {.ptx_version = {7, 8}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(instruction), supported).has_value());
  const auto old_ptx = checker::check(
      std::get<Cp>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      std::get<Cp>(body[0]),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const auto old_cta = checker::check(
      std::get<Cp>(body[2]),
      checker::Context{.target = {.ptx_version = {7, 7}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_cta.has_value());
  EXPECT_EQ(old_cta.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 8 .b64 global_value[2];
.entry kernel() { cp.async.mbarrier.arrive.b64 [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_space = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_space.has_value()) << wrong_space.error().front().message;
  const auto wrong_space_checked = checker::check(
      std::get<Cp>(wrong_space->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_space_checked.has_value());
  EXPECT_EQ(wrong_space_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 4 .b64 unaligned_value[2];
.shared .align 8 .b64 aligned_value[2];
.entry kernel() {
  cp.async.mbarrier.arrive.b64 [unaligned_value];
  cp.async.mbarrier.arrive.shared.b64 [aligned_value+4];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned.has_value()) << unaligned.error().front().message;
  for (const auto& instruction : unaligned->functions.front().body) {
    const auto checked = checker::check(std::get<Cp>(instruction), supported);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncCommitGroupSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { cp.async.commit_group; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Cp>(resolved->functions.front().body.front());
  const auto& commit = std::get<Cp::AsyncCommitGroup>(instruction.variant);
  EXPECT_TRUE(commit.async);
  EXPECT_TRUE(commit.commit_group);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { cp.commit_group; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto missing_async = resolveModule(*parsed_module_2);
  ASSERT_FALSE(missing_async.has_value());
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { cp.async.commit_group 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto extra_operand = resolveModule(*parsed_module_3);
  ASSERT_FALSE(extra_operand.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_wait_token = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_wait_token.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncWaitGroupSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  cp.async.wait_group 0;
  cp.async.wait_group 1;
  cp.async.wait_group 4294967295;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& zero =
      std::get<Cp::AsyncWaitGroup>(std::get<Cp>(body[0]).variant);
  const auto& large =
      std::get<Cp::AsyncWaitGroup>(std::get<Cp>(body[2]).variant);
  EXPECT_TRUE(zero.async);
  EXPECT_TRUE(zero.wait_group);
  EXPECT_EQ(zero.n.value.type, ScalarType::U32);
  EXPECT_EQ(zero.n.value.bits, 0u);
  EXPECT_EQ(large.n.value.bits, 4294967295u);
  const checker::Context context{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Cp>(instruction), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Cp>(body.front()),
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Cp>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %r0; cp.async.wait_group %r0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto register_operand = resolveModule(*parsed_module_2);
  ASSERT_FALSE(register_operand.has_value());
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group 1.0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto float_operand = resolveModule(*parsed_module_3);
  ASSERT_FALSE(float_operand.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group -1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto negative_operand = resolveModule(*parsed_module_4);
  ASSERT_TRUE(negative_operand.has_value())
      << negative_operand.error().front().message;
  const auto negative_checked = checker::check(
      std::get<Cp>(negative_operand->functions.front().body.front()), context);
  ASSERT_FALSE(negative_checked.has_value());
  EXPECT_EQ(negative_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  for (const auto literal : {"-1U", "4294967296"}) {
    const auto parsed_module_5 = parseModule(
        std::string(".entry kernel() { cp.async.wait_group ") + literal + "; }");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    const auto out_of_range = resolveModule(*parsed_module_5);
    SCOPED_TRACE(literal);
    ASSERT_FALSE(out_of_range.has_value());
    EXPECT_EQ(out_of_range.error().front().message,
              std::string("Integer literal '") + literal +
                  "' is out of range for scalar type 'U32'.");
  }
  const auto parsed_module_6 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto missing_operand = resolveModule(*parsed_module_6);
  ASSERT_FALSE(missing_operand.has_value());
  const auto parsed_module_7 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group 0, 1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto extra_operand = resolveModule(*parsed_module_7);
  ASSERT_FALSE(extra_operand.has_value());
  const auto parsed_module_8 = parseModule(R"ptx(
.entry kernel() { cp.wait_group 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_8);
  const auto missing_async = resolveModule(*parsed_module_8);
  ASSERT_FALSE(missing_async.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncWaitAllSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_all; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Cp>(resolved->functions.front().body.front());
  const auto& wait_all = std::get<Cp::AsyncWaitAll>(instruction.variant);
  EXPECT_TRUE(wait_all.async);
  EXPECT_TRUE(wait_all.wait_all);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { cp.wait_all; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto missing_async = resolveModule(*parsed_module_2);
  ASSERT_FALSE(missing_async.has_value());
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_all 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto immediate_operand = resolveModule(*parsed_module_3);
  ASSERT_FALSE(immediate_operand.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %r0; cp.async.wait_all %r0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto register_operand = resolveModule(*parsed_module_4);
  ASSERT_FALSE(register_operand.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto wrong_token = resolveModule(*parsed_module_5);
  ASSERT_FALSE(wrong_token.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLdmatrixSyncAlignedM8n8X2SharedB16Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 16 .b16 shared_value[16];
.entry kernel() {
  .reg .b32 %r<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Ldmatrix>(resolved->functions.front().body.front());
  const auto& matrix = std::get<Ldmatrix::SyncAlignedM8n8X2SharedB16>(
      instruction.variant);
  EXPECT_TRUE(matrix.sync);
  EXPECT_TRUE(matrix.aligned);
  EXPECT_TRUE(matrix.m8n8);
  EXPECT_TRUE(matrix.x2);
  EXPECT_TRUE(matrix.shared);
  EXPECT_EQ(matrix.type, ScalarType::B16);
  ASSERT_EQ(matrix.dst.value.elements.size(), 2u);
  EXPECT_EQ(matrix.dst.value.elements[0]->declared_type, ScalarType::B32);
  EXPECT_EQ(matrix.dst.value.elements[1]->declared_type, ScalarType::B32);
  const checker::Context context{
      .target = {.ptx_version = {6, 5}, .sm_version = 75},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(instruction, context).has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 4}, .sm_version = 75},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 74},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .b16 global_value;
.entry kernel() { .reg .b32 %r<2>; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_address.has_value())
      << wrong_address.error().front().message;
  const auto address_check = checker::check(
      std::get<Ldmatrix>(wrong_address->functions.front().body.front()), context);
  ASSERT_FALSE(address_check.has_value());
  EXPECT_EQ(address_check.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 16 .b16 shared_value[16];
.entry kernel() {
  .reg .b32 %r<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value+16];
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value+8];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto unaligned_address = resolveModule(*parsed_module_3);
  ASSERT_TRUE(unaligned_address.has_value())
      << unaligned_address.error().front().message;
  const auto& alignment_body = unaligned_address->functions.front().body;
  EXPECT_TRUE(checker::check(std::get<Ldmatrix>(alignment_body[0]), context)
                  .has_value());
  const auto unaligned_check =
      checker::check(std::get<Ldmatrix>(alignment_body[1]), context);
  ASSERT_FALSE(unaligned_check.has_value());
  EXPECT_EQ(unaligned_check.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.shared .b16 shared_value;
.entry kernel() { .reg .b16 %r<2>; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_register = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_register.has_value());

  for (const auto source : {
           ".entry kernel() { .reg .b32 %r<3>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0}, [x]; }",
           ".entry kernel() { .reg .b32 %r<3>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1, %r2}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m16n16.x2.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x1.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.trans.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.m8n8.x2.shared.b16 {%r0, %r1}, [x]; }",
       }) {
    const auto parsed_module_5 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
    ASSERT_FALSE(resolveModule(*parsed_module_5).has_value()) << source;
  }
}

TEST(ResolvedModule, ResolvesAndChecksMmaSyncAlignedM16n8k8RowColSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %d<4>;
  .reg .f32 %c<4>;
  .reg .f16x2 %a<2>;
  .reg .f16x2 %b<1>;
  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32
    {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Mma>(resolved->functions.front().body.front());
  const auto& mma =
      std::get<Mma::SyncAlignedM16n8k8RowColF32F16F16F32>(instruction.variant);
  EXPECT_TRUE(mma.sync);
  EXPECT_TRUE(mma.aligned);
  EXPECT_TRUE(mma.m16n8k8);
  EXPECT_TRUE(mma.row);
  EXPECT_TRUE(mma.col);
  EXPECT_EQ(mma.d_type, ScalarType::F32);
  EXPECT_EQ(mma.a_type, ScalarType::F16);
  EXPECT_EQ(mma.b_type, ScalarType::F16);
  EXPECT_EQ(mma.c_type, ScalarType::F32);
  ASSERT_EQ(mma.dst.value.elements.size(), 4u);
  ASSERT_EQ(mma.a.value.elements.size(), 2u);
  ASSERT_EQ(mma.b.value.elements.size(), 1u);
  ASSERT_EQ(mma.c.value.elements.size(), 4u);
  EXPECT_EQ(mma.dst.value.elements[0]->declared_type, ScalarType::F32);
  EXPECT_EQ(mma.a.value.elements[0]->declared_type, ScalarType::F16x2);
  EXPECT_EQ(mma.b.value.elements[0]->declared_type, ScalarType::F16x2);
  EXPECT_EQ(mma.c.value.elements[0]->declared_type, ScalarType::F32);

  const checker::Context context{
      .target = {.ptx_version = {6, 5}, .sm_version = 75},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(instruction, context).has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 4}, .sm_version = 75},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 74},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  for (const auto source : {
           ".entry kernel() { .reg .f32 %d<3>; .reg .f32 %c<4>; .reg .f16x2 %a<2>; .reg .f16x2 %b<1>; mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3}; }",
           ".entry kernel() { .reg .f32 %d<4>; .reg .f32 %c<4>; .reg .f16 %a<2>; .reg .f16x2 %b<1>; mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3}; }",
           ".entry kernel() { .reg .f32 %d<4>; .reg .f32 %c<4>; .reg .f16x2 %a<2>; .reg .f16x2 %b0; mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2, %d3}, {%a0, %a1}, %b0, {%c0, %c1, %c2, %c3}; }",
           ".entry kernel() { .reg .f32 %d<4>; .reg .f32 %c<3>; .reg .f16x2 %a<2>; .reg .f16x2 %b<1>; mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2}; }",
           ".entry kernel() { .reg .f32 %d<4>; .reg .f32 %c<4>; .reg .f16x2 %a<2>; .reg .f16x2 %b<1>; mma.sync.aligned.m16n8k8.row.f32.f16.f16.f32 {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3}; }",
       }) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    ASSERT_FALSE(resolveModule(*parsed_module_2).has_value()) << source;
  }
}

TEST(ResolvedModule, ResolvesAndChecksMembarCtaSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { membar.cta; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Membar>(resolved->functions.front().body.front());
  const auto& membar = std::get<Membar::Cta>(instruction.variant);
  EXPECT_EQ(membar.scope, MemoryScope::Cta);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {1, 4}, .sm_version = 0},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 3}, .sm_version = 0},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old.has_value());
  EXPECT_EQ(too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { membar.gl; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_gl = resolveModule(*parsed_module_2);
  ASSERT_FALSE(wrong_gl.has_value());
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { membar.sys; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_sys = resolveModule(*parsed_module_3);
  ASSERT_FALSE(wrong_sys.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { membar.cta 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto extra_operand = resolveModule(*parsed_module_4);
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksFenceAcqRelCtaSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { fence.acq_rel.cta; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Fence>(resolved->functions.front().body.front());
  const auto& fence = std::get<Fence::AcqRelCta>(instruction.variant);
  EXPECT_EQ(fence.semantics, MemoryConsistency::AcqRel);
  EXPECT_EQ(fence.scope, MemoryScope::Cta);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 70},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { fence.acquire.cta; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_semantics = resolveModule(*parsed_module_2);
  ASSERT_FALSE(wrong_semantics.has_value());
  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { fence.acq_rel.sys; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_scope = resolveModule(*parsed_module_3);
  ASSERT_FALSE(wrong_scope.has_value());
  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { fence.acq_rel.cta 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto extra_operand = resolveModule(*parsed_module_4);
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksModernFenceProxySlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 16 .b8 global_value[128];
.entry kernel() {
  fence.proxy.async;
  fence.proxy.async.global;
  fence.proxy.async.shared::cta;
  fence.proxy.async.shared::cluster;
  fence.proxy.tensormap::generic.release.cta;
  fence.proxy.tensormap::generic.release.gpu;
  fence.proxy.tensormap::generic.release.sys;
  fence.proxy.tensormap::generic.release.cluster;
  fence.proxy.tensormap::generic.acquire.cta [global_value], 128;
  fence.proxy.tensormap::generic.acquire.gpu [global_value], 128;
  fence.proxy.tensormap::generic.acquire.sys [global_value], 128;
  fence.proxy.tensormap::generic.acquire.cluster [global_value], 128;
  fence.proxy.async::generic.acquire.sync_restrict::shared::cluster.cluster;
  fence.proxy.async::generic.release.sync_restrict::shared::cta.cluster;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 14u);
  const auto& async = std::get<Fence::ProxyAsync>(std::get<Fence>(body[0]).variant);
  EXPECT_EQ(async.proxy_kind.value, AsyncProxyKind::Async);
  EXPECT_EQ(std::get<Fence::ProxyAsync>(std::get<Fence>(body[2]).variant)
                .proxy_kind.value,
            AsyncProxyKind::AsyncSharedCta);
  EXPECT_EQ(std::get<Fence::ProxyAsyncSharedCluster>(
                std::get<Fence>(body[3]).variant)
                .proxy_kind.value,
            AsyncProxyKind::AsyncSharedCluster);
  const auto& acquire = std::get<Fence::ProxyTensormapGenericAcquire>(
      std::get<Fence>(body[9]).variant);
  EXPECT_EQ(acquire.proxy_pair.value, ProxyKindPair::TensormapToGeneric);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedSymbolRef>(acquire.address.value.base));
  const auto& restricted =
      std::get<Fence::ProxyAsyncGenericAcquireSyncRestrictSharedCluster>(
          std::get<Fence>(body[12]).variant);
  EXPECT_EQ(restricted.proxy_pair.value, ProxyKindPair::AsyncToGeneric);

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = {.ptx_version = {8, 6},
                 .sm_version = 90,
                 .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Fence>(instruction), supported).has_value());

  const auto old_pair = checker::check(
      std::get<Fence>(body[4]),
      checker::Context{.target = {.ptx_version = {8, 2}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_pair.has_value());
  EXPECT_EQ(old_pair.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sync_restrict = checker::check(
      std::get<Fence>(body[12]),
      checker::Context{.target = {.ptx_version = {8, 5},
                                  .sm_version = 90,
                                  .capabilities = cluster_capabilities},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sync_restrict.has_value());
  EXPECT_EQ(old_sync_restrict.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = checker::check(
      std::get<Fence>(body[0]),
      checker::Context{.target = {.ptx_version = {8, 0}, .sm_version = 89},
                       .instruction_range = ast.range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  const auto no_cluster = checker::check(
      std::get<Fence>(body[3]),
      checker::Context{.target = {.ptx_version = {8, 0}, .sm_version = 90},
                       .instruction_range = ast.range});
  ASSERT_FALSE(no_cluster.has_value());
  EXPECT_EQ(no_cluster.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto parsed_module_2 = parseModule(R"ptx(
.shared .align 16 .b8 shared_value[128];
.entry kernel() { fence.proxy.tensormap::generic.acquire.gpu [shared_value], 128; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_address.has_value());
  const auto wrong_address_checked = checker::check(
      std::get<Fence>(wrong_address->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_address_checked.has_value());
  EXPECT_EQ(wrong_address_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 16 .b8 global_value[128];
.entry kernel() { fence.proxy.tensormap::generic.acquire.gpu [global_value], 64; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_size = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_size.has_value());
  const auto wrong_size_checked = checker::check(
      std::get<Fence>(wrong_size->functions.front().body.front()), supported);
  ASSERT_FALSE(wrong_size_checked.has_value());
  EXPECT_EQ(wrong_size_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %rd0; fence.proxy.tensormap::generic.release.gpu [%rd0], 128; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto extra_operand = resolveModule(*parsed_module_4);
  EXPECT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksClusterlaunchcontrolTryCancelSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.shared .align 16 .b8 response[16];
.shared .align 8 .b8 barrier[8];
.entry kernel() {
  clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier];
  clusterlaunchcontrol.try_cancel.async.shared::cta.mbarrier::complete_tx::bytes.b128 [response], [barrier];
  clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.multicast::cluster::all.b128 [response], [barrier];
  clusterlaunchcontrol.try_cancel.async.shared::cta.mbarrier::complete_tx::bytes.multicast::cluster::all.b128 [response], [barrier];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::TryCancelAsyncGeneric>(
      std::get<Clusterlaunchcontrol>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::TryCancelAsyncSharedCta>(
      std::get<Clusterlaunchcontrol>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<
      Clusterlaunchcontrol::TryCancelAsyncMulticastGeneric>(
      std::get<Clusterlaunchcontrol>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<
      Clusterlaunchcontrol::TryCancelAsyncMulticastSharedCta>(
      std::get<Clusterlaunchcontrol>(body[3]).variant));

  const auto context_for = [&ast](std::string_view target,
                                  checker::PtxVersion ptx_version) {
    const auto profile = base::find_target_profile(target);
    EXPECT_TRUE(profile.has_value()) << target;
    return checker::Context{
        .target = {.ptx_version = ptx_version,
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
        .instruction_range = ast.range,
    };
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(instruction),
                               context_for("sm_100a", {8, 6}))
                    .has_value());
  }
  EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                             context_for("sm_100f", {8, 8}))
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                             context_for("sm_120a", {8, 6}))
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                             context_for("sm_120f", {8, 8}))
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                             context_for("sm_110a", {9, 0}))
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                             context_for("sm_110f", {9, 0}))
                  .has_value());

  for (const auto target : {"sm_100", "sm_110", "sm_120"}) {
    SCOPED_TRACE(target);
    EXPECT_FALSE(checker::check(std::get<Clusterlaunchcontrol>(body[2]),
                                context_for(target, {9, 0}))
                     .has_value());
  }
  const auto generic_sm100_multicast = checker::check(
      std::get<Clusterlaunchcontrol>(body[2]), context_for("sm_100", {8, 6}));
  ASSERT_FALSE(generic_sm100_multicast.has_value());
  EXPECT_EQ(generic_sm100_multicast.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  EXPECT_FALSE(checker::check(std::get<Clusterlaunchcontrol>(body[0]),
                              context_for("sm_90", {9, 0}))
                   .has_value());
  EXPECT_FALSE(checker::check(std::get<Clusterlaunchcontrol>(body[0]),
                              context_for("sm_100", {8, 5}))
                   .has_value());
  const auto no_cluster = checker::check(
      std::get<Clusterlaunchcontrol>(body[0]),
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100},
                       .instruction_range = ast.range});
  ASSERT_FALSE(no_cluster.has_value());
  EXPECT_EQ(no_cluster.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto parsed_module_2 = parseModule(R"ptx(
.global .align 16 .b8 response[16];
.shared .align 8 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_address.has_value());
  const auto wrong_address_checked = checker::check(
      std::get<Clusterlaunchcontrol>(wrong_address->functions.front().body.front()),
      context_for("sm_100a", {8, 6}));
  ASSERT_FALSE(wrong_address_checked.has_value());
  EXPECT_EQ(wrong_address_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.shared .align 8 .b8 response[16];
.shared .align 8 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_response_alignment = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_response_alignment.has_value());
  const auto response_alignment_checked = checker::check(
      std::get<Clusterlaunchcontrol>(wrong_response_alignment->functions.front().body.front()),
      context_for("sm_100a", {8, 6}));
  ASSERT_FALSE(response_alignment_checked.has_value());
  EXPECT_EQ(response_alignment_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.shared .align 16 .b8 response[16];
.shared .align 4 .b8 barrier[8];
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [response], [barrier]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_mbarrier_alignment = resolveModule(*parsed_module_4);
  ASSERT_TRUE(wrong_mbarrier_alignment.has_value());
  const auto mbarrier_alignment_checked = checker::check(
      std::get<Clusterlaunchcontrol>(wrong_mbarrier_alignment->functions.front().body.front()),
      context_for("sm_100a", {8, 6}));
  ASSERT_FALSE(mbarrier_alignment_checked.has_value());
  EXPECT_EQ(mbarrier_alignment_checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 [%rd0]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  EXPECT_FALSE(resolveModule(*parsed_module_5).has_value());
}

TEST(ResolvedModule, ResolvesAndChecksClusterlaunchcontrolQueryCancelSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p0;
  .reg .b32 %r<8>;
  .reg .u32 %u0;
  .reg .s32 %s0;
  .reg .b128 %q0;
  clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %p0, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b32.b128 {%r0, %r1, %r2, %r3}, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b32.b128 {%r4, _, _, _}, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid::x.b32.b128 %r5, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid::y.b32.b128 %r6, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid::z.b32.b128 %r7, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid::x.b32.b128 %u0, %q0;
  clusterlaunchcontrol.query_cancel.get_first_ctaid::y.b32.b128 %s0, %q0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelIsCanceledPred>(
      std::get<Clusterlaunchcontrol>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidV4>(
      std::get<Clusterlaunchcontrol>(body[1]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidV4>(
      std::get<Clusterlaunchcontrol>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidX>(
      std::get<Clusterlaunchcontrol>(body[3]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidY>(
      std::get<Clusterlaunchcontrol>(body[4]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidZ>(
      std::get<Clusterlaunchcontrol>(body[5]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidX>(
      std::get<Clusterlaunchcontrol>(body[6]).variant));
  EXPECT_TRUE(std::holds_alternative<Clusterlaunchcontrol::QueryCancelGetFirstCtaidY>(
      std::get<Clusterlaunchcontrol>(body[7]).variant));

  const auto context_for = [&ast](std::string_view target,
                                  checker::PtxVersion ptx_version) {
    const auto profile = base::find_target_profile(target);
    EXPECT_TRUE(profile.has_value()) << target;
    return checker::Context{
        .target = {.ptx_version = ptx_version,
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
        .instruction_range = ast.range,
    };
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(checker::check(std::get<Clusterlaunchcontrol>(instruction),
                               context_for("sm_100a", {8, 6}))
                    .has_value());
  }

  const auto too_old = checker::check(
      std::get<Clusterlaunchcontrol>(body[0]), context_for("sm_100a", {8, 5}));
  ASSERT_FALSE(too_old.has_value());
  EXPECT_EQ(too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  const auto too_small = checker::check(
      std::get<Clusterlaunchcontrol>(body[0]), context_for("sm_90", {8, 6}));
  ASSERT_FALSE(too_small.has_value());
  EXPECT_EQ(too_small.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  const auto no_cluster = checker::check(
      std::get<Clusterlaunchcontrol>(body[0]),
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100},
                       .instruction_range = ast.range});
  ASSERT_FALSE(no_cluster.has_value());
  EXPECT_EQ(no_cluster.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  for (const std::string_view source : {
           ".entry kernel() { .reg .pred %p0; clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %p0, 1; }",
           ".entry kernel() { .reg .b32 %r<3>; .reg .b128 %q0; clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b32.b128 {%r0, %r1, %r2}, %q0; }",
           ".entry kernel() { .reg .b32 %r0; .reg .b128 %q0; clusterlaunchcontrol.query_cancel.get_first_ctaid::x.b32.b128 _, %q0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    EXPECT_FALSE(resolveModule(*parsed_module_2).has_value());
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %r0;
  .reg .b128 %q0;
  clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %r0, %q0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_status = resolveModule(*parsed_module_3);
  EXPECT_FALSE(wrong_status.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksAtomGlobalRelaxedCtaAddU32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.relaxed.cta.global.add.u32 %r0, [global_value], %r1;
  atom.global.relaxed.cta.add.u32 %r0, [global_value], %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2U);
  const auto& instruction = std::get<Atom>(body.front());
  const auto& legacy_instruction = std::get<Atom>(body.back());
  const auto& atom =
      std::get<Atom::GlobalRelaxedCtaAddU32>(instruction.variant);
  const auto& legacy_atom =
      std::get<Atom::GlobalRelaxedCtaAddU32>(legacy_instruction.variant);
  EXPECT_EQ(atom.state_space, MemoryStateSpace::Global);
  EXPECT_EQ(atom.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(atom.scope, MemoryScope::Cta);
  EXPECT_TRUE(atom.add);
  EXPECT_EQ(atom.type, ScalarType::U32);
  EXPECT_EQ(atom.dst.value.declared_type, ScalarType::U32);
  EXPECT_EQ(atom.src.value.declared_type, ScalarType::U32);
  EXPECT_EQ(legacy_atom.state_space, atom.state_space);
  EXPECT_EQ(legacy_atom.semantics, atom.semantics);
  EXPECT_EQ(legacy_atom.scope, atom.scope);
  EXPECT_EQ(legacy_atom.add, atom.add);
  EXPECT_EQ(legacy_atom.type, atom.type);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 70},
                      .instruction_range = ast.range,
                  })
                  .has_value());
  EXPECT_TRUE(checker::check(
                  legacy_instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 70},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.add.u32 %r0, [local_value], %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto local_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Atom>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u64 %wide<2>;
  .reg .u16 %h<2>;
  .reg .u32 %r0;
  atom.global.relaxed.cta.add.u32 %wide0, [global_value], %r0;
  atom.global.relaxed.cta.add.u32 %r0, [global_value], %wide1;
  atom.global.relaxed.cta.add.u32 %h0, [global_value], %r0;
  atom.global.relaxed.cta.add.u32 %r0, [global_value], %h1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto mismatched_registers = resolveModule(*parsed_module_3);
  ASSERT_TRUE(mismatched_registers.has_value())
      << mismatched_registers.error().front().message;
  for (const auto& candidate : mismatched_registers->functions.front().body) {
    const auto checked = checker::check(
        std::get<Atom>(candidate),
        checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.and.u32 %r0, [global_value], %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_operation = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_operation.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.global.acquire.cta.add.u32 %r0, [global_value], %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto wrong_ordering = resolveModule(*parsed_module_5);
  ASSERT_FALSE(wrong_ordering.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  atom.global.relaxed.cta.add.u32 %r0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto missing_operand = resolveModule(*parsed_module_6);
  ASSERT_FALSE(missing_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksRedGlobalRelaxedCtaAddU32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.relaxed.cta.global.add.u32 [global_value], %r0;
  red.global.relaxed.cta.add.u32 [global_value], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2U);
  const auto& instruction = std::get<Red>(body.front());
  const auto& legacy_instruction = std::get<Red>(body.back());
  const auto& red = std::get<Red::GlobalRelaxedCtaAddU32>(instruction.variant);
  const auto& legacy_red =
      std::get<Red::GlobalRelaxedCtaAddU32>(legacy_instruction.variant);
  EXPECT_EQ(red.state_space, MemoryStateSpace::Global);
  EXPECT_EQ(red.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(red.scope, MemoryScope::Cta);
  EXPECT_TRUE(red.add);
  EXPECT_EQ(red.type, ScalarType::U32);
  EXPECT_EQ(red.src.value.declared_type, ScalarType::U32);
  EXPECT_EQ(legacy_red.state_space, red.state_space);
  EXPECT_EQ(legacy_red.semantics, red.semantics);
  EXPECT_EQ(legacy_red.scope, red.scope);
  EXPECT_EQ(legacy_red.add, red.add);
  EXPECT_EQ(legacy_red.type, red.type);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 70},
                      .instruction_range = ast.range,
                  })
                  .has_value());
  EXPECT_TRUE(checker::check(
                  legacy_instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 70},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r0;
  red.global.relaxed.cta.add.u32 [local_value], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto local_address = resolveModule(*parsed_module_2);
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Red>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  .reg .u64 %wide0;
  red.global.relaxed.cta.add.u32 [global_value], %h0;
  red.global.relaxed.cta.add.u32 [global_value], %wide0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto mismatched_sources = resolveModule(*parsed_module_3);
  ASSERT_TRUE(mismatched_sources.has_value())
      << mismatched_sources.error().front().message;
  for (const auto& candidate : mismatched_sources->functions.front().body) {
    const auto checked = checker::check(
        std::get<Red>(candidate),
        checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.global.relaxed.cta.and.u32 [global_value], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_operation = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_operation.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.global.acquire.cta.add.u32 [global_value], %r0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto wrong_ordering = resolveModule(*parsed_module_5);
  ASSERT_FALSE(wrong_ordering.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  red.global.relaxed.cta.add.u32 %r0, [global_value], %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto unexpected_dst = resolveModule(*parsed_module_6);
  ASSERT_FALSE(unexpected_dst.has_value());
  const auto parsed_module_7 = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { red.global.relaxed.cta.add.u32 [global_value]; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto missing_source = resolveModule(*parsed_module_7);
  ASSERT_FALSE(missing_source.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksActivemaskB32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  activemask.b32 %b0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Activemask>(resolved->functions.front().body.front());
  const auto& activemask = std::get<Activemask::B32>(instruction.variant);
  EXPECT_EQ(activemask.type, ScalarType::B32);
  EXPECT_EQ(activemask.dst.value.declared_type, ScalarType::B32);
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{
                      .target = {.ptx_version = {6, 2}, .sm_version = 30},
                      .instruction_range = ast.range,
                  })
                  .has_value());

  const auto too_old_ptx = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 1}, .sm_version = 30},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {6, 2}, .sm_version = 29},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  activemask.b32 %u0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto compatible_dst = resolveModule(*parsed_module_2);
  ASSERT_TRUE(compatible_dst.has_value())
      << compatible_dst.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Activemask>(compatible_dst->functions.front().body.front()),
                  checker::Context{.target = {.ptx_version = {6, 2}, .sm_version = 30}})
                  .has_value());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .b64 %wide0;
  activemask.b32 %wide0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto mismatched_dsts = resolveModule(*parsed_module_3);
  ASSERT_TRUE(mismatched_dsts.has_value())
      << mismatched_dsts.error().front().message;
  for (const auto& candidate : mismatched_dsts->functions.front().body) {
    const auto checked = checker::check(
        std::get<Activemask>(candidate),
        checker::Context{.target = {.ptx_version = {6, 2}, .sm_version = 30}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; activemask.u32 %b0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_type = resolveModule(*parsed_module_4);
  ASSERT_FALSE(wrong_type.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b<2>; activemask.b32 %b0, %b1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto extra_operand = resolveModule(*parsed_module_5);
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksVoteSyncBallotB32Slice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  .reg .u32 %mask;
  vote.sync.ballot.b32 %b0, %p0, 0xffffffff;
  vote.sync.ballot.b32 %b1, %p0, %mask;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& immediate =
      std::get<Vote::SyncBallotB32>(std::get<Vote>(body[0]).variant);
  const auto& register_mask =
      std::get<Vote::SyncBallotB32>(std::get<Vote>(body[1]).variant);
  EXPECT_TRUE(immediate.sync);
  EXPECT_TRUE(immediate.ballot);
  EXPECT_EQ(immediate.type, ScalarType::B32);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(immediate.membermask.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(register_mask.membermask.value));
  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 30},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Vote>(body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Vote>(body[1]), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Vote>(body[0]),
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 30},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Vote>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 29},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  .reg .b64 %wide0;
  .reg .pred %p0;
  .reg .b64 %bad_mask;
  vote.sync.ballot.b32 %wide0, %p0, 0;
  vote.sync.ballot.b32 %u0, %p0, %bad_mask;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bad_dst_and_mask = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bad_dst_and_mask.has_value())
      << bad_dst_and_mask.error().front().message;
  for (const auto& candidate : bad_dst_and_mask->functions.front().body) {
    const auto checked = checker::check(
        std::get<Vote>(candidate), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  .reg .pred %p0;
  vote.sync.ballot.b32 %u0, %p0, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto compatible_dst = resolveModule(*parsed_module_3);
  ASSERT_TRUE(compatible_dst.has_value())
      << compatible_dst.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Vote>(compatible_dst->functions.front().body.front()), context)
                  .has_value());

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .u32 %r0;
  vote.sync.ballot.b32 %b0, %r0, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto bad_predicate = resolveModule(*parsed_module_4);
  ASSERT_FALSE(bad_predicate.has_value());
  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.all.pred %b0, %p0, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto wrong_all = resolveModule(*parsed_module_5);
  ASSERT_FALSE(wrong_all.has_value());
  const auto parsed_module_6 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.any.pred %b0, %p0, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto wrong_any = resolveModule(*parsed_module_6);
  ASSERT_FALSE(wrong_any.has_value());
  const auto parsed_module_7 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.ballot.b32 %b0, %p0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_7);
  const auto legacy = resolveModule(*parsed_module_7);
  ASSERT_FALSE(legacy.has_value());
  const auto parsed_module_8 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.ballot.b32 %b0, %p0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_8);
  const auto missing_mask = resolveModule(*parsed_module_8);
  ASSERT_FALSE(missing_mask.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksBarWarpSyncSlice) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %mask;
  bar.warp.sync 0xffffffff;
  bar.warp.sync %mask;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& immediate =
      std::get<Bar::WarpSync>(std::get<Bar>(body[0]).variant);
  const auto& register_mask =
      std::get<Bar::WarpSync>(std::get<Bar>(body[1]).variant);
  EXPECT_TRUE(immediate.warp);
  EXPECT_TRUE(immediate.sync);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(immediate.membermask.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      register_mask.membermask.value));
  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 30},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Bar>(body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Bar>(body[1]), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Bar>(body[0]),
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 30},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Bar>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 29},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .b64 %wide;
  bar.warp.sync %wide;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bad_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bad_width.has_value()) << bad_width.error().front().message;
  const auto bad_check = checker::check(
      std::get<Bar>(bad_width->functions.front().body.front()), context);
  ASSERT_FALSE(bad_check.has_value());
  EXPECT_EQ(bad_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { bar.warp.sync 1, 2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto extra_operand = resolveModule(*parsed_module_3);
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksBarrierClusterSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  barrier.cluster.arrive;
  barrier.cluster.arrive.aligned;
  barrier.cluster.arrive.release.aligned;
  barrier.cluster.arrive.relaxed;
  barrier.cluster.wait;
  barrier.cluster.wait.aligned;
  barrier.cluster.wait.acquire.aligned;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 7u);
  const auto& arrive =
      std::get<Barrier::ClusterArrive>(std::get<Barrier>(body[0]).variant);
  const auto& wait =
      std::get<Barrier::ClusterWait>(std::get<Barrier>(body[4]).variant);
  EXPECT_EQ(Barrier::ClusterArrive::scope, MemoryScope::Cluster);
  EXPECT_TRUE(Barrier::ClusterArrive::arrive);
  EXPECT_EQ(arrive.semantics.value, MemoryConsistency::Release);
  EXPECT_TRUE(arrive.semantics.locs.empty());
  EXPECT_FALSE(arrive.aligned.value);
  EXPECT_EQ(Barrier::ClusterWait::scope, MemoryScope::Cluster);
  EXPECT_TRUE(Barrier::ClusterWait::wait);
  EXPECT_EQ(wait.semantics.value, MemoryConsistency::Acquire);
  EXPECT_TRUE(wait.semantics.locs.empty());
  EXPECT_FALSE(wait.aligned.value);
  EXPECT_FALSE(std::get<Barrier::ClusterArrive>(
                   std::get<Barrier>(body[2]).variant)
                   .semantics.locs.empty());
  EXPECT_FALSE(std::get<Barrier::ClusterWait>(
                   std::get<Barrier>(body[6]).variant)
                   .semantics.locs.empty());

  const auto context_for = [&ast](std::string_view target,
                                  checker::PtxVersion ptx_version) {
    const auto profile = base::find_target_profile(target);
    EXPECT_TRUE(profile.has_value()) << target;
    return checker::Context{
        .target = {.ptx_version = ptx_version,
                   .sm_version = profile->identity.architecture.number,
                   .enabled_family_features = profile->enabled_family_features,
                   .identity = profile->identity,
                   .capabilities = profile->capabilities},
        .instruction_range = ast.range,
    };
  };
  for (const std::string_view target : {"sm_90a", "sm_100"}) {
    SCOPED_TRACE(target);
    for (const auto& instruction : body) {
      EXPECT_TRUE(checker::check(std::get<Barrier>(instruction),
                                 context_for(target, {8, 0}))
                      .has_value());
    }
  }

  for (const auto index : {0u, 1u, 4u, 5u}) {
    SCOPED_TRACE(index);
    EXPECT_TRUE(checker::check(std::get<Barrier>(body[index]),
                               context_for("sm_90a", {7, 8}))
                    .has_value());
  }
  EXPECT_FALSE(checker::check(std::get<Barrier>(body.front()),
                              context_for("sm_80", {8, 0}))
                   .has_value());
  const checker::Context missing_cluster{
      .target = {.ptx_version = {8, 0}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  EXPECT_FALSE(checker::check(std::get<Barrier>(body.front()), missing_cluster)
                   .has_value());
  const auto too_old = checker::check(
      std::get<Barrier>(body.front()), context_for("sm_90a", {7, 7}));
  ASSERT_FALSE(too_old.has_value());
  for (const auto index : {2u, 3u, 6u}) {
    SCOPED_TRACE(index);
    const auto explicit_semantics = checker::check(
        std::get<Barrier>(body[index]), context_for("sm_90a", {7, 8}));
    ASSERT_FALSE(explicit_semantics.has_value());
    EXPECT_EQ(explicit_semantics.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  }

  for (const std::string_view source : {
           ".entry kernel() { barrier.cluster.arrive 1; }",
           ".entry kernel() { barrier.cluster.wait.release; }",
           ".entry kernel() { barrier.cluster.arrive.aligned.release; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    EXPECT_FALSE(resolveModule(*parsed_module_2).has_value());
  }
}

TEST(ResolvedModule, ResolvesAndChecksMatchSyncSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<5>;
  .reg .b64 %d<2>;
  .reg .u32 %mask;
  .reg .pred %p0;
  match.any.sync.b32 %b0, %b1, 0xffffffff;
  match.any.sync.b64 %b2, %d0, %mask;
  match.all.sync.b32 %b3, %b4, %mask;
  match.all.sync.b64 %b4|%p0, %d1, 0xffffffff;
}

)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto& any_immediate =
      std::get<Match::AnySync>(std::get<Match>(body[0]).variant);
  const auto& any_register =
      std::get<Match::AnySync>(std::get<Match>(body[1]).variant);
  const auto& all_plain =
      std::get<Match::AllSync>(std::get<Match>(body[2]).variant);
  const auto& all_pair =
      std::get<Match::AllSync>(std::get<Match>(body[3]).variant);
  EXPECT_EQ(any_immediate.type.value, ScalarType::B32);
  EXPECT_EQ(any_register.type.value, ScalarType::B64);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      any_immediate.membermask.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      any_register.membermask.value));
  EXPECT_TRUE(std::holds_alternative<
      Match::AllSync::WithoutPredicateOperands>(all_plain.operands));
  const auto& paired = std::get<Match::AllSync::WithPredicateOperands>(
      all_pair.operands);
  ASSERT_TRUE(paired.dst.value.data.has_value());
  ASSERT_TRUE(paired.dst.value.predicate.has_value());
  EXPECT_EQ(paired.dst.value.data->value.declared_type, ScalarType::B32);
  EXPECT_EQ(paired.dst.value.predicate->value.register_ref.declared_type,
            ScalarType::Pred);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(paired.membermask.value));

  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 70},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(checker::check(std::get<Match>(instruction), context).has_value());
  }
  const auto too_old_ptx = checker::check(
      std::get<Match>(body.front()),
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 70},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Match>(body.front()),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 69},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  for (const std::string_view source : {
           ".entry kernel() { .reg .b64 %d0; .reg .b32 %b0; match.any.sync.b32 %d0, %b0, 0; }",
           ".entry kernel() { .reg .b32 %b<2>; match.any.sync.b64 %b0, %b1, 0; }",
           ".entry kernel() { .reg .b32 %b<2>; .reg .b64 %d0; match.any.sync.b32 %b0, %b1, %d0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto invalid = resolveModule(*parsed_module_2);
    ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
    const auto checked = checker::check(
        std::get<Match>(invalid->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
  for (const std::string_view source : {
           ".entry kernel() { .reg .b32 %b<2>; .reg .pred %p0; match.any.sync.b32 %b0|%p0, %b1, 0; }",
           ".entry kernel() { .reg .b32 %b<2>; .reg .u32 %u0; match.all.sync.b32 %b0|%u0, %b1, 0; }",
           ".entry kernel() { .reg .b32 %b<2>; .reg .pred %p0; match.all.sync.b32 %b0|%p0, %b1; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    EXPECT_FALSE(resolveModule(*parsed_module_3).has_value());
  }
}

TEST(ResolvedModule, ResolvesMatchSyncSinksAndReportsExactRejectedRanges) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  match.all.sync.b32 _, %b1, 0xffffffff;
  match.all.sync.b32 _|%p0, %b1, 0xffffffff;
  match.all.sync.b32 %b0|_, %b1, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& all_data_sink = std::get<Match>(body[0]);
  const auto& single_sink = std::get<Match>(body[1]);
  const auto& shfl_predicate_sink = std::get<Match>(body[2]);
  const auto& all_data_operands = std::get<Match::AllSync::WithoutPredicateOperands>(
      std::get<Match::AllSync>(all_data_sink.variant).operands);
  const auto& single_sink_operands = std::get<Match::AllSync::WithPredicateOperands>(
      std::get<Match::AllSync>(single_sink.variant).operands);
  const auto& shfl_predicate_operands =
      std::get<Match::AllSync::WithPredicateOperands>(
          std::get<Match::AllSync>(shfl_predicate_sink.variant).operands);
  EXPECT_FALSE(all_data_operands.dst.value.register_ref.has_value());
  EXPECT_FALSE(single_sink_operands.dst.value.data.has_value());
  EXPECT_TRUE(single_sink_operands.dst.value.predicate.has_value());
  EXPECT_TRUE(shfl_predicate_operands.dst.value.data.has_value());
  EXPECT_FALSE(shfl_predicate_operands.dst.value.predicate.has_value());

  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 70},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Match>(instruction), context).has_value());

  const auto reject = [&](std::string_view source) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto& invalid_ast = *parsed_module_2;
    const auto& invalid_function =
        std::get<syntax_ast::AstFunction>(invalid_ast.items.back());
    const auto& invalid_instruction = std::get<syntax_ast::AstInstruction>(
        invalid_function.body.back());
    const auto invalid = resolveModule(invalid_ast);
    ASSERT_FALSE(invalid.has_value());
    ASSERT_FALSE(invalid.error().empty());
    EXPECT_EQ(invalid.error().front().range,
              sourceRange(invalid_instruction.operands.front()));
  };
  reject(R"ptx(
.entry kernel() { .reg .b32 %b<2>; match.all.sync.b32 _|_, %b1, 0xffffffff; }
)ptx");
  reject(R"ptx(
.entry kernel() { .reg .b32 %b<2>; match.any.sync.b32 _, %b1, 0xffffffff; }
)ptx");
}

TEST(ResolvedModule, ResolvesAndChecksReduxSyncSlices) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u<4>;
  .reg .s32 %s<2>;
  .reg .b32 %b<2>;
  .reg .f32 %f<4>;
  .reg .u32 %mask;
  redux.sync.add.u32 %u0, %u1, 0xffffffff;
  redux.sync.min.s32 %s0, %s1, %mask;
  redux.sync.max.u32 %u2, %u3, %mask;
  redux.sync.xor.b32 %b0, %b1, 0xffffffff;
  redux.sync.min.abs.NaN.f32 %f0, %f1, %mask;
  redux.sync.max.f32 %f2, %f3, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  const auto& add = std::get<Redux::SyncAdd>(std::get<Redux>(body[0]).variant);
  const auto& min = std::get<Redux::SyncMin>(std::get<Redux>(body[1]).variant);
  const auto& boolean =
      std::get<Redux::SyncBoolean>(std::get<Redux>(body[3]).variant);
  const auto& min_f32 =
      std::get<Redux::SyncMinF32>(std::get<Redux>(body[4]).variant);
  EXPECT_EQ(add.type.value, ScalarType::U32);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(add.membermask.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(min.membermask.value));
  EXPECT_EQ(boolean.operation.value, BooleanOperator::Xor);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      boolean.membermask.value));
  EXPECT_TRUE(min_f32.abs.value);
  EXPECT_TRUE(min_f32.nan.value);
  EXPECT_FALSE(min_f32.abs.locs.empty());
  EXPECT_FALSE(min_f32.nan.locs.empty());

  const checker::Context baseline_context{
      .target = {.ptx_version = {7, 0}, .sm_version = 80},
      .instruction_range = ast.range,
  };
  for (const auto index : {0u, 1u, 2u, 3u}) {
    EXPECT_TRUE(checker::check(std::get<Redux>(body[index]), baseline_context)
                    .has_value());
  }
  const auto too_old_ptx = checker::check(
      std::get<Redux>(body.front()),
      checker::Context{.target = {.ptx_version = {6, 9}, .sm_version = 80},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Redux>(body.front()),
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 79},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  for (const std::string_view source : {
           ".entry kernel() { .reg .b64 %d0; .reg .u32 %u0; redux.sync.add.u32 %d0, %u0, 0; }",
           ".entry kernel() { .reg .u32 %u0; .reg .b64 %d0; redux.sync.add.u32 %u0, %d0, 0; }",
           ".entry kernel() { .reg .u32 %u<2>; .reg .b64 %d0; redux.sync.add.u32 %u0, %u1, %d0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto invalid = resolveModule(*parsed_module_2);
    ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
    const auto checked = checker::check(
        std::get<Redux>(invalid->functions.front().body.front()), baseline_context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %f<2>;
  redux.sync.min.f32 %f0, %f1, 0xffffffff;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto& float_ast = *parsed_module_3;
  const auto float_resolved = resolveModule(float_ast);
  ASSERT_TRUE(float_resolved.has_value()) << float_resolved.error().front().message;
  const auto availability = [&float_resolved](std::string_view source) {
    const auto parsed = parseModule(source);
    if (!parsed || !parsed.diagnostics.empty()) {
      ADD_FAILURE() << (parsed.diagnostics.empty()
                            ? "PTX source did not produce a syntax module."
                            : parsed.diagnostics.front().message);
      return checker::CheckResult{
          std::unexpected(checker::CheckDiagnostics{})};
    }
    return checkModuleAvailability(*parsed, *float_resolved);
  };
  EXPECT_TRUE(availability(R"ptx(
.version 8.6
.target sm_100a
.entry kernel() {
  .reg .f32 %f<2>;
  redux.sync.min.f32 %f0, %f1, 0xffffffff;
}
)ptx")
                  .has_value());
  EXPECT_FALSE(availability(R"ptx(
.version 8.6
.target sm_100f
.entry kernel() {
  .reg .f32 %f<2>;
  redux.sync.min.f32 %f0, %f1, 0xffffffff;
}
)ptx")
                   .has_value());
  EXPECT_TRUE(availability(R"ptx(
.version 8.8
.target sm_100f
.entry kernel() {
  .reg .f32 %f<2>;
  redux.sync.min.f32 %f0, %f1, 0xffffffff;
}
)ptx")
                  .has_value());
  EXPECT_FALSE(availability(R"ptx(
.version 8.8
.target sm_100
.entry kernel() {
  .reg .f32 %f<2>;
  redux.sync.min.f32 %f0, %f1, 0xffffffff;
}
)ptx")
                   .has_value());
}

TEST(ResolvedModule, ResolvesAndChecksGriddepcontrolActions) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  griddepcontrol.launch_dependents;
  griddepcontrol.wait;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& ast = *parsed_module_1;
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Griddepcontrol::LaunchDependents>(
      std::get<Griddepcontrol>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Griddepcontrol::Wait>(
      std::get<Griddepcontrol>(body[1]).variant));

  const checker::Context context{
      .target = {.ptx_version = {7, 8}, .sm_version = 90},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    EXPECT_TRUE(
        checker::check(std::get<Griddepcontrol>(instruction), context).has_value());
  }
  for (const checker::Context unavailable : {
           checker::Context{.target = {.ptx_version = {7, 7}, .sm_version = 90},
                            .instruction_range = ast.range},
           checker::Context{.target = {.ptx_version = {7, 8}, .sm_version = 89},
                            .instruction_range = ast.range},
       }) {
    SCOPED_TRACE(unavailable.target.ptx_version.minor);
    EXPECT_FALSE(
        checker::check(std::get<Griddepcontrol>(body.front()), unavailable).has_value());
  }
  for (const std::string_view source : {
           ".entry kernel() { griddepcontrol; }",
           ".entry kernel() { griddepcontrol.wait.launch_dependents; }",
           ".entry kernel() { griddepcontrol.wait 0; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    EXPECT_FALSE(resolveModule(*parsed_module_2).has_value());
  }
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir
