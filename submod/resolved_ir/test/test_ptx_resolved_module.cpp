#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.diagnostics.front().message;
  return std::move(*module);
}

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

TEST(ResolvedModule, CarriesFunctionAndRegisterSymbolIdentity) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u32 %named;
  add.u32 %named, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const ResolvedFunction& function = resolved->functions.front();
  EXPECT_EQ(function.name, "kernel");
  EXPECT_TRUE(function.is_entry);
  EXPECT_FALSE(function.is_prototype);
  EXPECT_EQ(resolved->symbols.symbol(function.symbol_id).name, "kernel");
  EXPECT_FALSE(
      resolved->symbols.symbol(function.symbol_id).parameterized_count);
  EXPECT_TRUE(resolved->symbols.symbol(function.symbol_id).function_is_entry);
  ASSERT_EQ(function.body.size(), 1u);

  const Add::IntegerNoSat& add = resolvedIntegerAdd(function.body.front());
  EXPECT_FALSE(std::get<Add>(function.body.front()).execution_predicate);
  const ResolvedRegisterRef& dst = add.dst.value;
  const auto& src1 = std::get<ResolvedRegisterRef>(add.src1.value);
  const auto& src2 = std::get<ResolvedRegisterRef>(add.src2.value);

  ASSERT_TRUE(dst.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*dst.symbol_id).name, "%named");
  EXPECT_FALSE(dst.index.has_value());
  EXPECT_FALSE(dst.parameterized_index.has_value());
  EXPECT_EQ(dst.declared_type, ScalarType::U32);

  ASSERT_TRUE(src1.symbol_id.has_value());
  ASSERT_TRUE(src2.symbol_id.has_value());
  EXPECT_EQ(src1.symbol_id, src2.symbol_id);
  EXPECT_EQ(resolved->symbols.symbol(*src1.symbol_id).name, "%r");
  EXPECT_EQ(src1.index, 1u);
  EXPECT_EQ(src2.index, 2u);
  EXPECT_EQ(src1.parameterized_index, 1u);
  EXPECT_EQ(src2.parameterized_index, 2u);
  EXPECT_EQ(src1.declared_type, ScalarType::U32);
}

TEST(ResolvedModule, ResolvesAndChecksM12I05FrozenAddForms) {
  const auto resolved = resolveModule(parseModule(R"ptx(
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
)ptx"));
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
  const auto resolved = resolveModule(parseModule(R"ptx(
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
)ptx"));
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

TEST(ResolvedModule, ResolvesNegativeUnsignedImmediatesAtTargetWidth) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  add.u32 %r0, %r1, -1;
  mov.u32 %r0, -1;
}
)ptx"));
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

TEST(ResolvedModule, ResolvesBareRetInDeviceFunctionAndEntry) {
  const auto ast = parseModule(R"ptx(
.func device() {
  ret;
}
.entry kernel() {
  ret;
}
)ptx");

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
  const auto ast = parseModule(R"ptx(
.func device() {
  .reg .pred %p0;
  @%p0 exit;
}
.entry kernel() {
  exit;
}
)ptx");

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
  const auto ast = parseModule(R"ptx(
.func device() {
  .reg .pred %p0;
  @%p0 trap;
}
.entry kernel() {
  trap;
}
)ptx");

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
  const auto ast = parseModule(R"ptx(
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

TEST(ResolvedModule, RejectsM10CacheHintPolicyWithoutU64Register) {
  const auto wrong_policy_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r0;
  .reg .u32 %policy;
  ld.global.L2::cache_hint.u32 %r0, [%rd0], %policy;
}
)ptx");
  const auto wrong_policy = resolveModule(wrong_policy_ast);
  ASSERT_TRUE(wrong_policy.has_value()) << wrong_policy.error().front().message;
  const auto checked = checker::check(
      std::get<Ld>(wrong_policy->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {7, 4}, .sm_version = 80},
                       .instruction_range = wrong_policy_ast.range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto missing_policy = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r0;
  ld.global.L2::cache_hint.u32 %r0, [%rd0];
}
)ptx"));
  ASSERT_FALSE(missing_policy.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLduGlobalU32Slice) {
  const auto ast = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u64 %wide;
  ldu.global.u32 %wide, [global_value];
}
)ptx");
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

  const auto local_address = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r0;
  ldu.global.u32 %r0, [local_value];
}
)ptx"));
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_space = checker::check(
      std::get<Ldu>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 0}});
  ASSERT_FALSE(wrong_space.has_value());
  EXPECT_EQ(wrong_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto narrow_dst = resolveModule(parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  ldu.global.u32 %h0, [global_value];
}
)ptx"));
  ASSERT_TRUE(narrow_dst.has_value()) << narrow_dst.error().front().message;
  const auto wrong_register = checker::check(
      std::get<Ldu>(narrow_dst->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 0}});
  ASSERT_FALSE(wrong_register.has_value());
  EXPECT_EQ(wrong_register.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  ldu.global.b32 %r0, [global_value];
}
)ptx"));
  ASSERT_FALSE(wrong_type.has_value());

  const auto missing_address = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  ldu.global.u32 %r0;
}
)ptx"));
  ASSERT_FALSE(missing_address.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksPrefetchGlobalL1Slice) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  prefetch.global.L1 [global_value];
}
)ptx");
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

  const auto local_address = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  prefetch.global.L1 [local_value];
}
)ptx"));
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Prefetch>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto wrong_level = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { prefetch.global.L2 [global_value]; }
)ptx"));
  ASSERT_FALSE(wrong_level.has_value());
  const auto wrong_space = resolveModule(parseModule(R"ptx(
.local .u32 local_value;
.entry kernel() { prefetch.local.L1 [local_value]; }
)ptx"));
  ASSERT_FALSE(wrong_space.has_value());
  const auto missing_address = resolveModule(parseModule(R"ptx(
.entry kernel() { prefetch.global.L1; }
)ptx"));
  ASSERT_FALSE(missing_address.has_value());
  const auto extra_address = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { prefetch.global.L1 [global_value], [global_value]; }
)ptx"));
  ASSERT_FALSE(extra_address.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncCaSharedGlobalSlice) {
  const auto ast = parseModule(R"ptx(
.global .align 16 .b8 global_value[16];
.shared .align 16 .b8 shared_value[16];
.entry kernel() {
  cp.async.ca.shared.global [shared_value], [global_value], 4;
  cp.async.ca.shared.global [shared_value], [global_value], 8;
  cp.async.ca.shared.global [shared_value], [global_value], 16;
}
)ptx");

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

  const auto wrong_spaces = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() {
  cp.async.ca.shared.global [global_value], [global_value], 4;
  cp.async.ca.shared.global [shared_value], [shared_value], 4;
}
)ptx"));
  ASSERT_TRUE(wrong_spaces.has_value())
      << wrong_spaces.error().front().message;
  for (const auto& instruction : wrong_spaces->functions.front().body) {
    const auto checked = checker::check(std::get<Cp>(instruction), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  }

  const auto invalid_sizes = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() {
  cp.async.ca.shared.global [shared_value], [global_value], 3;
  cp.async.ca.shared.global [shared_value], [global_value], 32;
}
)ptx"));
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

  const auto register_size = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { .reg .u32 %r0; cp.async.ca.shared.global [shared_value], [global_value], %r0; }
)ptx"));
  ASSERT_FALSE(register_size.has_value());
  const auto non_integer_size = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.ca.shared.global [shared_value], [global_value], 4.0; }
)ptx"));
  ASSERT_FALSE(non_integer_size.has_value());
  const auto wrong_modifier = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.cg.shared.global [shared_value], [global_value], 4; }
)ptx"));
  ASSERT_FALSE(wrong_modifier.has_value());
  const auto missing_size = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.shared .u32 shared_value;
.entry kernel() { cp.async.ca.shared.global [shared_value], [global_value]; }
)ptx"));
  ASSERT_FALSE(missing_size.has_value());
}

TEST(ResolvedModule, ChecksCpAsyncDynamicAddressAlignment) {
  const auto resolved = resolveModule(parseModule(R"ptx(
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
)ptx"));
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

TEST(ResolvedModule, ResolvesAndChecksCpAsyncCommitGroupSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() { cp.async.commit_group; }
)ptx");
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

  const auto missing_async = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.commit_group; }
)ptx"));
  ASSERT_FALSE(missing_async.has_value());
  const auto extra_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.commit_group 0; }
)ptx"));
  ASSERT_FALSE(extra_operand.has_value());
  const auto wrong_wait_token = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx"));
  ASSERT_FALSE(wrong_wait_token.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncWaitGroupSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  cp.async.wait_group 0;
  cp.async.wait_group 1;
  cp.async.wait_group 4294967295;
}
)ptx");
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

  const auto register_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %r0; cp.async.wait_group %r0; }
)ptx"));
  ASSERT_FALSE(register_operand.has_value());
  const auto float_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group 1.0; }
)ptx"));
  ASSERT_FALSE(float_operand.has_value());
  const auto negative_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group -1; }
)ptx"));
  ASSERT_TRUE(negative_operand.has_value())
      << negative_operand.error().front().message;
  const auto negative_checked = checker::check(
      std::get<Cp>(negative_operand->functions.front().body.front()), context);
  ASSERT_FALSE(negative_checked.has_value());
  EXPECT_EQ(negative_checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  const auto missing_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx"));
  ASSERT_FALSE(missing_operand.has_value());
  const auto extra_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group 0, 1; }
)ptx"));
  ASSERT_FALSE(extra_operand.has_value());
  const auto missing_async = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.wait_group 0; }
)ptx"));
  ASSERT_FALSE(missing_async.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksCpAsyncWaitAllSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() { cp.async.wait_all; }
)ptx");
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

  const auto missing_async = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.wait_all; }
)ptx"));
  ASSERT_FALSE(missing_async.has_value());
  const auto immediate_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_all 0; }
)ptx"));
  ASSERT_FALSE(immediate_operand.has_value());
  const auto register_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %r0; cp.async.wait_all %r0; }
)ptx"));
  ASSERT_FALSE(register_operand.has_value());
  const auto wrong_token = resolveModule(parseModule(R"ptx(
.entry kernel() { cp.async.wait_group; }
)ptx"));
  ASSERT_FALSE(wrong_token.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLdmatrixSyncAlignedM8n8X2SharedB16Slice) {
  const auto ast = parseModule(R"ptx(
.shared .align 16 .b16 shared_value[16];
.entry kernel() {
  .reg .b32 %r<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value];
}
)ptx");
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

  const auto wrong_address = resolveModule(parseModule(R"ptx(
.global .b16 global_value;
.entry kernel() { .reg .b32 %r<2>; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [global_value]; }
)ptx"));
  ASSERT_TRUE(wrong_address.has_value())
      << wrong_address.error().front().message;
  const auto address_check = checker::check(
      std::get<Ldmatrix>(wrong_address->functions.front().body.front()), context);
  ASSERT_FALSE(address_check.has_value());
  EXPECT_EQ(address_check.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto unaligned_address = resolveModule(parseModule(R"ptx(
.shared .align 16 .b16 shared_value[16];
.entry kernel() {
  .reg .b32 %r<2>;
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value+16];
  ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value+8];
}
)ptx"));
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

  const auto wrong_register = resolveModule(parseModule(R"ptx(
.shared .b16 shared_value;
.entry kernel() { .reg .b16 %r<2>; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1}, [shared_value]; }
)ptx"));
  ASSERT_FALSE(wrong_register.has_value());

  for (const auto source : {
           ".entry kernel() { .reg .b32 %r<3>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0}, [x]; }",
           ".entry kernel() { .reg .b32 %r<3>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.shared.b16 {%r0, %r1, %r2}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m16n16.x2.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x1.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.aligned.m8n8.x2.trans.shared.b16 {%r0, %r1}, [x]; }",
           ".entry kernel() { .reg .b32 %r<2>; .shared .b16 x; ldmatrix.sync.m8n8.x2.shared.b16 {%r0, %r1}, [x]; }",
       }) {
    ASSERT_FALSE(resolveModule(parseModule(source)).has_value()) << source;
  }
}

TEST(ResolvedModule, ResolvesAndChecksMmaSyncAlignedM16n8k8RowColSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %d<4>;
  .reg .f32 %c<4>;
  .reg .f16x2 %a<2>;
  .reg .f16x2 %b<1>;
  mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32
    {%d0, %d1, %d2, %d3}, {%a0, %a1}, {%b0}, {%c0, %c1, %c2, %c3};
}
)ptx");
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
    ASSERT_FALSE(resolveModule(parseModule(source)).has_value()) << source;
  }
}

TEST(ResolvedModule, ResolvesAndChecksMembarCtaSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() { membar.cta; }
)ptx");
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

  const auto wrong_gl = resolveModule(parseModule(R"ptx(
.entry kernel() { membar.gl; }
)ptx"));
  ASSERT_FALSE(wrong_gl.has_value());
  const auto wrong_sys = resolveModule(parseModule(R"ptx(
.entry kernel() { membar.sys; }
)ptx"));
  ASSERT_FALSE(wrong_sys.has_value());
  const auto extra_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { membar.cta 0; }
)ptx"));
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksFenceAcqRelCtaSlice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() { fence.acq_rel.cta; }
)ptx");
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

  const auto wrong_semantics = resolveModule(parseModule(R"ptx(
.entry kernel() { fence.acquire.cta; }
)ptx"));
  ASSERT_FALSE(wrong_semantics.has_value());
  const auto wrong_scope = resolveModule(parseModule(R"ptx(
.entry kernel() { fence.acq_rel.sys; }
)ptx"));
  ASSERT_FALSE(wrong_scope.has_value());
  const auto extra_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { fence.acq_rel.cta 0; }
)ptx"));
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksAtomGlobalRelaxedCtaAddU32Slice) {
  const auto ast = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.add.u32 %r0, [global_value], %r1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction = std::get<Atom>(resolved->functions.front().body.front());
  const auto& atom =
      std::get<Atom::GlobalRelaxedCtaAddU32>(instruction.variant);
  EXPECT_EQ(atom.state_space, MemoryStateSpace::Global);
  EXPECT_EQ(atom.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(atom.scope, MemoryScope::Cta);
  EXPECT_TRUE(atom.add);
  EXPECT_EQ(atom.type, ScalarType::U32);
  EXPECT_EQ(atom.dst.value.declared_type, ScalarType::U32);
  EXPECT_EQ(atom.src.value.declared_type, ScalarType::U32);
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

  const auto local_address = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.add.u32 %r0, [local_value], %r1;
}
)ptx"));
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Atom>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto mismatched_registers = resolveModule(parseModule(R"ptx(
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
)ptx"));
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

  const auto wrong_operation = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.global.relaxed.cta.and.u32 %r0, [global_value], %r1;
}
)ptx"));
  ASSERT_FALSE(wrong_operation.has_value());
  const auto wrong_ordering = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  atom.global.acquire.cta.add.u32 %r0, [global_value], %r1;
}
)ptx"));
  ASSERT_FALSE(wrong_ordering.has_value());
  const auto missing_operand = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  atom.global.relaxed.cta.add.u32 %r0, [global_value];
}
)ptx"));
  ASSERT_FALSE(missing_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksRedGlobalRelaxedCtaAddU32Slice) {
  const auto ast = parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.global.relaxed.cta.add.u32 [global_value], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction = std::get<Red>(resolved->functions.front().body.front());
  const auto& red = std::get<Red::GlobalRelaxedCtaAddU32>(instruction.variant);
  EXPECT_EQ(red.state_space, MemoryStateSpace::Global);
  EXPECT_EQ(red.semantics, MemoryConsistency::Relaxed);
  EXPECT_EQ(red.scope, MemoryScope::Cta);
  EXPECT_TRUE(red.add);
  EXPECT_EQ(red.type, ScalarType::U32);
  EXPECT_EQ(red.src.value.declared_type, ScalarType::U32);
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

  const auto local_address = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .local .u32 local_value;
  .reg .u32 %r0;
  red.global.relaxed.cta.add.u32 [local_value], %r0;
}
)ptx"));
  ASSERT_TRUE(local_address.has_value())
      << local_address.error().front().message;
  const auto wrong_address = checker::check(
      std::get<Red>(local_address->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 70}});
  ASSERT_FALSE(wrong_address.has_value());
  EXPECT_EQ(wrong_address.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);

  const auto mismatched_sources = resolveModule(parseModule(R"ptx(
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  .reg .u64 %wide0;
  red.global.relaxed.cta.add.u32 [global_value], %h0;
  red.global.relaxed.cta.add.u32 [global_value], %wide0;
}
)ptx"));
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

  const auto wrong_operation = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.global.relaxed.cta.and.u32 [global_value], %r0;
}
)ptx"));
  ASSERT_FALSE(wrong_operation.has_value());
  const auto wrong_ordering = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  red.global.acquire.cta.add.u32 [global_value], %r0;
}
)ptx"));
  ASSERT_FALSE(wrong_ordering.has_value());
  const auto unexpected_dst = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<2>;
  red.global.relaxed.cta.add.u32 %r0, [global_value], %r1;
}
)ptx"));
  ASSERT_FALSE(unexpected_dst.has_value());
  const auto missing_source = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() { red.global.relaxed.cta.add.u32 [global_value]; }
)ptx"));
  ASSERT_FALSE(missing_source.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksActivemaskB32Slice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  activemask.b32 %b0;
}
)ptx");
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

  const auto mismatched_dsts = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  .reg .b64 %wide0;
  activemask.b32 %u0;
  activemask.b32 %wide0;
}
)ptx"));
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

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; activemask.u32 %b0; }
)ptx"));
  ASSERT_FALSE(wrong_type.has_value());
  const auto extra_operand = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b<2>; activemask.b32 %b0, %b1; }
)ptx"));
  ASSERT_FALSE(extra_operand.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksVoteSyncBallotB32Slice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  .reg .u32 %mask;
  vote.sync.ballot.b32 %b0, %p0, 0xffffffff;
  vote.sync.ballot.b32 %b1, %p0, %mask;
}
)ptx");
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

  const auto bad_dst_and_mask = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u0;
  .reg .b64 %wide0;
  .reg .pred %p0;
  .reg .b64 %bad_mask;
  vote.sync.ballot.b32 %u0, %p0, 0;
  vote.sync.ballot.b32 %wide0, %p0, 0;
  vote.sync.ballot.b32 %u0, %p0, %bad_mask;
}
)ptx"));
  ASSERT_TRUE(bad_dst_and_mask.has_value())
      << bad_dst_and_mask.error().front().message;
  for (const auto& candidate : bad_dst_and_mask->functions.front().body) {
    const auto checked = checker::check(
        std::get<Vote>(candidate), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto bad_predicate = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .u32 %r0;
  vote.sync.ballot.b32 %b0, %r0, 0;
}
)ptx"));
  ASSERT_FALSE(bad_predicate.has_value());
  const auto wrong_all = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.all.pred %b0, %p0, 0; }
)ptx"));
  ASSERT_FALSE(wrong_all.has_value());
  const auto wrong_any = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.any.pred %b0, %p0, 0; }
)ptx"));
  ASSERT_FALSE(wrong_any.has_value());
  const auto legacy = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.ballot.b32 %b0, %p0; }
)ptx"));
  ASSERT_FALSE(legacy.has_value());
  const auto missing_mask = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b0; .reg .pred %p0; vote.sync.ballot.b32 %b0, %p0; }
)ptx"));
  ASSERT_FALSE(missing_mask.has_value());
}

TEST(ResolvedModule, ResolvesAndChecksShflSyncIdxB32Slice) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<3>;
  .reg .pred %p<2>;
  .reg .u32 %u<4>;
  shfl.sync.idx.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
  shfl.sync.idx.b32 %b1|%p1, %b2, %u0, %u1, %u2;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& immediate =
      std::get<Shfl::SyncIdxB32>(std::get<Shfl>(body[0]).variant);
  const auto& register_operands =
      std::get<Shfl::SyncIdxB32>(std::get<Shfl>(body[1]).variant);
  EXPECT_TRUE(immediate.sync);
  EXPECT_TRUE(immediate.idx);
  EXPECT_EQ(immediate.type, ScalarType::B32);
  EXPECT_EQ(immediate.dst.value.data.declared_type, ScalarType::B32);
  EXPECT_EQ(immediate.dst.value.predicate.register_ref.declared_type,
            ScalarType::Pred);
  EXPECT_FALSE(immediate.dst.locs.empty());
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(immediate.lane.value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(register_operands.lane.value));
  const checker::Context context{
      .target = {.ptx_version = {6, 0}, .sm_version = 30},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Shfl>(body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Shfl>(body[1]), context).has_value());

  const auto too_old_ptx = checker::check(
      std::get<Shfl>(body[0]),
      checker::Context{.target = {.ptx_version = {5, 9}, .sm_version = 30},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_ptx.has_value());
  EXPECT_EQ(too_old_ptx.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto too_old_sm = checker::check(
      std::get<Shfl>(body[0]),
      checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 29},
                       .instruction_range = ast.range});
  ASSERT_FALSE(too_old_sm.has_value());
  EXPECT_EQ(too_old_sm.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto bad_data_and_lane = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  .reg .b64 %wide0;
  shfl.sync.idx.b32 %b0|%p0, %wide0, 0, 31, 0xffffffff;
}
)ptx"));
  ASSERT_TRUE(bad_data_and_lane.has_value())
      << bad_data_and_lane.error().front().message;
  const auto bad_check = checker::check(
      std::get<Shfl>(bad_data_and_lane->functions.front().body.front()), context);
  ASSERT_FALSE(bad_check.has_value());
  EXPECT_EQ(bad_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto bad_pair = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b0;
  .reg .u32 %u0;
  shfl.sync.idx.b32 %b0|%u0, %b0, 0, 31, 0xffffffff;
}
)ptx"));
  ASSERT_FALSE(bad_pair.has_value());
  const auto wrong_mode = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.sync.up.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
}
)ptx"));
  ASSERT_FALSE(wrong_mode.has_value());
  const auto legacy = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.idx.b32 %b0|%p0, %b1, 0, 31, 0xffffffff;
}
)ptx"));
  ASSERT_FALSE(legacy.has_value());
  const auto missing_membermask = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<2>;
  .reg .pred %p0;
  shfl.sync.idx.b32 %b0|%p0, %b1, 0, 31;
}
)ptx"));
  ASSERT_FALSE(missing_membermask.has_value());
}

TEST(ResolvedModule, ChecksAndB32RegisterCompatibilityAndWidth) {
  const auto valid_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u;
  .reg .s32 %s;
  .reg .b32 %b;
  and.b32 %b, %u, %s;
}
)ptx");
  const auto valid = resolveModule(valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto valid_check = checker::check(
      std::get<And>(valid->functions.front().body.front()),
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = valid_ast.range,
      });
  EXPECT_TRUE(valid_check.has_value());

  const auto invalid_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b;
  .reg .u16 %h;
  and.b32 %b, %h, %b;
}
)ptx");
  const auto invalid = resolveModule(invalid_ast);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_and =
      std::get<And>(invalid->functions.front().body.front());
  const auto& invalid_variant = std::get<And::B32>(invalid_and.variant);
  const auto invalid_check = checker::check(
      invalid_and,
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = invalid_ast.range,
      });
  ASSERT_FALSE(invalid_check.has_value());
  ASSERT_EQ(invalid_check.error().size(), 1u);
  EXPECT_EQ(invalid_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(invalid_check.error().front().range,
            invalid_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksOrB32RegisterCompatibilityAndWidth) {
  const auto valid_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u;
  .reg .s32 %s;
  .reg .b32 %b;
  or.b32 %b, %u, %s;
}
)ptx");
  const auto valid = resolveModule(valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto valid_check = checker::check(
      std::get<Or>(valid->functions.front().body.front()),
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = valid_ast.range,
      });
  EXPECT_TRUE(valid_check.has_value());

  const auto invalid_ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b;
  .reg .u16 %h;
  or.b32 %b, %h, %b;
}
)ptx");
  const auto invalid = resolveModule(invalid_ast);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_or = std::get<Or>(invalid->functions.front().body.front());
  const auto& invalid_variant = std::get<Or::B32>(invalid_or.variant);
  const auto invalid_check = checker::check(
      invalid_or,
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = invalid_ast.range,
      });
  ASSERT_FALSE(invalid_check.has_value());
  ASSERT_EQ(invalid_check.error().size(), 1u);
  EXPECT_EQ(invalid_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(invalid_check.error().front().range,
            invalid_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksXorB32RegisterCompatibilityAndWidth) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .s32 %s; .reg .b32 %b; xor.b32 %b, %u, %s; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Xor>(valid->functions.front().body.front()),
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; xor.b32 %b, %h, %b; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Xor>(invalid->functions.front().body.front());
  const auto& variant = std::get<Xor::B32>(instruction.variant);
  const auto checked = checker::check(
      instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksNotB32RegisterCompatibilityAndWidth) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .b32 %b; not.b32 %b, %u; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Not>(valid->functions.front().body.front()),
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; not.b32 %b, %h; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Not>(invalid->functions.front().body.front());
  const auto& variant = std::get<Not::B32>(instruction.variant);
  const auto checked = checker::check(
      instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksShlB32DataAndAmountWidths) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %u, %amount; .reg .b32 %b; shl.b32 %b, %u, %amount; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Shl>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}}).has_value());
  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u64 %amount; shl.b32 %b, %b, %amount; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Shl>(invalid->functions.front().body.front());
  const auto& variant = std::get<Shl::B32>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.amount.locs.front());
}

TEST(ResolvedModule, ChecksShrU32DataAndAmountWidths) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .s32 %s; .reg .b32 %b; shr.u32 %b, %s, %b; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Shr>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}}).has_value());
  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .u64 %amount; shr.u32 %u, %u, %amount; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Shr>(invalid->functions.front().body.front());
  const auto& variant = std::get<Shr::U32>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.amount.locs.front());
}

TEST(ResolvedModule, ChecksSetpLtU32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .pred %p0, %p1; .reg .u32 %u; setp.lt.and.u32 %p0, %u, 16, !%p1; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Setp>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}}).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %wide; setp.lt.u32 %p0, %wide, 16; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Setp>(invalid->functions.front().body.front());
  const auto& variant = std::get<Setp::LtU32>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksSetpDualPredicateOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .pred %p0, %p1, %p2; .reg .u32 %u0, %u1; .reg .s32 %s0, %s1; setp.eq.u32 %p0|%p1, %u0, %u1; setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(checker::check(
                    std::get<Setp>(body),
                    checker::Context{.target = {.ptx_version = {1, 0},
                                                 .sm_version = 0}})
                    .has_value());
  }

  auto instruction = std::get<Setp>(valid->functions.front().body.front());
  auto& variant = std::get<Setp::EqU32Pair>(instruction.variant);
  variant.dst.value.second.register_ref.declared_type = ScalarType::U32;
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.dst.locs[1]);
}

TEST(ResolvedModule, ChecksSetCommonScalarOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .u32 %r0, %r1, %r2;
  .reg .f32 %f;
  .reg .s32 %s0, %s1;
  set.eq.u32.u32 %r0, %r1, %r2;
  set.lt.and.f32.s32 %f, %s0, %s1, !%p;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(checker::check(
                    std::get<Set>(body),
                    checker::Context{.target = {.ptx_version = {1, 0},
                                                 .sm_version = 0}})
                    .has_value());
  }

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .f32 %f;
  .reg .s32 %s;
  .reg .u64 %wrong;
  set.lt.and.f32.s32 %f, %wrong, %s, %p;
}
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Set>(invalid->functions.front().body.front());
  const auto& variant = std::get<Set::LtAndF32S32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksSlctNumericSelectorAndBitSizeDataOperands) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %d32;
  .reg .s32 %a32;
  .reg .u32 %b32;
  .reg .s32 %selector32;
  .reg .b64 %d64;
  .reg .s64 %a64;
  .reg .u64 %b64;
  .reg .f32 %selector64;
  slct.u32.s32 %d32, %a32, %b32, %selector32;
  slct.ftz.u64.f32 %d64, %a64, %b64, %selector64;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(checker::check(
                    std::get<Slct>(body),
                    checker::Context{.target = {.ptx_version = {1, 0},
                                                 .sm_version = 0}})
                    .has_value());
  }

  const auto wrong_selector = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %a, %b, %selector;
  slct.u32.s32 %d, %a, %b, %selector;
}
)ptx"));
  ASSERT_TRUE(wrong_selector.has_value())
      << wrong_selector.error().front().message;
  const auto& selector_instruction =
      std::get<Slct>(wrong_selector->functions.front().body.front());
  const auto& selector_variant =
      std::get<Slct::U32S32>(selector_instruction.variant);
  const auto bad_selector = checker::check(
      selector_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(bad_selector.has_value());
  EXPECT_EQ(bad_selector.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bad_selector.error().front().range,
            selector_variant.selector.locs.front());

  const auto wrong_data = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %b;
  .reg .f32 %a;
  .reg .s32 %selector;
  slct.u32.s32 %d, %a, %b, %selector;
}
)ptx"));
  ASSERT_TRUE(wrong_data.has_value()) << wrong_data.error().front().message;
  const auto& data_instruction =
      std::get<Slct>(wrong_data->functions.front().body.front());
  const auto& data_variant = std::get<Slct::U32S32>(data_instruction.variant);
  const auto bad_data = checker::check(
      data_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(bad_data.has_value());
  EXPECT_EQ(bad_data.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bad_data.error().front().range,
            data_variant.src_true.locs.front());

  const auto predicate_selector = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %a, %b;
  .reg .pred %p;
  slct.u32.s32 %d, %a, %b, %p;
}
)ptx"));
  EXPECT_FALSE(predicate_selector.has_value());
}

TEST(ResolvedModule, ChecksSelpU32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .pred %p; .reg .u32 %dst, %src; selp.u32 %dst, %src, 0, %p; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Selp>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}}).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .pred %p; .reg .u32 %dst; .reg .u64 %wide; selp.u32 %dst, %wide, 0, %p; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Selp>(invalid->functions.front().body.front());
  const auto& variant = std::get<Selp::U32>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src_true.locs.front());
}

TEST(ResolvedModule, ChecksCvtS32U32OperandTypesAndWidths) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .s64 %wide_dst; .reg .u64 %wide_src; cvt.s32.u32 %wide_dst, %wide_src; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Cvt>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}}).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst; .reg .f32 %wrong_src; cvt.s32.u32 %dst, %wrong_src; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::S32U32>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksCvtRnF32F64OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .f64 %src; cvt.rn.f32.f64 %dst, %src; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Cvt>(valid->functions.front().body.front()), checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 13}}).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %wrong_src; cvt.rn.f32.f64 %dst, %wrong_src; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::RnF32F64>(instruction.variant);
  const auto checked = checker::check(instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 13}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksMixedCvtOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %f; .reg .u32 %u; cvt.rn.f32.u32 %f, %u; cvt.rzi.u32.f32 %u, %f; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(std::get<Cvt>(valid->functions.front().body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Cvt>(valid->functions.front().body[1]), context).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %wrong_dst, %src; cvt.rzi.u32.f32 %wrong_dst, %src; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::RziU32F32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksM12CvtScalarAndPackedTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %f0, %f1;
  .reg .u32 %u0;
  .reg .b32 %r0;
  cvt.rn.f32.s32 %f0, %u0;
  cvt.rn.f16x2.f32 %r0, %f0, %f1;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& scalar = std::get<Cvt>(valid->functions.front().body[0]);
  const auto& packed = std::get<Cvt>(valid->functions.front().body[1]);
  EXPECT_TRUE(std::holds_alternative<Cvt::RnF32S32>(scalar.variant));
  EXPECT_TRUE(std::holds_alternative<Cvt::RnF16x2F32>(packed.variant));
  EXPECT_TRUE(checker::check(
                  scalar,
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  packed,
                  checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .f16x2 %dst; .reg .f32 %a, %b; cvt.rn.f16x2.f32 %dst, %a, %b; }",
           ".entry kernel() { .reg .f32 %dst, %a, %b; cvt.rn.f16x2.f32 %dst, %a, %b; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Cvt>(wrong->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12CvtPackOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2, %r3;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction = std::get<Cvt>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Cvt::PackSatU8S32B32>(instruction.variant));
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 72}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst; .reg .u32 %a, %b, %c; cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst, %a, %c; .reg .f32 %b; cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst, %a, %b; .reg .u64 %c; cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Cvt>(wrong->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 72}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksCvtaU64OperandWidths) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %src; cvta.global.u64 %dst, %src; cvta.to.global.u64 %dst, %src; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(checker::check(std::get<Cvta>(valid->functions.front().body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Cvta>(valid->functions.front().body[1]), context).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; cvta.global.u64 %dst, %src; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Cvta>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvta::GlobalU64>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksMulLoU32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; mul.lo.u32 %dst, %src, 7; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(std::get<Mul>(valid->functions.front().body.front()), context).has_value());

  const auto wrong_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.lo.u32 %dst, %src, 7; }
)ptx"));
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction = std::get<Mul>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Mul::LoU32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range, width_variant.dst.locs.front());

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %src; mul.lo.u32 %dst, %src, 7; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction = std::get<Mul>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Mul::LoU32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksMulHiAndWideU32OperandTypes) {
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2;
  .reg .u64 %rd0;
  mul.hi.u32 %r0, %r1, %r2;
  mul.wide.u32 %rd0, %r1, %r2;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& hi = std::get<Mul>(body[0]);
  const auto& wide = std::get<Mul>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Mul::HiU32>(hi.variant));
  EXPECT_TRUE(std::holds_alternative<Mul::WideU32>(wide.variant));
  EXPECT_TRUE(checker::check(hi, context).has_value());
  EXPECT_TRUE(checker::check(wide, context).has_value());

  const auto narrow_hi = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.hi.u32 %dst, %src, %src; }
)ptx"));
  ASSERT_TRUE(narrow_hi.has_value()) << narrow_hi.error().front().message;
  const auto& narrow_instruction = std::get<Mul>(narrow_hi->functions.front().body.front());
  const auto& narrow_variant = std::get<Mul::HiU32>(narrow_instruction.variant);
  const auto narrow_checked = checker::check(narrow_instruction, context);
  ASSERT_FALSE(narrow_checked.has_value());
  EXPECT_EQ(narrow_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(narrow_checked.error().front().range, narrow_variant.dst.locs.front());

  const auto bit_wide_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst; .reg .b32 %src; mul.wide.u32 %dst, %src, %src; }
)ptx"));
  ASSERT_TRUE(bit_wide_source.has_value()) << bit_wide_source.error().front().message;
  const auto& bit_instruction = std::get<Mul>(bit_wide_source->functions.front().body.front());
  const auto& bit_variant = std::get<Mul::WideU32>(bit_instruction.variant);
  const auto bit_checked = checker::check(bit_instruction, context);
  ASSERT_FALSE(bit_checked.has_value());
  EXPECT_EQ(bit_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bit_checked.error().front().range, bit_variant.src1.locs.front());

  const auto narrow_wide_dst = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; mul.wide.u32 %dst, %src, %src; }
)ptx"));
  ASSERT_TRUE(narrow_wide_dst.has_value()) << narrow_wide_dst.error().front().message;
  const auto& dst_instruction = std::get<Mul>(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = std::get<Mul::WideU32>(dst_instruction.variant);
  const auto dst_checked = checker::check(dst_instruction, context);
  ASSERT_FALSE(dst_checked.has_value());
  EXPECT_EQ(dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(dst_checked.error().front().range, dst_variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksMulRnF32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src1, %src2; mul.rn.f32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(std::get<Mul>(valid->functions.front().body.front()), context).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .f64 %wrong_src; mul.rn.f32 %dst, %wrong_src, %src2; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Mul>(invalid->functions.front().body.front());
  const auto& variant = std::get<Mul::RnF32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksMadLoU32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src1, %src3; mad.lo.u32 %dst, %src1, 7, %src3; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(std::get<Mad>(valid->functions.front().body.front()), context).has_value());

  const auto wrong_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src1, %src3; mad.lo.u32 %dst, %src1, 7, %src3; }
)ptx"));
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction = std::get<Mad>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Mad::LoU32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range, width_variant.dst.locs.front());

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src2, %src3; .reg .f32 %wrong_src; mad.lo.u32 %dst, %wrong_src, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction = std::get<Mad>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Mad::LoU32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12MadWideAndRnOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  .reg .u64 %rd0, %rd3;
  .reg .f32 %f0, %f1, %f2, %f3;
  mad.lo.s32 %r0, %r1, %r2, %r3;
  mad.wide.u32 %rd0, %r1, %r2, %rd3;
  mad.rn.f32 %f0, %f1, %f2, %f3;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& lo = std::get<Mad>(body[0]);
  const auto& wide = std::get<Mad>(body[1]);
  const auto& rn = std::get<Mad>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Mad::LoS32>(lo.variant));
  EXPECT_TRUE(std::holds_alternative<Mad::WideU32>(wide.variant));
  EXPECT_TRUE(std::holds_alternative<Mad::RnF32>(rn.variant));
  EXPECT_TRUE(checker::check(
                  lo, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  wide, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  rn, checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}})
                  .has_value());

  const auto bit_wide_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %addend; .reg .b32 %src; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx"));
  ASSERT_TRUE(bit_wide_source.has_value()) << bit_wide_source.error().front().message;
  const auto& bit_instruction = std::get<Mad>(bit_wide_source->functions.front().body.front());
  const auto& bit_variant = std::get<Mad::WideU32>(bit_instruction.variant);
  const auto bit_checked = checker::check(
      bit_instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(bit_checked.has_value());
  EXPECT_EQ(bit_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bit_checked.error().front().range, bit_variant.src1.locs.front());

  const auto narrow_wide_addend = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst; .reg .u32 %src, %addend; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx"));
  ASSERT_TRUE(narrow_wide_addend.has_value()) << narrow_wide_addend.error().front().message;
  const auto& addend_instruction = std::get<Mad>(narrow_wide_addend->functions.front().body.front());
  const auto& addend_variant = std::get<Mad::WideU32>(addend_instruction.variant);
  const auto addend_checked = checker::check(
      addend_instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(addend_checked.has_value());
  EXPECT_EQ(addend_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(addend_checked.error().front().range, addend_variant.src3.locs.front());

  const auto narrow_wide_dst = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; .reg .u64 %addend; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx"));
  ASSERT_TRUE(narrow_wide_dst.has_value()) << narrow_wide_dst.error().front().message;
  const auto& dst_instruction = std::get<Mad>(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = std::get<Mad::WideU32>(dst_instruction.variant);
  const auto dst_checked = checker::check(
      dst_instruction, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(dst_checked.has_value());
  EXPECT_EQ(dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(dst_checked.error().front().range, dst_variant.dst.locs.front());

  const auto bit_float_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2, %src3; .reg .b32 %src1; mad.rn.f32 %dst, %src1, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(bit_float_source.has_value()) << bit_float_source.error().front().message;
  const auto& float_instruction = std::get<Mad>(bit_float_source->functions.front().body.front());
  const auto& float_variant = std::get<Mad::RnF32>(float_instruction.variant);
  const auto float_checked = checker::check(
      float_instruction, checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  ASSERT_FALSE(float_checked.has_value());
  EXPECT_EQ(float_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(float_checked.error().front().range, float_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksFmaRnF32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src1, %src2, %src3; fma.rn.f32 %dst, %src1, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(checker::check(std::get<Fma>(valid->functions.front().body.front()), context).has_value());

  const auto invalid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2, %src3; .reg .f64 %wrong_src; fma.rn.f32 %dst, %wrong_src, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = std::get<Fma>(invalid->functions.front().body.front());
  const auto& variant = std::get<Fma::RnF32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12FmaRnF64AndF16OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .f64 %d0, %d1, %d2, %d3;
  .reg .f16 %h0, %h1, %h2, %h3;
  fma.rn.f64 %d0, %d1, %d2, %d3;
  fma.rn.f16 %h0, %h1, %h2, %h3;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& f64 = std::get<Fma>(body[0]);
  const auto& f16 = std::get<Fma>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Fma::RnF64>(f64.variant));
  EXPECT_TRUE(std::holds_alternative<Fma::RnF16>(f16.variant));
  EXPECT_TRUE(checker::check(
                  f64, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 13}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  f16, checker::Context{.target = {.ptx_version = {4, 2}, .sm_version = 53}})
                  .has_value());

  const auto bit_f64_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f64 %dst, %src2, %src3; .reg .b64 %src1; fma.rn.f64 %dst, %src1, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(bit_f64_source.has_value()) << bit_f64_source.error().front().message;
  const auto& f64_instruction = std::get<Fma>(bit_f64_source->functions.front().body.front());
  const auto& f64_variant = std::get<Fma::RnF64>(f64_instruction.variant);
  const auto f64_checked = checker::check(
      f64_instruction, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 13}});
  ASSERT_FALSE(f64_checked.has_value());
  EXPECT_EQ(f64_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(f64_checked.error().front().range, f64_variant.src1.locs.front());

  const auto bit_f16_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f16 %dst, %src2, %src3; .reg .b16 %src1; fma.rn.f16 %dst, %src1, %src2, %src3; }
)ptx"));
  ASSERT_TRUE(bit_f16_source.has_value()) << bit_f16_source.error().front().message;
  const auto& f16_instruction = std::get<Fma>(bit_f16_source->functions.front().body.front());
  const auto& f16_variant = std::get<Fma::RnF16>(f16_instruction.variant);
  const auto f16_checked = checker::check(
      f16_instruction, checker::Context{.target = {.ptx_version = {4, 2}, .sm_version = 53}});
  ASSERT_FALSE(f16_checked.has_value());
  EXPECT_EQ(f16_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(f16_checked.error().front().range, f16_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksDivU32OperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; div.u32 %dst, %src, 0; }
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(std::get<Div>(valid->functions.front().body.front()), context).has_value());

  const auto wrong_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; div.u32 %dst, %src, 0; }
)ptx"));
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction = std::get<Div>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Div::U32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range, width_variant.dst.locs.front());

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %wrong_src; div.u32 %dst, %wrong_src, 0; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction = std::get<Div>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Div::U32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12DivS32AndRnFloatingOperandTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2;
  .reg .f32 %f0, %f1, %f2;
  .reg .f64 %d0, %d1, %d2;
  div.s32 %r0, %r1, %r2;
  div.rn.f32 %f0, %f1, %f2;
  div.rn.f64 %d0, %d1, %d2;
}

)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& s32 = std::get<Div>(body[0]);
  const auto& f32 = std::get<Div>(body[1]);
  const auto& f64 = std::get<Div>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Div::S32>(s32.variant));
  EXPECT_TRUE(std::holds_alternative<Div::RnF32>(f32.variant));
  EXPECT_TRUE(std::holds_alternative<Div::RnF64>(f64.variant));
  EXPECT_TRUE(checker::check(
                  s32, checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  f32, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 20}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  f64, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 13}})
                  .has_value());

  const auto bit_f32_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; div.rn.f32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(bit_f32_source.has_value()) << bit_f32_source.error().front().message;
  const auto& f32_instruction = std::get<Div>(bit_f32_source->functions.front().body.front());
  const auto& f32_variant = std::get<Div::RnF32>(f32_instruction.variant);
  const auto f32_checked = checker::check(
      f32_instruction, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 20}});
  ASSERT_FALSE(f32_checked.has_value());
  EXPECT_EQ(f32_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(f32_checked.error().front().range, f32_variant.src1.locs.front());

  const auto bit_f64_source = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f64 %dst, %src2; .reg .b64 %src1; div.rn.f64 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(bit_f64_source.has_value()) << bit_f64_source.error().front().message;
  const auto& f64_instruction = std::get<Div>(bit_f64_source->functions.front().body.front());
  const auto& f64_variant = std::get<Div::RnF64>(f64_instruction.variant);
  const auto f64_checked = checker::check(
      f64_instruction, checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 13}});
  ASSERT_FALSE(f64_checked.has_value());
  EXPECT_EQ(f64_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(f64_checked.error().front().range, f64_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12RemTypesAndZeroDivisor) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .u32 %u0, %u1, %u2;
  rem.s32 %s0, %s1, 0;
  rem.u32 %u0, %u1, %u2;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& signed_rem = std::get<Rem>(body[0]);
  const auto& unsigned_rem = std::get<Rem>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Rem::S32>(signed_rem.variant));
  EXPECT_TRUE(std::holds_alternative<Rem::U32>(unsigned_rem.variant));
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(signed_rem, context).has_value());
  EXPECT_TRUE(checker::check(unsigned_rem, context).has_value());

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %src; rem.u32 %dst, %src, 0; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction = std::get<Rem>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Rem::U32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());

  const auto wrong_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %src; rem.s32 %dst, %src, 0; }
)ptx"));
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction = std::get<Rem>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Rem::S32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range, width_variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksM12MinTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1, %s2;
  .reg .f32 %f0, %f1, %f2;
  min.s32 %s0, %s1, %s2;
  min.NaN.f32 %f0, %f1, %f2;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_min = std::get<Min>(body[0]);
  const auto& nan_min = std::get<Min>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Min::S32>(integer_min.variant));
  EXPECT_TRUE(std::holds_alternative<Min::NanF32>(nan_min.variant));
  EXPECT_TRUE(checker::check(
                  integer_min,
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  nan_min,
                  checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  const auto wrong_integer_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; min.s32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(wrong_integer_type.has_value()) << wrong_integer_type.error().front().message;
  const auto& integer_instruction = std::get<Min>(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant = std::get<Min::S32>(integer_instruction.variant);
  const auto integer_checked = checker::check(
      integer_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(integer_checked.has_value());
  EXPECT_EQ(integer_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(integer_checked.error().front().range, integer_variant.src1.locs.front());

  const auto wrong_nan_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; min.NaN.f32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(wrong_nan_width.has_value()) << wrong_nan_width.error().front().message;
  const auto& nan_instruction = std::get<Min>(wrong_nan_width->functions.front().body.front());
  const auto& nan_variant = std::get<Min::NanF32>(nan_instruction.variant);
  const auto nan_checked = checker::check(
      nan_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  ASSERT_FALSE(nan_checked.has_value());
  EXPECT_EQ(nan_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(nan_checked.error().front().range, nan_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12MaxTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1, %s2;
  .reg .f32 %f0, %f1, %f2;
  max.s32 %s0, %s1, %s2;
  max.NaN.f32 %f0, %f1, %f2;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_max = std::get<Max>(body[0]);
  const auto& nan_max = std::get<Max>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Max::S32>(integer_max.variant));
  EXPECT_TRUE(std::holds_alternative<Max::NanF32>(nan_max.variant));
  EXPECT_TRUE(checker::check(
                  integer_max,
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  nan_max,
                  checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  const auto wrong_integer_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; max.s32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(wrong_integer_type.has_value()) << wrong_integer_type.error().front().message;
  const auto& integer_instruction = std::get<Max>(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant = std::get<Max::S32>(integer_instruction.variant);
  const auto integer_checked = checker::check(
      integer_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(integer_checked.has_value());
  EXPECT_EQ(integer_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(integer_checked.error().front().range, integer_variant.src1.locs.front());

  const auto wrong_nan_width = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; max.NaN.f32 %dst, %src1, %src2; }
)ptx"));
  ASSERT_TRUE(wrong_nan_width.has_value()) << wrong_nan_width.error().front().message;
  const auto& nan_instruction = std::get<Max>(wrong_nan_width->functions.front().body.front());
  const auto& nan_variant = std::get<Max::NanF32>(nan_instruction.variant);
  const auto nan_checked = checker::check(
      nan_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  ASSERT_FALSE(nan_checked.has_value());
  EXPECT_EQ(nan_checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(nan_checked.error().front().range, nan_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12AbsTypes) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .f32 %f0, %f1;
  abs.s32 %s0, %s1;
  abs.f32 %f0, %f1;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_abs = std::get<Abs>(body[0]);
  const auto& float_abs = std::get<Abs>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Abs::S32>(integer_abs.variant));
  EXPECT_TRUE(std::holds_alternative<Abs::F32>(float_abs.variant));
  const checker::Context context{.target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(integer_abs, context).has_value());
  EXPECT_TRUE(checker::check(float_abs, context).has_value());

  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .s32 %src; abs.f32 %dst, %src; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& instruction = std::get<Abs>(wrong_type->functions.front().body.front());
  const auto& variant = std::get<Abs::F32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksM12NegTypesAndPackedContainers) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .f32 %f0, %f1;
  .reg .b32 %r0, %r1;
  neg.s32 %s0, %s1;
  neg.f32 %f0, %f1;
  neg.f16x2 %r0, %r1;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_neg = std::get<Neg>(body[0]);
  const auto& float_neg = std::get<Neg>(body[1]);
  const auto& packed_neg = std::get<Neg>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Neg::S32>(integer_neg.variant));
  EXPECT_TRUE(std::holds_alternative<Neg::F32>(float_neg.variant));
  EXPECT_TRUE(std::holds_alternative<Neg::F16x2>(packed_neg.variant));
  EXPECT_TRUE(checker::check(
                  integer_neg,
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  float_neg,
                  checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(
                  packed_neg,
                  checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 53}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .f16 %dst, %src; neg.f16x2 %dst, %src; }",
           ".entry kernel() { .reg .f32 %dst, %src; neg.f16x2 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong_container = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong_container.has_value()) << wrong_container.error().front().message;
    const auto& instruction = std::get<Neg>(wrong_container->functions.front().body.front());
    const auto checked = checker::check(
        instruction, checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 53}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12Lop3B32WidthCompatibility) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  lop3.b32 %r0, %r1, %r2, %r3, 0x1a;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction = std::get<Lop3>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Lop3::B32>(instruction.variant));
  EXPECT_TRUE(checker::check(
                  instruction,
                  checker::Context{.target = {.ptx_version = {4, 3}, .sm_version = 50}})
                  .has_value());
}

TEST(ResolvedModule, ChecksM12ShfTypesAndCounts) {
  const auto valid = resolveModule(parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  shf.l.clamp.b32 %r0, %r1, %r2, 8;
  shf.r.wrap.b32 %r0, %r1, %r2, %r3;
}
)ptx"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  EXPECT_TRUE(std::holds_alternative<Shf::LClampB32>(std::get<Shf>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Shf::RWrapB32>(std::get<Shf>(body[1]).variant));
  const auto wrong_type = resolveModule(parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst; .reg .u32 %a, %b; shf.l.clamp.b32 %dst, %a, %b, 8; }
)ptx"));
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto checked = checker::check(
      std::get<Shf>(wrong_type->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {3, 1}, .sm_version = 32}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ChecksM12PrmtRegisterWidths) {
  const auto valid = resolveModule(parseModule(".entry kernel() { .reg .u32 %r0, %r1, %r2, %r3; prmt.b32 %r0, %r1, %r2, 0x5410; prmt.b32.f4e %r0, %r1, %r2, %r3; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(checker::check(std::get<Prmt>(valid->functions.front().body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Prmt>(valid->functions.front().body[1]), context).has_value());
  const auto wrong = resolveModule(parseModule(".entry kernel() { .reg .u16 %r0; .reg .u32 %r1, %r2; prmt.b32 %r0, %r1, %r2, 0; }"));
  ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
  EXPECT_FALSE(checker::check(std::get<Prmt>(wrong->functions.front().body.front()), context).has_value());
}

TEST(ResolvedModule, ChecksM12PopcTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(".entry kernel() { .reg .u32 %dst, %src; popc.b32 %dst, %src; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction = std::get<Popc>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Popc::B32>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction, context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst; .reg .u32 %src; popc.b32 %dst, %src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .u16 %src; popc.b32 %dst, %src; }",
       }) {
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Popc>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12ClzTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(".entry kernel() { .reg .u32 %dst, %src32; .reg .u64 %src64; clz.b32 %dst, %src32; clz.b64 %dst, %src64; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Clz>(valid->functions.front().body[0]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<Clz>(valid->functions.front().body[1]), context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; clz.b64 %dst, %src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .u16 %src; clz.b32 %dst, %src; }",
       }) {
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Clz>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfindTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(".entry kernel() { .reg .u32 %dst, %src; bfind.shiftamt.u32 %dst, %src; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Bfind>(valid->functions.front().body.front()), context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .s32 %dst, %src; bfind.shiftamt.u32 %dst, %src; }",
           ".entry kernel() { .reg .u64 %dst, %src; bfind.shiftamt.u32 %dst, %src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %src; bfind.shiftamt.u32 %dst, %src; }",
       }) {
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Bfind>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfeTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; bfe.u32 %dst, %src, 0, 8; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Bfe>(valid->functions.front().body.front()), context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .s32 %dst, %src; bfe.u32 %dst, %src, 0, 8; }",
           ".entry kernel() { .reg .u64 %dst, %src; bfe.u32 %dst, %src, 0, 8; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %src; bfe.u32 %dst, %src, 0, 8; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Bfe>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfiTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(
      ".entry kernel() { .reg .u32 %dst, %insert, %base; bfi.b32 %dst, %insert, %base, 0, 8; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Bfi>(valid->functions.front().body.front()), context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .b64 %dst, %insert, %base; bfi.b32 %dst, %insert, %base, 0, 8; }",
           ".entry kernel() { .reg .u16 %dst, %insert, %base; bfi.b32 %dst, %insert, %base, 0, 8; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Bfi>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BrevTypes) {
  const auto context = checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto valid = resolveModule(parseModule(".entry kernel() { .reg .u32 %dst, %src; brev.b32 %dst, %src; }"));
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(std::get<Brev>(valid->functions.front().body.front()), context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst, %src; brev.b32 %dst, %src; }",
           ".entry kernel() { .reg .b64 %dst, %src; brev.b32 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto wrong = resolveModule(parseModule(source));
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(std::get<Brev>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ReportsRegistersMissingFromTheBoundScope) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %missing, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Unresolved instruction operand '%missing'.");
}

TEST(ResolvedModule, ResolvesNestedBlocksInSourceOrder) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %value;
  {
    .reg .u32 %value;
    add.u32 %value, %value, %value;
  }
  add.u32 %value, %value, %value;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);
  const auto& inner = resolvedIntegerAdd(body[0]);
  const auto& outer = resolvedIntegerAdd(body[1]);
  const auto& inner_value = inner.dst.value;
  const auto& outer_value = outer.dst.value;
  ASSERT_TRUE(inner_value.symbol_id.has_value());
  ASSERT_TRUE(outer_value.symbol_id.has_value());
  EXPECT_NE(inner_value.symbol_id, outer_value.symbol_id);
}

TEST(ResolvedModule, ResolvesFunctionLocalControlTargetsInsideNestedBlocks) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  {
    .reg .u32 %index;
    .reg .u64 %function_pointer;
label:
prototype: .callprototype _;
branches: .branchtargets label;
    bra label;
    brx.idx %index, branches;
    call %function_pointer, prototype;
  }
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto function_scope =
      *resolved->symbols.symbol(resolved->functions.front().symbol_id).owned_scope;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& branch = std::get<Bra::Direct>(
      std::get<Bra>(body[0]).variant);
  ASSERT_TRUE(branch.target.value.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*branch.target.value.symbol_id).scope,
            function_scope);
  const auto& indexed = std::get<Brx::Idx>(std::get<Brx>(body[1]).variant);
  ASSERT_TRUE(indexed.tlist.value.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*indexed.tlist.value.symbol_id).scope,
            function_scope);
  const auto& call = std::get<Call::Direct::TargetMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[2]).variant).operands);
  const auto& metadata =
      std::get<ResolvedIndirectMetadataRef>(call.metadata.value);
  ASSERT_TRUE(metadata.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*metadata.symbol_id).scope,
            function_scope);
}

TEST(ResolvedModule, DoesNotStageCallsAcrossNestedBlockBoundaries) {
  const auto ast = parseModule(R"ptx(
.func callee(.param .u32 input);
.entry caller() {
  .reg .u32 %value;
  .param .u32 outer_staging;
  st.param.u32 [outer_staging], %value;
  {
    .param .u32 inner_staging;
    st.param.u32 [inner_staging], %value;
    call callee, (inner_staging);
  }
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "A function-local .param argument store must be in the "
            "contiguous block immediately before a call that uses it.");
}

TEST(ResolvedModule, DistinguishesSpecialRegistersFromMissingDeclarations) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  add.u32 %dst, %laneid, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Special register '%laneid' is not supported by this resolved "
            "operand.");
}

TEST(ResolvedModule, ResolvesAndChecksSpecialRegisterMetadata) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  mov.u32 %dst, %laneid;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& u32 = std::get<Mov::Scalar>(mov.variant);
  const auto& scalar = scalarMovOperands(u32);
  const auto& special = std::get<ResolvedSpecialRegisterRef>(scalar.src.value);
  EXPECT_EQ(special.spelling, "%laneid");
  EXPECT_EQ(special.id.kind, base::SpecialRegisterKind::LaneId);
  EXPECT_FALSE(special.component.has_value());
  const auto special_info = base::metadata(special.id);
  EXPECT_EQ(special_info.element_type, ScalarType::U32);
  EXPECT_EQ(special_info.vector_width, 1u);
  EXPECT_EQ(special_info.minimum_ptx_major, 1u);
  EXPECT_EQ(special_info.minimum_ptx_minor, 3u);
  EXPECT_EQ(special_info.minimum_sm, 0u);

  const checker::Context too_old{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 2},
              .sm_version = 10,
          },
      .instruction_range = scalar.src.locs.front(),
  };
  const auto rejected = checker::check(mov, too_old);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().message,
            "Operand value '%laneid' requires PTX ISA >= 1.3, but target PTX "
            "ISA is 1.2.");

  checker::Context supported = too_old;
  supported.target.ptx_version = checker::PtxVersion{1, 3};
  EXPECT_TRUE(checker::check(mov, supported).has_value());
}

TEST(ResolvedModule, ChecksSpecialRegisterSmAndTypeRequirements) {
  PtxSyntaxParser cluster_parser("mov.u32 %r0, %cluster_ctarank;");
  const auto cluster_ast = cluster_parser.parseInstruction();
  ASSERT_TRUE(cluster_ast.has_value())
      << cluster_ast.diagnostics.front().message;
  const auto cluster_resolved = resolveInstruction(*cluster_ast);
  ASSERT_TRUE(cluster_resolved.has_value()) << cluster_resolved.error().message;
  const auto& cluster_mov = std::get<Mov>(*cluster_resolved);
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const auto cluster_check = checker::check(
      cluster_mov, checker::Context{
                       .target =
                           checker::TargetInfo{
                               .ptx_version = checker::PtxVersion{7, 8},
                               .sm_version = 80,
                               .capabilities = cluster_capabilities,
                           },
                       .instruction_range = cluster_ast->range,
                   });
  ASSERT_FALSE(cluster_check.has_value());
  ASSERT_EQ(cluster_check.error().size(), 1u);
  EXPECT_EQ(cluster_check.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  PtxSyntaxParser wide_parser("mov.u32 %r0, %clock64;");
  const auto wide_ast = wide_parser.parseInstruction();
  ASSERT_TRUE(wide_ast.has_value()) << wide_ast.diagnostics.front().message;
  const auto wide_resolved = resolveInstruction(*wide_ast);
  ASSERT_TRUE(wide_resolved.has_value()) << wide_resolved.error().message;
  const auto wide_check =
      checker::check(std::get<Mov>(*wide_resolved),
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{9, 3},
                                 .sm_version = 120,
                             },
                         .instruction_range = wide_ast->range,
                     });
  ASSERT_FALSE(wide_check.has_value());
  ASSERT_EQ(wide_check.error().size(), 1u);
  EXPECT_EQ(wide_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(wide_check.error().front().message,
            "Special-register operand 'src' has declared type 'U64' but "
            "instruction type source 'type' is 'U32'.");
}

TEST(ResolvedModule, ResolvesAndChecksSmemAndGraphSpecialRegisters) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b<5>;
  .reg .u32 %r<4>;
  .reg .u64 %rd;
  mov.b32 %b0, %reserved_smem_offset_begin;
  mov.b32 %b1, %reserved_smem_offset_end;
  mov.b32 %b2, %reserved_smem_offset_cap;
  mov.b32 %b3, %reserved_smem_offset_0;
  mov.b32 %b4, %reserved_smem_offset_1;
  mov.u32 %r0, %total_smem_size;
  mov.u32 %r1, %dynamic_smem_size;
  mov.u32 %r2, %aggr_smem_size;
  mov.u64 %rd, %current_graph_exec;
  mov.u32 %r3, %current_graph_exec;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  constexpr std::array<std::string_view, 3> capabilities{
      "reserved_smem", "aggregate_smem", "graph_exec"};

  constexpr std::array spellings{
      "%reserved_smem_offset_begin", "%reserved_smem_offset_end",
      "%reserved_smem_offset_cap", "%reserved_smem_offset_0",
      "%reserved_smem_offset_1", "%total_smem_size", "%dynamic_smem_size",
      "%aggr_smem_size", "%current_graph_exec",
  };
  for (size_t index = 0; index < spellings.size(); ++index) {
    const auto& special = std::get<ResolvedSpecialRegisterRef>(
        scalarMovOperands(std::get<Mov>(body[index])).src.value);
    EXPECT_EQ(special.spelling, spellings[index]);
    ASSERT_TRUE(base::lookup(spellings[index]).has_value());
    EXPECT_EQ(special.id, base::lookup(spellings[index])->id);
  }

  const auto check_at = [&](size_t index, checker::PtxVersion version,
                            uint32_t sm) {
    return checker::check(std::get<Mov>(body[index]),
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = version,
                                      .sm_version = sm,
                                      .capabilities = capabilities,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  struct AvailabilityBoundary {
    size_t instruction;
    checker::PtxVersion supported;
    checker::PtxVersion too_old_ptx;
    uint32_t minimum_sm;
  };
  for (const AvailabilityBoundary boundary : {
           AvailabilityBoundary{0, {7, 6}, {7, 5}, 80},
           AvailabilityBoundary{5, {4, 1}, {4, 0}, 20},
           AvailabilityBoundary{7, {8, 1}, {8, 0}, 90},
           AvailabilityBoundary{8, {8, 0}, {7, 9}, 50},
       }) {
    EXPECT_TRUE(check_at(boundary.instruction, boundary.supported,
                         boundary.minimum_sm)
                    .has_value());
    const auto ptx_rejected = check_at(boundary.instruction,
                                       boundary.too_old_ptx,
                                       boundary.minimum_sm);
    ASSERT_FALSE(ptx_rejected.has_value());
    const auto info = base::metadata(
        std::get<ResolvedSpecialRegisterRef>(
            scalarMovOperands(std::get<Mov>(body[boundary.instruction]))
                .src.value)
            .id);
    EXPECT_EQ(ptx_rejected.error().front().kind,
              info.required_capability.empty()
                  ? checker::CheckDiagnosticKind::UnsupportedPtxVersion
                  : checker::CheckDiagnosticKind::UnsupportedAvailability);
    const auto sm_rejected = check_at(boundary.instruction, boundary.supported,
                                      boundary.minimum_sm - 1);
    ASSERT_FALSE(sm_rejected.has_value());
    EXPECT_EQ(sm_rejected.error().front().kind,
              info.required_capability.empty()
                  ? checker::CheckDiagnosticKind::UnsupportedSmVersion
                  : checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto wrong_type = check_at(9, {9, 3}, 100);
  ASSERT_FALSE(wrong_type.has_value());
  EXPECT_EQ(wrong_type.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesScalarSpecialRegisterComponentsOnly) {
  PtxSyntaxParser component_parser("mov.u32 %r0, %tid.x;");
  const auto component_ast = component_parser.parseInstruction();
  ASSERT_TRUE(component_ast.has_value())
      << component_ast.diagnostics.front().message;
  const auto component_resolved = resolveInstruction(*component_ast);
  ASSERT_TRUE(component_resolved.has_value())
      << component_resolved.error().message;
  const auto& component = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(*component_resolved)).src.value);
  EXPECT_EQ(component.spelling, "%tid.x");
  EXPECT_EQ(component.id.kind, base::SpecialRegisterKind::Tid);
  EXPECT_EQ(component.component, base::VectorComponent::X);
  const auto component_info = base::metadata(component.id);
  EXPECT_EQ(component_info.vector_width, 4u);
  EXPECT_EQ(component_info.minimum_ptx_major, 2u);

  PtxSyntaxParser vector_parser("mov.u32 %r0, %tid;");
  const auto vector_ast = vector_parser.parseInstruction();
  ASSERT_TRUE(vector_ast.has_value())
      << vector_ast.diagnostics.front().message;
  const auto vector_resolved = resolveInstruction(*vector_ast);
  ASSERT_FALSE(vector_resolved.has_value());
  EXPECT_EQ(vector_resolved.error().message,
            "Special register '%tid' is a vector; select a scalar component.");
}

TEST(ResolvedModule, ResolvesBoundSymbolsAndAddressBases) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u64 %rd<2>;
  .reg .u32 %r<3>;
  mov.u64 %rd0, global_value;
  ld.u32 %r0, [global_value+4];
  ld.u32 %r1, [%rd0-8];
  ld.u32 %r2, [240];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& mov = std::get<Mov>(body[0]);
  const auto& mov_symbol =
      std::get<ResolvedSymbolRef>(scalarMovOperands(mov).src.value);
  ASSERT_TRUE(mov_symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*mov_symbol.symbol_id).name,
            "global_value");
  EXPECT_EQ(mov_symbol.declaration_kind, binding::SymbolKind::Variable);
  EXPECT_EQ(mov_symbol.declaration_state_space,
            syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(mov_symbol.address_state_space, syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(mov_symbol.declared_type, ScalarType::U32);

  const auto& symbol_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[1]).variant).address.value;
  const auto& symbol_base = std::get<ResolvedSymbolRef>(symbol_address.base);
  EXPECT_EQ(symbol_base.symbol_id, mov_symbol.symbol_id);
  ASSERT_TRUE(symbol_address.offset.has_value());
  EXPECT_EQ(symbol_address.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(symbol_address.offset->value.type, ScalarType::S64);
  EXPECT_EQ(symbol_address.offset->value.bits, 4u);

  const auto& register_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[2]).variant).address.value;
  const auto& register_base =
      std::get<ResolvedRegisterRef>(register_address.base);
  ASSERT_TRUE(register_base.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*register_base.symbol_id).name, "%rd");
  EXPECT_EQ(register_base.parameterized_index, 0u);
  ASSERT_TRUE(register_address.offset.has_value());
  EXPECT_EQ(register_address.offset->operation,
            ResolvedAddressOffsetOperator::Subtract);
  EXPECT_EQ(register_address.offset->value.bits, 8u);

  const auto& immediate_address =
      std::get<Ld::GenericScalar>(std::get<Ld>(body[3]).variant).address.value;
  const auto& immediate_base =
      std::get<ResolvedImmediate>(immediate_address.base);
  EXPECT_EQ(immediate_base.type, ScalarType::U64);
  EXPECT_EQ(immediate_base.bits, 240u);
}

TEST(ResolvedModule, ChecksGenericLoadAvailability) {
  PtxSyntaxParser parser("ld.u32 %r0, [%rd0+4];");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& load = std::get<Ld>(*resolved);

  const auto rejected =
      checker::check(load, checker::Context{
                               .target =
                                   checker::TargetInfo{
                                       .ptx_version = checker::PtxVersion{1, 5},
                                       .sm_version = 10,
                                   },
                               .instruction_range = ast->range,
                           });
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  EXPECT_TRUE(
      checker::check(load,
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{2, 0},
                                 .sm_version = 20,
                             },
                         .instruction_range = ast->range,
                     })
          .has_value());
}

TEST(ResolvedModule, ChecksGenericLoadStoreAddressStateSpacePolicy) {
  const auto ast = parseModule(R"ptx(
.const .u32 constant_value;
.global .u32 global_value;
.entry kernel(.param .u32 input) {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  .local .u32 local_value;
  .shared .u32 shared_value;
  ld.u32 %r0, [global_value];
  ld.u32 %r0, [local_value];
  ld.u32 %r0, [shared_value];
  ld.u32 %r0, [constant_value];
  ld.u32 %r0, [input];
  ld.u32 %r0, [%rd0];
  st.u32 [global_value], %r0;
  st.u32 [local_value], %r0;
  st.u32 [shared_value], %r0;
  st.u32 [constant_value], %r0;
  st.u32 [input], %r0;
  st.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 12u);
  const checker::Context generic_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  for (const size_t index : {0u, 1u, 2u, 5u}) {
    EXPECT_TRUE(checker::check(std::get<Ld>(body[index]), generic_context)
                    .has_value());
  }
  for (const size_t index : {6u, 7u, 8u, 11u}) {
    EXPECT_TRUE(checker::check(std::get<St>(body[index]), generic_context)
                    .has_value());
  }

  auto const_context = generic_context;
  const_context.target.ptx_version = {3, 0};
  const auto old_const_load =
      checker::check(std::get<Ld>(body[3]), const_context);
  ASSERT_FALSE(old_const_load.has_value());
  ASSERT_EQ(old_const_load.error().size(), 1u);
  EXPECT_EQ(old_const_load.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(old_const_load.error().front().range,
            std::get<Ld::GenericScalar>(std::get<Ld>(body[3]).variant)
                .address.locs.front());
  const_context.target.ptx_version = {3, 1};
  EXPECT_TRUE(checker::check(std::get<Ld>(body[3]), const_context).has_value());

  const auto expect_mismatch = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, generic_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  };
  expect_mismatch(std::get<Ld>(body[4]));
  expect_mismatch(std::get<St>(body[9]));
  expect_mismatch(std::get<St>(body[10]));
}

TEST(ResolvedModule, ResolvesGenericAndExplicitScalarLoadStoreForms) {
  const auto resolve_standalone = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::move(*resolved);
  };

  const auto generic_load = resolve_standalone("ld.u32 %r0, [%rd0];");
  const auto global_load =
      resolve_standalone("ld.global.u32 %r0, [%rd0];");
  const auto generic_store = resolve_standalone("st.u32 [%rd0], %r0;");
  const auto global_store =
      resolve_standalone("st.global.u32 [%rd0], %r0;");

  EXPECT_TRUE(
      std::holds_alternative<Ld::GenericScalar>(
          std::get<Ld>(generic_load).variant));
  EXPECT_TRUE(
      std::holds_alternative<Ld::ExplicitScalar>(
          std::get<Ld>(global_load).variant));
  EXPECT_TRUE(std::holds_alternative<St::GenericScalar>(
      std::get<St>(generic_store).variant));
  EXPECT_TRUE(std::holds_alternative<St::ExplicitScalar>(
      std::get<St>(global_store).variant));
  EXPECT_EQ(
      std::get<Ld::ExplicitScalar>(std::get<Ld>(global_load).variant)
          .state_space.value,
      MemoryStateSpace::Global);
  EXPECT_EQ(
      std::get<St::ExplicitScalar>(std::get<St>(global_store).variant)
          .state_space.value,
      MemoryStateSpace::Global);

  const checker::Context old_target{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
  };
  const auto generic_store_check =
      checker::check(std::get<St>(generic_store), old_target);
  ASSERT_FALSE(generic_store_check.has_value());
  ASSERT_EQ(generic_store_check.error().size(), 2u);
  EXPECT_EQ(generic_store_check.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(generic_store_check.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
}

TEST(ResolvedModule, ResolvesEveryLegalLoadStoreCacheOperator) {
  struct CacheCase {
    std::string_view spelling;
    CacheOperator value;
  };
  constexpr CacheCase load_cases[] = {
      {"ca", CacheOperator::Ca}, {"cg", CacheOperator::Cg},
      {"cs", CacheOperator::Cs}, {"lu", CacheOperator::Lu},
      {"cv", CacheOperator::Cv},
  };
  constexpr CacheCase store_cases[] = {
      {"wb", CacheOperator::Wb}, {"cg", CacheOperator::Cg},
      {"cs", CacheOperator::Cs}, {"wt", CacheOperator::Wt},
  };
  const auto resolve = [](const std::string& source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*ast), std::move(*resolved)};
  };
  const checker::Context context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
  };

  for (const auto& cache : load_cases) {
    SCOPED_TRACE(cache.spelling);
    auto [generic_ast, generic_instruction] =
        resolve(fmt::format("ld.{}.u32 %r0, [%rd0];", cache.spelling));
    const auto& generic =
        std::get<Ld::GenericScalar>(std::get<Ld>(generic_instruction).variant);
    EXPECT_EQ(generic.cache.value, cache.value);
    ASSERT_EQ(generic.cache.locs.size(), 1u);
    EXPECT_EQ(generic.cache.locs.front(), generic_ast.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(std::get<Ld>(generic_instruction), context)
                    .has_value());

    auto [explicit_ast, explicit_instruction] = resolve(
        fmt::format("ld.global.{}.u32 %r0, [%rd0];", cache.spelling));
    const auto& explicit_load =
        std::get<Ld::ExplicitScalar>(std::get<Ld>(explicit_instruction).variant);
    EXPECT_EQ(explicit_load.cache.value, cache.value);
    ASSERT_EQ(explicit_load.cache.locs.size(), 1u);
    EXPECT_EQ(explicit_load.cache.locs.front(),
              explicit_ast.modifiers[1].syntax.range);
    EXPECT_TRUE(checker::check(std::get<Ld>(explicit_instruction), context)
                    .has_value());
  }

  for (const auto& cache : store_cases) {
    SCOPED_TRACE(cache.spelling);
    auto [generic_ast, generic_instruction] =
        resolve(fmt::format("st.{}.u32 [%rd0], %r0;", cache.spelling));
    const auto& generic =
        std::get<St::GenericScalar>(std::get<St>(generic_instruction).variant);
    EXPECT_EQ(generic.cache.value, cache.value);
    ASSERT_EQ(generic.cache.locs.size(), 1u);
    EXPECT_EQ(generic.cache.locs.front(), generic_ast.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(std::get<St>(generic_instruction), context)
                    .has_value());

    auto [explicit_ast, explicit_instruction] = resolve(
        fmt::format("st.global.{}.u32 [%rd0], %r0;", cache.spelling));
    const auto& explicit_store =
        std::get<St::ExplicitScalar>(std::get<St>(explicit_instruction).variant);
    EXPECT_EQ(explicit_store.cache.value, cache.value);
    ASSERT_EQ(explicit_store.cache.locs.size(), 1u);
    EXPECT_EQ(explicit_store.cache.locs.front(),
              explicit_ast.modifiers[1].syntax.range);
    EXPECT_TRUE(checker::check(std::get<St>(explicit_instruction), context)
                    .has_value());
  }
}

TEST(ResolvedModule, ChecksExplicitCacheOperatorAvailabilityAndOmittedBaseline) {
  const auto resolve = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*ast), std::move(*resolved)};
  };
  const checker::Context old_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 10},
  };
  const checker::Context supported_target{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
  };

  auto [cached_load_ast, cached_load_instruction] =
      resolve("ld.global.ca.u32 %r0, [%rd0];");
  const auto cached_load =
      checker::check(std::get<Ld>(cached_load_instruction), old_target);
  ASSERT_FALSE(cached_load.has_value());
  ASSERT_EQ(cached_load.error().size(), 2u);
  EXPECT_EQ(cached_load.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(cached_load.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(cached_load.error()[0].range,
            cached_load_ast.modifiers[1].syntax.range);
  EXPECT_TRUE(
      checker::check(std::get<Ld>(cached_load_instruction), supported_target)
          .has_value());

  auto [cached_store_ast, cached_store_instruction] =
      resolve("st.global.wb.u32 [%rd0], %r0;");
  const auto cached_store =
      checker::check(std::get<St>(cached_store_instruction), old_target);
  ASSERT_FALSE(cached_store.has_value());
  ASSERT_EQ(cached_store.error().size(), 2u);
  EXPECT_EQ(cached_store.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(cached_store.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(cached_store.error()[0].range,
            cached_store_ast.modifiers[1].syntax.range);
  EXPECT_TRUE(
      checker::check(std::get<St>(cached_store_instruction), supported_target)
          .has_value());

  auto [implicit_load_ast, implicit_load_instruction] =
      resolve("ld.global.u32 %r0, [%rd0];");
  (void)implicit_load_ast;
  EXPECT_TRUE(checker::check(std::get<Ld>(implicit_load_instruction), old_target)
                  .has_value());

  auto [implicit_store_ast, implicit_store_instruction] =
      resolve("st.global.u32 [%rd0], %r0;");
  (void)implicit_store_ast;
  EXPECT_TRUE(
      checker::check(std::get<St>(implicit_store_instruction), old_target)
          .has_value());
}

TEST(ResolvedModule, ResolvesAndChecksLoadStoreScalarTypeFamily) {
  struct ScalarCase {
    std::string_view spelling;
    ScalarType type;
  };
  constexpr ScalarCase scalar_cases[] = {
      {"b8", ScalarType::B8},   {"b16", ScalarType::B16},
      {"b32", ScalarType::B32}, {"b64", ScalarType::B64},
      {"u8", ScalarType::U8},   {"u16", ScalarType::U16},
      {"u32", ScalarType::U32}, {"u64", ScalarType::U64},
      {"s8", ScalarType::S8},   {"s16", ScalarType::S16},
      {"s32", ScalarType::S32}, {"s64", ScalarType::S64},
      {"f32", ScalarType::F32}, {"f64", ScalarType::F64},
  };
  const auto resolve = [](const std::string& source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const SourceRange type_range = ast->modifiers.back().syntax.range;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*resolved), type_range};
  };
  const checker::Context context{
      .target = {.ptx_version = {3, 1}, .sm_version = 20},
  };

  for (const auto& scalar : scalar_cases) {
    SCOPED_TRACE(scalar.spelling);
    auto [generic_load, generic_load_type_range] = resolve(
        fmt::format("ld.{} %r0, [%rd0];", scalar.spelling));
    const auto& generic_load_variant =
        std::get<Ld::GenericScalar>(std::get<Ld>(generic_load).variant);
    EXPECT_EQ(generic_load_variant.type.value, scalar.type);
    ASSERT_EQ(generic_load_variant.type.locs.size(), 1u);
    EXPECT_EQ(generic_load_variant.type.locs.front(), generic_load_type_range);
    EXPECT_TRUE(checker::check(std::get<Ld>(generic_load), context).has_value());

    auto [explicit_load, explicit_load_type_range] = resolve(
        fmt::format("ld.global.{} %r0, [%rd0];", scalar.spelling));
    const auto& explicit_load_variant =
        std::get<Ld::ExplicitScalar>(std::get<Ld>(explicit_load).variant);
    EXPECT_EQ(explicit_load_variant.type.value, scalar.type);
    ASSERT_EQ(explicit_load_variant.type.locs.size(), 1u);
    EXPECT_EQ(explicit_load_variant.type.locs.front(),
              explicit_load_type_range);
    EXPECT_TRUE(
        checker::check(std::get<Ld>(explicit_load), context).has_value());

    auto [generic_store, generic_store_type_range] = resolve(
        fmt::format("st.{} [%rd0], %r0;", scalar.spelling));
    const auto& generic_store_variant =
        std::get<St::GenericScalar>(std::get<St>(generic_store).variant);
    EXPECT_EQ(generic_store_variant.type.value, scalar.type);
    ASSERT_EQ(generic_store_variant.type.locs.size(), 1u);
    EXPECT_EQ(generic_store_variant.type.locs.front(),
              generic_store_type_range);
    EXPECT_TRUE(
        checker::check(std::get<St>(generic_store), context).has_value());

    auto [explicit_store, explicit_store_type_range] = resolve(
        fmt::format("st.global.{} [%rd0], %r0;", scalar.spelling));
    const auto& explicit_store_variant =
        std::get<St::ExplicitScalar>(std::get<St>(explicit_store).variant);
    EXPECT_EQ(explicit_store_variant.type.value, scalar.type);
    ASSERT_EQ(explicit_store_variant.type.locs.size(), 1u);
    EXPECT_EQ(explicit_store_variant.type.locs.front(),
              explicit_store_type_range);
    EXPECT_TRUE(
        checker::check(std::get<St>(explicit_store), context).has_value());
  }
}

TEST(ResolvedModule, ChecksLoadStoreF64Availability) {
  const auto resolve = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const SourceRange type_range = ast->modifiers.back().syntax.range;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::pair{std::move(*resolved), type_range};
  };

  auto [explicit_instruction, explicit_type_range] =
      resolve("ld.global.f64 %fd0, [%rd0];");
  const auto& explicit_load = std::get<Ld>(explicit_instruction);
  const checker::Context sm12_context{
      .target = {.ptx_version = {1, 0}, .sm_version = 12},
  };
  const auto explicit_rejected = checker::check(explicit_load, sm12_context);
  ASSERT_FALSE(explicit_rejected.has_value());
  ASSERT_EQ(explicit_rejected.error().size(), 1u);
  EXPECT_EQ(explicit_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(explicit_rejected.error().front().range, explicit_type_range);

  auto sm13_context = sm12_context;
  sm13_context.target.sm_version = 13;
  EXPECT_TRUE(checker::check(explicit_load, sm13_context).has_value());

  auto [explicit_vector_instruction, explicit_vector_type_range] =
      resolve("ld.global.v2.f64 {%fd0, %fd1}, [%rd0];");
  const auto& explicit_vector_load = std::get<Ld>(explicit_vector_instruction);
  const auto explicit_vector_rejected =
      checker::check(explicit_vector_load, sm12_context);
  ASSERT_FALSE(explicit_vector_rejected.has_value());
  ASSERT_EQ(explicit_vector_rejected.error().size(), 1u);
  EXPECT_EQ(explicit_vector_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_EQ(explicit_vector_rejected.error().front().range,
            explicit_vector_type_range);
  EXPECT_TRUE(checker::check(explicit_vector_load, sm13_context).has_value());

  auto [generic_instruction, generic_type_range] =
      resolve("ld.f64 %fd0, [%rd0];");
  const auto& generic_load = std::get<Ld>(generic_instruction);
  const auto& generic_variant =
      std::get<Ld::GenericScalar>(generic_load.variant);
  ASSERT_EQ(generic_variant.type.locs.size(), 1u);
  EXPECT_EQ(generic_variant.type.locs.front(), generic_type_range);
  const checker::Context generic_sm13_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 13},
  };
  const auto generic_rejected =
      checker::check(generic_load, generic_sm13_context);
  ASSERT_FALSE(generic_rejected.has_value());
  ASSERT_EQ(generic_rejected.error().size(), 1u);
  EXPECT_EQ(generic_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  auto generic_sm20_context = generic_sm13_context;
  generic_sm20_context.target.sm_version = 20;
  EXPECT_TRUE(checker::check(generic_load, generic_sm20_context).has_value());
}

TEST(ResolvedModule, ChecksBoundLoadStoreRegisterWidthPolicy) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %u32_value;
  .reg .u64 %u64_value;
  .reg .b64 %b64_value;
  .reg .b128 %b128_value;
  .reg .f64 %f64_value;
  ld.u8 %u32_value, [%rd0];
  ld.global.s16 %u64_value, [%rd0];
  ld.b16 %f64_value, [%rd0];
  ld.global.f32 %b64_value, [%rd0];
  st.u8 [%rd0], %u32_value;
  st.global.s16 [%rd0], %u64_value;
  st.b16 [%rd0], %f64_value;
  st.global.f32 [%rd0], %b64_value;
  ld.u64 %u32_value, [%rd0];
  st.u64 [%rd0], %u32_value;
  ld.f32 %f64_value, [%rd0];
  st.f32 [%rd0], %f64_value;
  ld.u32 %f64_value, [%rd0];
  st.u32 [%rd0], %f64_value;
  ld.u32 %b128_value, [%rd0];
  st.u32 [%rd0], %b128_value;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 16u);
  const checker::Context context{
      .target = {.ptx_version = {8, 3}, .sm_version = 70},
      .instruction_range = ast.range,
  };

  for (size_t index = 0; index < 8; ++index) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    EXPECT_TRUE(checked.has_value());
  }

  for (size_t index = 8; index < body.size(); ++index) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ResolvesAndChecksLegacyLoadStoreRegisterVectors) {
  const auto ast = parseModule(R"ptx(
.global .align 8 .u32 global_value;
.shared .align 8 .u16 shared_value;
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r<4>;
  .reg .u16 %h<4>;
  ld.v2.u32 {%r0, %r1}, [%rd0];
  ld.cg.v4.u16 {%h0, %h1, %h2, %h3}, [%rd0];
  ld.global.v2.u32 {%r0, %r1}, [global_value];
  ld.shared.v4.u16 {%h0, %h1, %h2, %h3}, [shared_value];
  st.v2.u32 [%rd0], {%r0, %r1};
  st.wt.v4.u16 [%rd0], {%h0, %h1, %h2, %h3};
  st.global.v2.u32 [global_value], {%r0, %r1};
  st.shared.v4.u16 [shared_value], {%h0, %h1, %h2, %h3};
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);

  const auto& generic_load =
      std::get<Ld::GenericVector>(std::get<Ld>(body[0]).variant);
  EXPECT_EQ(generic_load.vector.value, VectorArity::V2);
  EXPECT_EQ(generic_load.type.value, ScalarType::U32);
  EXPECT_FALSE(generic_load.vector.locs.empty());
  ASSERT_EQ(generic_load.dst.value.elements.size(), 2u);
  EXPECT_EQ(generic_load.dst.value.elements[0]->declared_type,
            ScalarType::U32);

  const auto& cached_load =
      std::get<Ld::GenericVector>(std::get<Ld>(body[1]).variant);
  EXPECT_EQ(cached_load.cache.value, CacheOperator::Cg);
  EXPECT_EQ(cached_load.vector.value, VectorArity::V4);

  const auto& explicit_load =
      std::get<Ld::ExplicitVector>(std::get<Ld>(body[2]).variant);
  EXPECT_EQ(explicit_load.state_space.value, MemoryStateSpace::Global);
  EXPECT_EQ(explicit_load.vector.value, VectorArity::V2);

  const auto& generic_store =
      std::get<St::GenericVector>(std::get<St>(body[4]).variant);
  EXPECT_EQ(generic_store.vector.value, VectorArity::V2);
  ASSERT_EQ(generic_store.src.value.elements.size(), 2u);
  EXPECT_EQ(generic_store.src.value.elements[1]->declared_type,
            ScalarType::U32);

  const auto& cached_store =
      std::get<St::GenericVector>(std::get<St>(body[5]).variant);
  EXPECT_EQ(cached_store.cache.value, CacheOperator::Wt);
  EXPECT_EQ(cached_store.vector.value, VectorArity::V4);

  const checker::Context current{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body) {
    const auto checked = std::visit(
        [&](const auto& value) { return checker::check(value, current); },
        instruction);
    EXPECT_TRUE(checked.has_value());
  }

  const checker::Context old_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast.range,
  };
  const auto old_generic = checker::check(std::get<Ld>(body[0]), old_target);
  ASSERT_FALSE(old_generic.has_value());
  EXPECT_EQ(old_generic.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(checker::check(std::get<Ld>(body[2]), old_target).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[6]), old_target).has_value());
}

TEST(ResolvedModule, ChecksBoundAndImmediateAddressAlignment) {
  const auto ast = parseModule(R"ptx(
.global .u32 scalar_value;
.global .align 16 .u32 vector_value;
.global .f16x2 half2_value;
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r<4>;
  ld.global.u32 %r0, [scalar_value];
  ld.global.u32 %r0, [scalar_value+2];
  ld.global.v4.u32 {%r0, %r1, %r2, %r3}, [vector_value];
  st.global.v4.u32 [vector_value+8], {%r0, %r1, %r2, %r3};
  ld.global.u32 %r0, [4];
  ld.global.u32 %r0, [2];
  ld.global.u32 %r0, [%rd0];
  ld.global.b32 %r0, [half2_value];
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  const checker::Context context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };
  for (const size_t index : {0u, 2u, 4u, 6u, 7u}) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    EXPECT_TRUE(checked.has_value());
  }
  for (const size_t index : {1u, 3u, 5u}) {
    const auto checked = std::visit(
        [&](const auto& instruction) { return checker::check(instruction, context); },
        body[index]);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::AddressAlignmentMismatch);
  }
}

TEST(ResolvedModule, RejectsInvalidLegacyLoadStoreRegisterVectors) {
  const auto resolve_source = [](std::string_view instruction) {
    const std::string source = fmt::format(R"ptx(
.entry kernel() {{
  .reg .u64 %rd0;
  .reg .u32 %r<4>;
  .reg .u16 %h<4>;
  .reg .f32 %f<4>;
  {}
}}
)ptx",
                                           instruction);
    return resolveModule(parseModule(source));
  };

  const auto v8_arity_mismatch =
      resolve_source("ld.v8.u32 {%r0, %r1}, [%rd0];");
  ASSERT_FALSE(v8_arity_mismatch.has_value());
  EXPECT_EQ(v8_arity_mismatch.error().front().message,
            "This vector operand requires 8 elements.");

  const auto scalar_load = resolve_source("ld.v2.u32 %r0, [%rd0];");
  ASSERT_FALSE(scalar_load.has_value());
  EXPECT_EQ(scalar_load.error().front().message,
            "Operands do not match any layout of instruction variant "
            "'GenericVector'.");

  const auto scalar_store = resolve_source("st.v2.u32 [%rd0], %r0;");
  ASSERT_FALSE(scalar_store.has_value());
  EXPECT_EQ(scalar_store.error().front().message,
            "Operands do not match any layout of instruction variant "
            "'GenericVector'.");

  const auto arity_mismatch =
      resolve_source("ld.v4.u32 {%r0, %r1}, [%rd0];");
  ASSERT_FALSE(arity_mismatch.has_value());
  EXPECT_EQ(arity_mismatch.error().front().message,
            "This vector operand requires 4 elements.");

  const auto sink = resolve_source("st.v2.u32 [%rd0], {%r0, _};");
  ASSERT_FALSE(sink.has_value());
  EXPECT_EQ(sink.error().front().message,
            "The '_' sink is allowed only in a 256-bit memory vector.");

  EXPECT_TRUE(resolve_source("ld.v2.u16 {%r0, %r1}, [%rd0];").has_value());

  const auto narrow = resolve_source("ld.v2.u32 {%h0, %h1}, [%rd0];");
  ASSERT_FALSE(narrow.has_value());
  EXPECT_EQ(narrow.error().front().message,
            "Vector element '%h0' has type 'U16' incompatible with this "
            "instruction.");

  const auto kind_mismatch = resolve_source("ld.v2.u32 {%f0, %f1}, [%rd0];");
  ASSERT_FALSE(kind_mismatch.has_value());
  EXPECT_EQ(kind_mismatch.error().front().message,
            "Vector element '%f0' has type 'F32' incompatible with this "
            "instruction.");
}

TEST(ResolvedModule, ChecksModernLoadStoreRegisterVectors) {
  const auto ast = parseModule(R"ptx(
.global .align 32 .u64 global64;
.shared .u32 shared32;
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u32 %r<8>;
  .reg .u64 %d<4>;
  ld.v8.u32 {_, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];
  st.global.v4.u64 [global64], {_, %d1, %d2, %d3};
  st.global.v4.u64 [global64+8], {_, %d1, %d2, %d3};
  ld.v4.u64 {%d0, %d1, %d2, %d3}, [%rd0];
  ld.shared.v8.u32 {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [shared32];
  ld.v8.u32 {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [shared32];
  ld.v8.u16 {%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7}, [%rd0];
  st.v8.u32 [%rd0], {_, %r1, %r2, %r3, %r4, %r5, %r6, %r7};
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);

  const checker::Context supported{
      .target = {.ptx_version = {8, 8}, .sm_version = 100},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Ld>(body[0]), supported).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[1]), supported).has_value());
  EXPECT_TRUE(checker::check(std::get<Ld>(body[3]), supported).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[7]), supported).has_value());

  const auto underaligned = checker::check(std::get<St>(body[2]), supported);
  ASSERT_FALSE(underaligned.has_value());
  EXPECT_EQ(underaligned.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);

  const auto explicit_non_global = checker::check(std::get<Ld>(body[4]), supported);
  ASSERT_FALSE(explicit_non_global.has_value());
  EXPECT_EQ(explicit_non_global.error().front().kind,
            checker::CheckDiagnosticKind::RuleViolation);
  const auto generic_bound_non_global =
      checker::check(std::get<Ld>(body[5]), supported);
  ASSERT_FALSE(generic_bound_non_global.has_value());
  EXPECT_EQ(generic_bound_non_global.error().front().kind,
            checker::CheckDiagnosticKind::RuleViolation);
  const auto wrong_width = checker::check(std::get<Ld>(body[6]), supported);
  ASSERT_FALSE(wrong_width.has_value());
  EXPECT_EQ(wrong_width.error().front().kind,
            checker::CheckDiagnosticKind::RuleViolation);

  auto old_ptx = supported;
  old_ptx.target.ptx_version = {8, 7};
  const auto ptx_rejected = checker::check(std::get<Ld>(body[3]), old_ptx);
  ASSERT_FALSE(ptx_rejected.has_value());
  EXPECT_EQ(ptx_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  auto old_sm = supported;
  old_sm.target.sm_version = 90;
  const auto sm_rejected = checker::check(std::get<Ld>(body[3]), old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto overwide = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u64 %d<8>;
  ld.v8.u64 {%d0, %d1, %d2, %d3, %d4, %d5, %d6, %d7}, [%rd0];
}
)ptx");
  const auto overwide_resolved = resolveModule(overwide);
  ASSERT_FALSE(overwide_resolved.has_value());
  EXPECT_EQ(overwide_resolved.error().front().message,
            "This vector operand's payload width (512 bits) exceeds the "
            "supported 256 bit limit.");

  const auto all_sinks = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  ld.v8.u32 {_, _, _, _, _, _, _, _}, [%rd0];
}
)ptx");
  const auto all_sinks_resolved = resolveModule(all_sinks);
  ASSERT_FALSE(all_sinks_resolved.has_value());
  EXPECT_EQ(all_sinks_resolved.error().front().message,
            "A vector must contain at least one register.");
}

TEST(ResolvedModule, KeepsNonMemoryRegisterWidthChecksStrict) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %narrow;
  .reg .u64 %wide;
  mov.u32 %wide, %narrow;
  add.u32 %wide, %narrow, %narrow;
  sub.u32 %wide, %narrow, %narrow;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const checker::Context context{
      .target = {.ptx_version = {3, 1}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  for (const auto& instruction : body) {
    const auto checked = std::visit(
        [&](const auto& value) { return checker::check(value, context); },
        instruction);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksBasicExplicitAddressStateSpaces) {
  const auto ast = parseModule(R"ptx(
.const .u32 constant_value;
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  .local .u32 local_value;
  .shared .u32 shared_value;
  ld.const.u32 %r0, [constant_value];
  ld.global.u32 %r0, [global_value];
  ld.local.u32 %r0, [local_value];
  ld.shared.u32 %r0, [shared_value];
  st.global.u32 [global_value], %r0;
  st.local.u32 [local_value], %r0;
  st.shared.u32 [shared_value], %r0;
  ld.shared.u32 %r0, [global_value];
  ld.local.u32 %r0, [%rd0];
  st.shared.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto& syntax_function =
      std::get<syntax_ast::AstFunction>(ast.items[2]);
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast.range,
  };

  constexpr std::array expected_load_spaces{
      MemoryStateSpace::Constant, MemoryStateSpace::Global,
      MemoryStateSpace::Local, MemoryStateSpace::Shared};
  for (size_t index = 0; index < expected_load_spaces.size(); ++index) {
    const auto& load = std::get<Ld>(body[index]);
    const auto& explicit_load = std::get<Ld::ExplicitScalar>(load.variant);
    EXPECT_EQ(explicit_load.state_space.value, expected_load_spaces[index]);
    ASSERT_EQ(explicit_load.state_space.locs.size(), 1u);
    const auto& syntax_instruction =
        std::get<syntax_ast::AstInstruction>(syntax_function.body[4 + index]);
    EXPECT_EQ(explicit_load.state_space.locs.front(),
              syntax_instruction.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(load, context).has_value());
  }

  constexpr std::array expected_store_spaces{
      MemoryStateSpace::Global, MemoryStateSpace::Local,
      MemoryStateSpace::Shared};
  for (size_t offset = 0; offset < expected_store_spaces.size(); ++offset) {
    const auto& store = std::get<St>(body[4 + offset]);
    const auto& explicit_store = std::get<St::ExplicitScalar>(store.variant);
    EXPECT_EQ(explicit_store.state_space.value, expected_store_spaces[offset]);
    ASSERT_EQ(explicit_store.state_space.locs.size(), 1u);
    const auto& syntax_instruction = std::get<syntax_ast::AstInstruction>(
        syntax_function.body[8 + offset]);
    EXPECT_EQ(explicit_store.state_space.locs.front(),
              syntax_instruction.modifiers.front().syntax.range);
    EXPECT_TRUE(checker::check(store, context).has_value());
  }

  const auto& mismatch_load = std::get<Ld>(body[7]);
  const auto mismatch = checker::check(mismatch_load, context);
  ASSERT_FALSE(mismatch.has_value());
  ASSERT_EQ(mismatch.error().size(), 1u);
  EXPECT_EQ(mismatch.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  EXPECT_EQ(mismatch.error().front().range,
            std::get<Ld::ExplicitScalar>(mismatch_load.variant)
                .address.locs.front());

  // A register address does not carry a declaration-derived state space, so
  // the explicit qualifier is retained without inventing an effective space.
  EXPECT_TRUE(checker::check(std::get<Ld>(body[8]), context).has_value());
  EXPECT_TRUE(checker::check(std::get<St>(body[9]), context).has_value());
}

TEST(ResolvedModule, RejectsConstStoreModifier) {
  PtxSyntaxParser parser("st.const.u32 [%rd0], %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message, "Unknown modifier '.const'.");
}

TEST(ResolvedModule, RejectsParamAddressThroughExplicitGlobalLoad) {
  const auto ast = parseModule(R"ptx(
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.global.u32 %r0, [input];
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& load =
      std::get<Ld>(resolved->functions.front().body.front());
  const auto checked = checker::check(
      load, checker::Context{
                .target = {.ptx_version = {1, 0}, .sm_version = 0},
                .instruction_range = ast.range,
            });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

TEST(ResolvedModule, ChecksExplicitParameterAddressSemantics) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel(.param .u32 kernel_input) {
  .reg .u32 %r0;
  ld.param.u32 %r0, [kernel_input];
}
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  ld.param.u32 %r0, [input];
  ld.param.u32 %r0, [%rd0];
  st.param.u32 [result], %r0;
  st.param.u32 [%rd0], %r0;
  ld.param.u32 %r0, [result];
  st.param.u32 [input], %r0;
  ld.param.u32 %r0, [global_value];
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  ASSERT_EQ(resolved->functions[0].body.size(), 1u);
  ASSERT_EQ(resolved->functions[1].body.size(), 7u);

  const checker::Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
      .instruction_range = ast.range,
  };
  const checker::Context supported_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };

  const auto& kernel_load =
      std::get<Ld>(resolved->functions[0].body.front());
  const auto& kernel_explicit =
      std::get<Ld::ExplicitScalar>(kernel_load.variant);
  EXPECT_EQ(kernel_explicit.state_space.value, MemoryStateSpace::Parameter);
  EXPECT_EQ(kernel_explicit.address.value.enclosing_function_kind,
            EnclosingFunctionKind::Entry);
  EXPECT_TRUE(checker::check(kernel_load, old_context).has_value());

  const auto& syntax_device =
      std::get<syntax_ast::AstFunction>(ast.items[2]);
  const auto& syntax_first_load =
      std::get<syntax_ast::AstInstruction>(syntax_device.body[2]);
  const auto& device_load = std::get<Ld>(resolved->functions[1].body[0]);
  const auto& device_explicit =
      std::get<Ld::ExplicitScalar>(device_load.variant);
  EXPECT_EQ(device_explicit.state_space.value, MemoryStateSpace::Parameter);
  ASSERT_EQ(device_explicit.state_space.locs.size(), 1u);
  EXPECT_EQ(device_explicit.state_space.locs.front(),
            syntax_first_load.modifiers.front().syntax.range);
  EXPECT_EQ(device_explicit.address.value.enclosing_function_kind,
            EnclosingFunctionKind::Device);
  const auto& input_symbol =
      std::get<ResolvedSymbolRef>(device_explicit.address.value.base);
  EXPECT_FALSE(input_symbol.address_availability.has_value());

  const auto expect_old_target = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, old_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 2u);
    EXPECT_EQ(checked.error()[0].kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_EQ(checked.error()[1].kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(checker::check(instruction, supported_context).has_value());
  };
  expect_old_target(device_load);
  expect_old_target(std::get<Ld>(resolved->functions[1].body[1]));
  expect_old_target(std::get<St>(resolved->functions[1].body[2]));
  expect_old_target(std::get<St>(resolved->functions[1].body[3]));

  const auto expect_direction_mismatch = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, old_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ParameterDirectionMismatch);
  };
  expect_direction_mismatch(std::get<Ld>(resolved->functions[1].body[4]));
  expect_direction_mismatch(std::get<St>(resolved->functions[1].body[5]));

  const auto wrong_space = checker::check(
      std::get<Ld>(resolved->functions[1].body[6]), old_context);
  ASSERT_FALSE(wrong_space.has_value());
  ASSERT_EQ(wrong_space.error().size(), 1u);
  EXPECT_EQ(wrong_space.error().front().kind,
            checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}

TEST(ResolvedModule, ChecksStandaloneExplicitParameterAvailability) {
  const auto resolve_standalone = [](std::string_view source) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = resolveInstruction(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().message;
    return std::move(*resolved);
  };
  const auto load = resolve_standalone("ld.param.u32 %r0, [%rd0];");
  const auto store = resolve_standalone("st.param.u32 [%rd0], %r0;");
  const auto& resolved_load = std::get<Ld>(load);
  const auto& resolved_store = std::get<St>(store);
  EXPECT_EQ(std::get<Ld::ExplicitScalar>(resolved_load.variant)
                .address.value.enclosing_function_kind,
            EnclosingFunctionKind::Unknown);
  EXPECT_EQ(std::get<St::ExplicitScalar>(resolved_store.variant)
                .address.value.enclosing_function_kind,
            EnclosingFunctionKind::Unknown);

  const checker::Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
  };
  EXPECT_TRUE(checker::check(resolved_load, old_context).has_value());
  const auto old_store = checker::check(resolved_store, old_context);
  ASSERT_FALSE(old_store.has_value());
  ASSERT_EQ(old_store.error().size(), 2u);
  EXPECT_EQ(old_store.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(old_store.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  auto supported_context = old_context;
  supported_context.target = {.ptx_version = {2, 0}, .sm_version = 20};
  EXPECT_TRUE(checker::check(resolved_store, supported_context).has_value());
}

TEST(ResolvedModule, RejectsNarrowStoreSourceRegisterType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %rd0;
  .reg .u16 %r0;
  st.global.u32 [%rd0], %r0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto checked = checker::check(
      std::get<St>(resolved->functions.front().body.front()),
      checker::Context{
          .target = {.ptx_version = {1, 0}, .sm_version = 0},
          .instruction_range = ast.range,
      });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsUnbracketedStoreAddress) {
  PtxSyntaxParser parser("st.u32 %rd0, %r0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = resolveInstruction(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      resolved.error().message,
      "Operands do not match any layout of instruction variant 'GenericScalar'.");
}

TEST(ResolvedModule, ResolvesMovRegisterImmediateAndSymbolOffsetSources) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r<3>;
  .reg .u64 %rd<2>;
  mov.u32 %r0, %r1;
  mov.u32 %r2, 42;
  mov.u64 %rd0, global_value+8;
  mov.u64 %rd1, %clock64;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);

  const auto& register_source = std::get<ResolvedRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[0])).src.value);
  EXPECT_EQ(register_source.spelling, "%r1");
  EXPECT_EQ(register_source.parameterized_index, 1u);
  EXPECT_EQ(register_source.declared_type, ScalarType::U32);

  const auto& immediate_source = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(immediate_source.type, ScalarType::U32);
  EXPECT_EQ(immediate_source.bits, 42u);

  const auto& address_source = std::get<ResolvedAddress>(
      scalarMovOperands(std::get<Mov>(body[2])).src.value);
  const auto& symbol = std::get<ResolvedSymbolRef>(address_source.base);
  ASSERT_TRUE(symbol.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*symbol.symbol_id).name, "global_value");
  ASSERT_TRUE(address_source.offset.has_value());
  EXPECT_EQ(address_source.offset->operation,
            ResolvedAddressOffsetOperator::Add);
  EXPECT_EQ(address_source.offset->value.type, ScalarType::S64);
  EXPECT_EQ(address_source.offset->value.bits, 8u);

  const auto& special_source = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[3])).src.value);
  EXPECT_EQ(special_source.spelling, "%clock64");
  EXPECT_EQ(special_source.id.kind,
            base::SpecialRegisterKind::Clock64);
  EXPECT_EQ(base::metadata(special_source.id).element_type,
            ScalarType::U64);
}

TEST(ResolvedModule, ChecksMovRegisterSourceType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mov.u32 %r0, %rd0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto checked =
      checker::check(mov, checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = checker::PtxVersion{9, 2},
                                      .sm_version = 120,
                                  },
                              .instruction_range = ast.range,
                          });
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMovScalarTypeFamilies) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .b16 %bh0;
  .reg .u16 %uh0;
  .reg .s16 %sh0;
  .reg .b32 %b0;
  .reg .u32 %u0;
  .reg .s32 %s0;
  .reg .f32 %f0;
  .reg .b64 %bd0;
  .reg .f64 %fd0;
  mov.b16 %bh0, %uh0;
  mov.s16 %sh0, %bh0;
  mov.u16 %uh0, 65535;
  mov.b32 %b0, %u0;
  mov.s32 %s0, %b0;
  mov.u32 %u0, %s0;
  mov.f32 %f0, %b0;
  mov.b64 %bd0, %fd0;
  mov.f64 %fd0, %bd0;
  mov.f32 %f0, %u0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 10u);
  const auto& first = std::get<Mov::Scalar>(std::get<Mov>(body[0]).variant);
  EXPECT_EQ(first.type.value, ScalarType::B16);
  const auto& immediate = std::get<ResolvedImmediate>(
      scalarMovOperands(std::get<Mov>(body[2])).src.value);
  EXPECT_EQ(immediate.type, ScalarType::U16);
  EXPECT_EQ(immediate.bits, 65535u);
  const auto& f64 = std::get<Mov>(body[8]);
  EXPECT_EQ(std::get<Mov::Scalar>(f64.variant).type.value, ScalarType::F64);

  const auto check_at = [&](const Mov& mov, uint32_t sm) {
    return checker::check(mov,
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = checker::PtxVersion{9, 2},
                                      .sm_version = sm,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  for (size_t index = 0; index < 9; ++index)
    EXPECT_TRUE(check_at(std::get<Mov>(body[index]), 13).has_value());

  const auto old_target = check_at(f64, 12);
  ASSERT_FALSE(old_target.has_value());
  ASSERT_EQ(old_target.error().size(), 1u);
  EXPECT_EQ(old_target.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  const auto incompatible = check_at(std::get<Mov>(body[9]), 13);
  ASSERT_FALSE(incompatible.has_value());
  ASSERT_EQ(incompatible.error().size(), 1u);
  EXPECT_EQ(incompatible.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ResolvesAndChecksMovRegisterVectorPackAndUnpack) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u8 %b<4>;
  .reg .u16 %h<2>;
  .reg .b32 %r<2>;
  .reg .b64 %rd<2>;
  .reg .b128 %q0;
  mov.b32 %r0, {%h0, %h1};
  mov.b32 {%b0, %b1, %b2, %b3}, %r0;
  mov.b64 {%r0, _}, %rd0;
  mov.b128 %q0, {%rd0, %rd1};
  mov.b128 {%rd0, %rd1}, %q0;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto& pack = packMovOperands(std::get<Mov>(body[0]));
  ASSERT_EQ(pack.src.value.elements.size(), 2u);
  ASSERT_TRUE(pack.src.value.elements[0].has_value());
  EXPECT_EQ(pack.src.value.elements[0]->spelling, "%h0");
  EXPECT_EQ(pack.src.value.elements[0]->declared_type, ScalarType::U16);

  const auto& unpack = unpackMovOperands(std::get<Mov>(body[2]));
  ASSERT_EQ(unpack.dst.value.elements.size(), 2u);
  ASSERT_TRUE(unpack.dst.value.elements[0].has_value());
  EXPECT_FALSE(unpack.dst.value.elements[1].has_value());

  const checker::Context current{
      .target =
          checker::TargetInfo{
              .ptx_version = {8, 3},
              .sm_version = 70,
          },
      .instruction_range = ast.range,
  };
  for (const auto& instruction : body)
    EXPECT_TRUE(
        checker::check(std::get<Mov>(instruction), current).has_value());

  const auto b128_too_old =
      checker::check(std::get<Mov>(body[3]),
                     checker::Context{
                         .target = checker::TargetInfo{.ptx_version = {8, 2},
                                                       .sm_version = 70},
                         .instruction_range = ast.range,
                     });
  ASSERT_FALSE(b128_too_old.has_value());
  EXPECT_EQ(b128_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto b128_sm_too_old =
      checker::check(std::get<Mov>(body[3]),
                     checker::Context{
                         .target = checker::TargetInfo{.ptx_version = {8, 3},
                                                       .sm_version = 69},
                         .instruction_range = ast.range,
                     });
  ASSERT_FALSE(b128_sm_too_old.has_value());
  EXPECT_EQ(b128_sm_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);

  Mov corrupted = std::get<Mov>(body[0]);
  packMovOperands(corrupted).src.value.elements[0]->declared_type =
      ScalarType::U8;
  const auto corrupted_check = checker::check(corrupted, current);
  ASSERT_FALSE(corrupted_check.has_value());
  EXPECT_EQ(corrupted_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsInvalidMovRegisterVectorForms) {
  const auto resolve_source = [](std::string_view instruction) {
    const std::string source = fmt::format(R"ptx(
.entry kernel() {{
  .reg .u8 %b<4>;
  .reg .u16 %h<2>;
  .reg .b32 %r0;
  .reg .b128 %q0;
  {}
}}
)ptx",
                                           instruction);
    return resolveModule(parseModule(source));
  };

  const auto non_bit = resolve_source("mov.u32 %r0, {%h0, %h1};");
  ASSERT_FALSE(non_bit.has_value());
  EXPECT_EQ(non_bit.error().front().message,
            "A vector mov requires a bit-size instruction type.");

  const auto source_sink = resolve_source("mov.b32 %r0, {%h0, _};");
  ASSERT_FALSE(source_sink.has_value());
  EXPECT_EQ(source_sink.error().front().message,
            "The '_' sink is allowed only in a destination vector.");

  const auto bad_arity = resolve_source("mov.b32 %r0, {%b0, %b1, %b2};");
  ASSERT_FALSE(bad_arity.has_value());
  EXPECT_EQ(bad_arity.error().front().message,
            "A vector mov requires two or four elements.");

  const auto all_sinks = resolve_source("mov.b32 {_, _}, %r0;");
  ASSERT_FALSE(all_sinks.has_value());
  EXPECT_EQ(all_sinks.error().front().message,
            "A vector must contain at least one register.");

  const auto scalar_b128 = resolve_source("mov.b128 %q0, %q0;");
  ASSERT_FALSE(scalar_b128.has_value());
  EXPECT_EQ(scalar_b128.error().front().message,
            "The .b128 mov type is available only for vector pack or unpack "
            "forms.");

  const auto sub_byte = resolve_source("mov.b16 %r0, {%b0, %b1, %b2, %b3};");
  ASSERT_FALSE(sub_byte.has_value());
  EXPECT_EQ(sub_byte.error().front().message,
            "Vector mov elements must be at least eight bits wide.");
}

TEST(ResolvedModule, ChecksLegacySpecialRegisterMoveWidths) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u16 %h0;
  .reg .u32 %r0;
  mov.u16 %h0, %tid.x;
  mov.u32 %r0, %tid.x;
  mov.u16 %h0, %gridid;
  mov.u32 %r0, %gridid;
  mov.u16 %h0, %laneid;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto& tid = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[0])).src.value);
  const auto& current_tid = std::get<ResolvedSpecialRegisterRef>(
      scalarMovOperands(std::get<Mov>(body[1])).src.value);
  EXPECT_EQ(tid.id, current_tid.id);
  EXPECT_EQ(tid.component, current_tid.component);
  const auto tid_info = base::metadata(tid.id);
  EXPECT_EQ(tid_info.element_type, ScalarType::U32);
  EXPECT_EQ(tid_info.minimum_ptx_major, 2u);
  EXPECT_EQ(tid_info.minimum_ptx_minor, 0u);

  const auto check_at = [&](size_t index, checker::PtxVersion version) {
    return checker::check(std::get<Mov>(body[index]),
                          checker::Context{
                              .target =
                                  checker::TargetInfo{
                                      .ptx_version = version,
                                      .sm_version = 10,
                                  },
                              .instruction_range = ast.range,
                          });
  };
  EXPECT_TRUE(check_at(0, {1, 0}).has_value());
  EXPECT_TRUE(check_at(1, {2, 0}).has_value());
  EXPECT_TRUE(check_at(2, {1, 0}).has_value());
  EXPECT_TRUE(check_at(3, {1, 3}).has_value());

  const auto current_tid_too_old = check_at(1, {1, 9});
  ASSERT_FALSE(current_tid_too_old.has_value());
  ASSERT_EQ(current_tid_too_old.error().size(), 1u);
  EXPECT_EQ(current_tid_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto gridid_too_old = check_at(3, {1, 2});
  ASSERT_FALSE(gridid_too_old.has_value());
  ASSERT_EQ(gridid_too_old.error().size(), 1u);
  EXPECT_EQ(gridid_too_old.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);

  const auto laneid_narrow = check_at(4, {9, 2});
  ASSERT_FALSE(laneid_narrow.has_value());
  ASSERT_EQ(laneid_narrow.error().size(), 1u);
  EXPECT_EQ(laneid_narrow.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, RejectsSixteenBitMovAddress) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u16 %h0;
  mov.u16 %h0, global_value;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "A data address requires a 32-bit or 64-bit integer or bit-size "
            "mov type.");
}

TEST(ResolvedModule, ResolvesAndChecksPredicateMove) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p<2>;
  mov.pred %p0, %p1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto& mov = std::get<Mov>(resolved->functions.front().body.front());
  const auto& predicate = std::get<Mov::Pred>(mov.variant);
  const auto& source = std::get<ResolvedPredicate>(predicate.src.value);
  EXPECT_FALSE(predicate.dst.value.negated);
  EXPECT_FALSE(source.negated);
  ASSERT_TRUE(predicate.dst.value.register_ref.symbol_id.has_value());
  ASSERT_TRUE(source.register_ref.symbol_id.has_value());
  EXPECT_EQ(
      resolved->symbols.symbol(*predicate.dst.value.register_ref.symbol_id)
          .name,
      "%p");
  EXPECT_EQ(predicate.dst.value.register_ref.parameterized_index, 0u);
  EXPECT_EQ(source.register_ref.parameterized_index, 1u);

  EXPECT_TRUE(
      checker::check(mov,
                     checker::Context{
                         .target =
                             checker::TargetInfo{
                                 .ptx_version = checker::PtxVersion{1, 0},
                                 .sm_version = 0,
                             },
                         .instruction_range = ast.range,
                     })
          .has_value());

  PtxSyntaxParser parser("mov.pred %p0, %p1;");
  const auto standalone_ast = parser.parseInstruction();
  ASSERT_TRUE(standalone_ast.has_value())
      << standalone_ast.diagnostics.front().message;
  const auto standalone = resolveInstruction(*standalone_ast);
  ASSERT_TRUE(standalone.has_value()) << standalone.error().message;
  const auto& standalone_predicate =
      std::get<Mov::Pred>(std::get<Mov>(*standalone).variant);
  const auto& standalone_source =
      std::get<ResolvedPredicate>(standalone_predicate.src.value);
  EXPECT_FALSE(
      standalone_predicate.dst.value.register_ref.symbol_id.has_value());
  EXPECT_FALSE(standalone_source.register_ref.symbol_id.has_value());
}

TEST(ResolvedModule, ResolvesU32MovSymbolAddressSource) {
  const auto ast = parseModule(R"ptx(
.global .u32 global_value;
.entry kernel() {
  .reg .u32 %r0;
  mov.u32 %r0, global_value;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& source = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions.front().body.front()))
          .src.value);
  EXPECT_EQ(source.spelling, "global_value");
  EXPECT_EQ(source.declaration_state_space, syntax_ast::AstStateSpace::Global);
  EXPECT_EQ(source.address_state_space, syntax_ast::AstStateSpace::Global);
}

TEST(ResolvedModule, ResolvesKernelAndDeviceFunctionParameterAddresses) {
  const auto ast = parseModule(R"ptx(
.entry kernel(.param .u32 kernel_input) {
  .reg .u32 %r0;
  mov.u32 %r0, kernel_input+4;
}
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u64 %rd<2>;
  mov.u64 %rd0, input;
  mov.u64 %rd1, result;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);

  const auto& kernel_address = std::get<ResolvedAddress>(
      scalarMovOperands(std::get<Mov>(resolved->functions[0].body.front()))
          .src.value);
  const auto& kernel_parameter =
      std::get<ResolvedSymbolRef>(kernel_address.base);
  EXPECT_EQ(kernel_parameter.declaration_kind,
            binding::SymbolKind::InputParameter);
  EXPECT_EQ(kernel_parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(kernel_parameter.address_state_space,
            syntax_ast::AstStateSpace::Parameter);
  ASSERT_TRUE(kernel_address.offset.has_value());
  EXPECT_EQ(kernel_address.offset->value.bits, 4u);

  const auto& device_input = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions[1].body[0]))
          .src.value);
  EXPECT_EQ(device_input.declaration_kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(device_input.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(device_input.address_state_space, syntax_ast::AstStateSpace::Local);
  ASSERT_TRUE(device_input.address_availability.has_value());
  EXPECT_EQ(device_input.address_availability->minimum_ptx_version,
            (checker::PtxVersion{2, 0}));
  EXPECT_EQ(device_input.address_availability->minimum_sm_version, 20u);

  const auto& return_parameter = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(resolved->functions[1].body[1]))
          .src.value);
  EXPECT_EQ(return_parameter.declaration_kind,
            binding::SymbolKind::ReturnParameter);
  EXPECT_EQ(return_parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(return_parameter.address_state_space,
            syntax_ast::AstStateSpace::Local);
  ASSERT_TRUE(return_parameter.address_availability.has_value());
  EXPECT_EQ(return_parameter.address_availability->minimum_ptx_version,
            (checker::PtxVersion{6, 0}));
  EXPECT_EQ(return_parameter.address_availability->minimum_sm_version, 20u);
}

TEST(ResolvedModule, ChecksDeviceParameterAddressAvailability) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) device(.param .u32 input) {
  .reg .u64 %rd<2>;
  mov.u64 %rd0, input;
  mov.u64 %rd1, result+4;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& input_mov = std::get<Mov>(resolved->functions.front().body[0]);
  const auto& return_mov = std::get<Mov>(resolved->functions.front().body[1]);

  const auto input_rejected =
      checker::check(input_mov, checker::Context{
                                    .target =
                                        checker::TargetInfo{
                                            .ptx_version = {1, 5},
                                            .sm_version = 10,
                                        },
                                    .instruction_range = ast.range,
                                });
  ASSERT_FALSE(input_rejected.has_value());
  ASSERT_EQ(input_rejected.error().size(), 2u);
  EXPECT_EQ(input_rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(input_rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(input_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {2, 0},
                                         .sm_version = 20,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());

  const auto return_rejected =
      checker::check(return_mov, checker::Context{
                                     .target =
                                         checker::TargetInfo{
                                             .ptx_version = {5, 0},
                                             .sm_version = 10,
                                         },
                                     .instruction_range = ast.range,
                                 });
  ASSERT_FALSE(return_rejected.has_value());
  ASSERT_EQ(return_rejected.error().size(), 2u);
  EXPECT_EQ(return_rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(return_rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(return_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {6, 0},
                                         .sm_version = 20,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());
}

TEST(ResolvedModule, ResolvesAndChecksFunctionAddresses) {
  const auto ast = parseModule(R"ptx(
.func device() {}
.entry launched() {}
.entry caller() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  mov.u32 %r0, device;
  mov.u64 %rd0, launched;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 3u);
  const auto& device_mov = std::get<Mov>(resolved->functions[2].body[0]);
  const auto& kernel_mov = std::get<Mov>(resolved->functions[2].body[1]);

  const auto& device =
      std::get<ResolvedFunctionRef>(scalarMovOperands(device_mov).src.value);
  ASSERT_TRUE(device.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*device.symbol_id).name, "device");
  EXPECT_FALSE(device.is_entry);
  EXPECT_FALSE(device.address_availability.has_value());
  EXPECT_TRUE(checker::check(device_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {1, 0},
                                         .sm_version = 0,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());

  const auto& kernel =
      std::get<ResolvedFunctionRef>(scalarMovOperands(kernel_mov).src.value);
  ASSERT_TRUE(kernel.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*kernel.symbol_id).name, "launched");
  EXPECT_TRUE(kernel.is_entry);
  ASSERT_TRUE(kernel.address_availability.has_value());
  EXPECT_EQ(kernel.address_availability->minimum_ptx_version,
            (checker::PtxVersion{3, 1}));
  EXPECT_EQ(kernel.address_availability->minimum_sm_version, 35u);

  const auto rejected =
      checker::check(kernel_mov, checker::Context{
                                     .target =
                                         checker::TargetInfo{
                                             .ptx_version = {3, 0},
                                             .sm_version = 30,
                                         },
                                     .instruction_range = ast.range,
                                 });
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(checker::check(kernel_mov,
                             checker::Context{
                                 .target =
                                     checker::TargetInfo{
                                         .ptx_version = {3, 1},
                                         .sm_version = 35,
                                     },
                                 .instruction_range = ast.range,
                             })
                  .has_value());
}

TEST(ResolvedModule, PreservesDirectDeviceParameterAddressSpace) {
  const auto ast = parseModule(R"ptx(
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.u32 %r0, [input];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& address =
      std::get<Ld::GenericScalar>(
          std::get<Ld>(resolved->functions.front().body.front()).variant)
          .address.value;
  const auto& parameter = std::get<ResolvedSymbolRef>(address.base);
  EXPECT_EQ(parameter.declaration_kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(parameter.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(parameter.address_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_FALSE(parameter.address_availability.has_value());
}

TEST(ResolvedModule, RejectsLocallyScopedParamVariableAddress) {
  const auto ast = parseModule(R"ptx(
.func device() {
  .param .align 8 .b8 call_argument[8];
  .reg .u64 %rd0;
  mov.u64 %rd0, call_argument;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'call_argument' is not an addressable data symbol.");
}

TEST(ResolvedModule, ResolvesAndChecksLocalCallParameterAddresses) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) callee(.param .u32 input);
.entry caller() {
  .reg .u32 %r0, %r1;
  .param .u32 input_staging, return_staging;
  st.param.u32 [input_staging], %r0;
  call (return_staging), callee, (input_staging);
  ld.param.u32 %r1, [return_staging];
}
)ptx");
  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions[1].body;
  ASSERT_EQ(body.size(), 3u);
  const auto& store = std::get<St>(body[0]);
  const auto& load = std::get<Ld>(body[2]);
  const auto& address =
      std::get<St::ExplicitScalar>(store.variant).address.value;
  const auto& staging = std::get<ResolvedSymbolRef>(address.base);
  EXPECT_EQ(staging.declaration_kind, binding::SymbolKind::CallParameter);
  EXPECT_EQ(staging.declaration_state_space,
            syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(staging.address_state_space, syntax_ast::AstStateSpace::Parameter);
  const auto& call = std::get<Call>(body[1]);
  const auto& call_operands =
      std::get<Call::Direct::ReturnTargetInputOperands>(
          std::get<Call::Direct>(call.variant).operands);
  const auto caller_scope =
      *resolved->symbols.symbol(resolved->functions[1].symbol_id).owned_scope;
  const auto return_staging =
      resolved->symbols.lookup(caller_scope, "return_staging");
  ASSERT_TRUE(return_staging.has_value());
  EXPECT_EQ(call_operands.return_value.value.symbol_id,
            return_staging->symbol);

  const checker::Context old_context{
      .target = {.ptx_version = {1, 5}, .sm_version = 10},
      .instruction_range = ast.range,
  };
  const checker::Context supported_context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20},
      .instruction_range = ast.range,
  };
  const auto expect_availability = [&](const auto& instruction) {
    const auto checked = checker::check(instruction, old_context);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 2u);
    EXPECT_EQ(checked.error()[0].kind,
              checker::CheckDiagnosticKind::UnsupportedPtxVersion);
    EXPECT_EQ(checked.error()[1].kind,
              checker::CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(checker::check(instruction, supported_context).has_value());
  };
  expect_availability(store);
  expect_availability(load);
}

TEST(ResolvedModule, IgnoresLocDirectivesForCallParameterStaging) {
  const auto ast = parseModule(R"ptx(
.file 1 "caller.ptx"
.func (.param .u32 result) callee(.param .u32 input);
.entry caller() {
  .reg .u32 %r0, %r1;
  .param .u32 input_staging, return_staging;
  st.param.u32 [input_staging], %r0;
  .loc 1 5 1
  call (return_staging), callee, (input_staging);
  .loc 1 6 1
  ld.param.u32 %r1, [return_staging];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions[1].body;
  ASSERT_EQ(body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<St>(body[0]));
  EXPECT_TRUE(std::holds_alternative<Call>(body[1]));
  EXPECT_TRUE(std::holds_alternative<Ld>(body[2]));
}

TEST(ResolvedModule, IgnoresPragmasForCallParameterStaging) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) callee(.param .u32 input);
.entry caller() {
  .reg .u32 %r0, %r1;
  .param .u32 input_staging, return_staging;
  st.param.u32 [input_staging], %r0;
  .pragma "input";
  call (return_staging), callee, (input_staging);
  .pragma "return";
  ld.param.u32 %r1, [return_staging];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions[1].body;
  ASSERT_EQ(body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<St>(body[0]));
  EXPECT_TRUE(std::holds_alternative<Call>(body[1]));
  EXPECT_TRUE(std::holds_alternative<Ld>(body[2]));
}

TEST(ResolvedModule, KeepsStandaloneAddressAndSymbolIdentityOpen) {
  PtxSyntaxParser mov_parser("mov.u64 %rd0, global_value;");
  const auto mov_ast = mov_parser.parseInstruction();
  ASSERT_TRUE(mov_ast.has_value()) << mov_ast.diagnostics.front().message;
  const auto mov_resolved = resolveInstruction(*mov_ast);
  ASSERT_TRUE(mov_resolved.has_value()) << mov_resolved.error().message;
  const auto& symbol = std::get<ResolvedSymbolRef>(
      scalarMovOperands(std::get<Mov>(*mov_resolved)).src.value);
  EXPECT_EQ(symbol.spelling, "global_value");
  EXPECT_FALSE(symbol.symbol_id.has_value());
  EXPECT_FALSE(symbol.declaration_kind.has_value());
  EXPECT_FALSE(symbol.declaration_state_space.has_value());
  EXPECT_FALSE(symbol.address_state_space.has_value());

  PtxSyntaxParser load_parser("ld.u32 %r0, [%rd0+4];");
  const auto load_ast = load_parser.parseInstruction();
  ASSERT_TRUE(load_ast.has_value()) << load_ast.diagnostics.front().message;
  const auto load_resolved = resolveInstruction(*load_ast);
  ASSERT_TRUE(load_resolved.has_value()) << load_resolved.error().message;
  const auto& address =
      std::get<Ld::GenericScalar>(std::get<Ld>(*load_resolved).variant)
          .address.value;
  const auto& base = std::get<ResolvedRegisterRef>(address.base);
  EXPECT_EQ(base.spelling, "%rd0");
  EXPECT_FALSE(base.symbol_id.has_value());
}

TEST(ResolvedModule, RejectsUnbracketedLoadAddress) {
  PtxSyntaxParser parser("ld.u32 %r0, %rd0+4;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto invalid_address = resolveInstruction(*ast);
  ASSERT_FALSE(invalid_address.has_value());
  EXPECT_EQ(invalid_address.error().message,
            "Expected a bracketed address operand.");
}

TEST(ResolvedModule, RejectsNonRegisterSymbolsInRegisterOperands) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  .local .u32 value;
  add.u32 %dst, value, %dst;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'value' is not a .reg variable.");
}

TEST(ResolvedModule, CheckerUsesDeclarationBoundRegisterTypes) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %wide;
  .reg .u32 %r<2>;
  add.u32 %wide, %r0, %r1;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const ResolvedFunction& function = resolved->functions.front();
  const Add& add = std::get<Add>(function.body.front());
  const auto& syntax_instruction =
      std::get<syntax_ast::AstFunction>(ast.items.front()).body.back();

  const auto result = checker::check(
      add,
      checker::Context{
          .target =
              checker::TargetInfo{
                  .ptx_version = checker::PtxVersion{9, 0},
                  .sm_version = 100,
              },
          .instruction_range =
              std::get<syntax_ast::AstInstruction>(syntax_instruction).range,
      });

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(result.error().front().message,
            "Register operand 'dst' has declared type 'U64' but instruction "
            "type source 'type' is 'U32'.");
}

TEST(ResolvedModule, BindsPredicateOperandsByDeclarationType) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  bar.red.and.pred %condition, 0, !%condition;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& bar = std::get<Bar>(resolved->functions.front().body.front());
  const auto& reduction = std::get<Bar::RedAndPred>(bar.variant);
  const auto& operands =
      std::get<Bar::RedAndPred::WithoutThreadCountOperands>(reduction.operands);
  ASSERT_TRUE(operands.dst.value.register_ref.symbol_id.has_value());
  EXPECT_EQ(operands.dst.value.register_ref.symbol_id,
            operands.predicate.value.register_ref.symbol_id);
  EXPECT_EQ(operands.dst.value.register_ref.declared_type, ScalarType::Pred);
  EXPECT_TRUE(operands.predicate.value.negated);
}

TEST(ResolvedModule, PreservesAndBindsExecutionPredicate) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  .reg .u32 %r<3>;
  @!%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& add = std::get<Add>(resolved->functions.front().body.front());
  ASSERT_TRUE(add.execution_predicate.has_value());
  const ResolvedPredicate& predicate = add.execution_predicate->value;
  EXPECT_TRUE(predicate.negated);
  EXPECT_EQ(predicate.register_ref.spelling, "%condition");
  EXPECT_EQ(predicate.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(predicate.register_ref.declared_type, ScalarType::Pred);
  ASSERT_TRUE(predicate.register_ref.symbol_id.has_value());
  EXPECT_EQ(resolved->symbols.symbol(*predicate.register_ref.symbol_id).name,
            "%condition");

  const auto& function = std::get<syntax_ast::AstFunction>(ast.items.front());
  const auto& instruction =
      std::get<syntax_ast::AstInstruction>(function.body.back());
  ASSERT_TRUE(instruction.predicate.has_value());
  ASSERT_EQ(add.execution_predicate->locs.size(), 1u);
  EXPECT_EQ(add.execution_predicate->locs.front(),
            instruction.predicate->range);
}

TEST(ResolvedModule, RejectsNonPredicateExecutionGuard) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %condition;
  .reg .u32 %r<3>;
  @%condition add.u32 %r0, %r1, %r2;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Expected a predicate register, but '%condition' is declared "
            "'.u32'.");
}

TEST(ResolvedModule, ResolvesPredicatedDirectBranchTarget) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %condition;
  @!%condition bra.uni done;
done:
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 1u);
  const auto& bra = std::get<Bra>(resolved->functions.front().body.front());
  ASSERT_TRUE(bra.execution_predicate.has_value());
  EXPECT_TRUE(bra.execution_predicate->value.negated);

  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_TRUE(direct.uni.value);
  EXPECT_EQ(direct.target.value.spelling, "done");
  ASSERT_TRUE(direct.target.value.symbol_id.has_value());
  const binding::Symbol& label =
      resolved->symbols.symbol(*direct.target.value.symbol_id);
  EXPECT_EQ(label.kind, binding::SymbolKind::Label);
  EXPECT_EQ(label.name, "done");
  const binding::Symbol& function_symbol =
      resolved->symbols.symbol(resolved->functions.front().symbol_id);
  ASSERT_TRUE(function_symbol.owned_scope.has_value());
  EXPECT_EQ(label.scope, *function_symbol.owned_scope);
  ASSERT_EQ(direct.target.locs.size(), 1u);

  const checker::Context check_context{
      .target =
          checker::TargetInfo{
              .ptx_version = checker::PtxVersion{1, 0},
              .sm_version = 0,
          },
      .instruction_range = direct.target.locs.front(),
  };
  const auto checked = checker::check(bra, check_context);
  EXPECT_TRUE(checked.has_value());
}

TEST(ResolvedModule, StandaloneBranchTargetRemainsUnbound) {
  PtxSyntaxParser parser("bra target;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& bra = std::get<Bra>(*resolved);
  EXPECT_FALSE(bra.execution_predicate.has_value());
  const auto& direct = std::get<Bra::Direct>(bra.variant);
  EXPECT_FALSE(direct.uni.value);
  EXPECT_TRUE(direct.uni.locs.empty());
  EXPECT_EQ(direct.target.value.spelling, "target");
  EXPECT_FALSE(direct.target.value.symbol_id.has_value());
}

TEST(ResolvedModule, ResolvesIndexedBranchTargetSetInCurrentFunction) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %index;
targets: .branchtargets done;
  brx.idx.uni %index, targets;
done:
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 1u);
  const auto& brx = std::get<Brx>(resolved->functions.front().body.front());
  const auto& indexed = std::get<Brx::Idx>(brx.variant);
  EXPECT_TRUE(indexed.uni.value);
  EXPECT_EQ(indexed.index.value.declared_type, ScalarType::U32);
  EXPECT_EQ(indexed.tlist.value.spelling, "targets");
  ASSERT_TRUE(indexed.tlist.value.symbol_id.has_value());
  const auto& target_set =
      resolved->symbols.symbol(*indexed.tlist.value.symbol_id);
  EXPECT_EQ(target_set.kind, binding::SymbolKind::BranchTargetSet);

  const checker::Context too_old{
      .target = {.ptx_version = checker::PtxVersion{5, 9}, .sm_version = 30},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_FALSE(checker::check(brx, too_old).has_value());
  const checker::Context too_old_sm{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 29},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_FALSE(checker::check(brx, too_old_sm).has_value());
  const checker::Context supported{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 30},
      .instruction_range = indexed.tlist.locs.front(),
  };
  EXPECT_TRUE(checker::check(brx, supported).has_value());
}

TEST(ResolvedModule, IndexedBranchRequiresU32IndexRegister) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %index;
targets: .branchtargets done;
  brx.idx %index, targets;
done:
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& brx = std::get<Brx>(resolved->functions.front().body.front());
  const checker::Context context{
      .target = {.ptx_version = checker::PtxVersion{6, 0}, .sm_version = 30},
      .instruction_range = SourceRange{},
  };
  EXPECT_FALSE(checker::check(brx, context).has_value());
}

TEST(ResolvedModule, ResolvesDirectCallGroupsAndPreservesBindings) {
  const auto ast = parseModule(R"ptx(
.func callee();
.func callee_empty();
.func (.param .u32 result) callee_full(
    .param .u32 input0, .param .u32 input1, .param .s32 literal);
.entry caller() {
  .reg .pred %p;
  .reg .u32 %out, %arg;
  .param .u32 parameter;
  call callee;
  call callee_empty, ();
  @%p call.uni (%out), callee_full, (%arg, parameter, -4);
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.back().body;
  ASSERT_EQ(body.size(), 3u);

  const auto& target_only = std::get<Call>(body[0]);
  const auto& target_payload =
      std::get<Call::Direct::TargetOperands>(std::get<Call::Direct>(target_only.variant).operands);
  EXPECT_EQ(target_payload.target.value.spelling, "callee");
  ASSERT_TRUE(target_payload.target.value.symbol_id.has_value());

  const auto& empty_inputs = std::get<Call>(body[1]);
  const auto& input_payload = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(empty_inputs.variant).operands);
  EXPECT_TRUE(input_payload.arguments.value.values.empty());
  ASSERT_EQ(input_payload.arguments.locs.size(), 1u);

  Call call = std::get<Call>(body[2]);
  auto& direct = std::get<Call::Direct>(call.variant);
  ASSERT_TRUE(call.execution_predicate.has_value());
  EXPECT_TRUE(direct.uni.value);
  const auto& return_payload = std::get<Call::Direct::ReturnTargetInputOperands>(direct.operands);
  EXPECT_EQ(return_payload.return_value.value.spelling, "%out");
  ASSERT_TRUE(return_payload.return_value.value.symbol_id.has_value());
  ASSERT_EQ(return_payload.arguments.value.values.size(), 3u);
  const auto& parameter = std::get<ResolvedCallParameterRef>(
      return_payload.arguments.value.values[1].value);
  EXPECT_EQ(parameter.state_space, syntax_ast::AstStateSpace::Parameter);
  const auto& literal = std::get<ResolvedCallLiteral>(
      return_payload.arguments.value.values[2].value);
  EXPECT_EQ(literal.spelling, "-4");
  ASSERT_EQ(return_payload.arguments.value.values[2].locs.size(), 1u);

  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = return_payload.target.locs.front(),
  };
  EXPECT_TRUE(checker::check(call, context).has_value());

  direct.operand_layout = ResolvedOperandLayoutTag{0};
  const auto payload_mismatch = checker::check(call, context);
  ASSERT_FALSE(payload_mismatch.has_value());
  EXPECT_EQ(payload_mismatch.error().back().kind,
            checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch);

  direct.operand_layout = ResolvedOperandLayoutTag{99};
  const auto invalid_tag = checker::check(call, context);
  ASSERT_FALSE(invalid_tag.has_value());
  EXPECT_EQ(invalid_tag.error().back().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
}

TEST(ResolvedModule, ChecksDirectCallAbiForMixedParametersAndLiterals) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func (.param .u32 result) callee(
    .reg .u32 register_input, .param .u32 parameter_input,
    .param .s16 literal_input, .reg .u32 parameterized_register_input);
.entry caller() {
  .reg .u32 register_argument;
  .reg .u32 %r<1>;
  .param .u32 parameter_argument, return_argument;
  call (return_argument), callee,
      (register_argument, parameter_argument, -4, %r0);
}
)ptx"));

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(ResolvedModule, ReportsDirectCallAbiArityMismatches) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func (.param .u32 return0, .param .u32 return1) callee(
    .param .u32 input0, .param .u32 input1);
.entry caller() {
  .param .u32 return0, input0;
  call (return0), callee, (input0);
}
)ptx"));

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 2u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call to 'callee' has 1 return argument but callee requires "
            "2.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call to 'callee' has 1 input argument but callee requires "
            "2.");
}

TEST(ResolvedModule, ReportsDirectCallAbiPropertyAndLiteralMismatches) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func scalar(.param .u32 input);
.func bytes(.param .align 16 .b8 input[8]);
.func pointer(.param .u64 .ptr .global .align 16 input);
.func literal(.param .u16 input);
.entry caller(.param .u64 .ptr .shared .align 32 wrong_space,
              .param .u64 .ptr .global .align 8 weak_alignment) {
  .reg .v2 .u32 vector_argument;
  .reg .b8 register_byte;
  .param .align 16 .b8 wrong_size[4];
  .param .align 8 .b8 weak_array_alignment[8];
  call scalar, (vector_argument);
  call bytes, (register_byte);
  call bytes, (wrong_size);
  call bytes, (weak_array_alignment);
  call pointer, (wrong_space);
  call pointer, (weak_alignment);
  call literal, (1.5);
  call literal, (65536);
  call bytes, (1);
  call pointer, (1);
}
)ptx"));

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 10u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call input argument 1 for 'scalar' has type or vector "
            "shape mismatch.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call input argument 1 for 'bytes' has call argument "
            "state-space mismatch.");
  EXPECT_EQ(
      resolved.error()[2].message,
      "Direct call input argument 1 for 'bytes' has array size mismatch.");
  EXPECT_EQ(resolved.error()[3].message,
            "Direct call input argument 1 for 'bytes' has array alignment "
            "mismatch.");
  EXPECT_EQ(resolved.error()[4].message,
            "Direct call input argument 1 for 'pointer' has pointed "
            "state-space mismatch.");
  EXPECT_EQ(resolved.error()[5].message,
            "Direct call input argument 1 for 'pointer' has pointed alignment "
            "mismatch.");
  EXPECT_EQ(resolved.error()[6].message,
            "Decimal floating literal '1.5' is incompatible with scalar type "
            "'U16'.");
  EXPECT_EQ(resolved.error()[7].message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");
  EXPECT_EQ(resolved.error()[8].message,
            "Direct call input argument 1 for 'bytes' has call argument "
            "state-space mismatch.");
  EXPECT_EQ(resolved.error()[9].message,
            "Direct call input argument 1 for 'pointer' has pointer "
            "qualification mismatch.");
}

TEST(ResolvedModule, AcceptsDirectCallsAcrossFunctionLifecycles) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func prototype_only(.reg .u32 input);
.extern .func external(.reg .u32 input);
.func defined_before(.reg .u32 input) { }
.func bytes(.param .align 8 .b8 input[]);
.func generic_pointer(.param .u64 .ptr .align 8 input);
.func global_pointer(.param .u64 .ptr .global .align 16 input);
.func immediate(.reg .u8 high, .reg .s8 low, .reg .f32 float);
.entry caller(.param .u64 .ptr .global .align 16 global_actual) {
  .reg .u32 %r;
  .param .align 8 .b8 blob[8];
  call prototype_only, (%r);
  call external, (%r);
  call defined_before, (%r);
  call defined_after, (%r);
  call renamed, (%r);
  call bytes, (blob);
  call generic_pointer, (global_actual);
  call global_pointer, (global_actual);
  call immediate, (255, -128, 0f3f800000);
}
.func defined_after(.reg .u32 input) { }
.func renamed(.reg .u32 declaration_name);
.func renamed(.reg .u32 definition_name) {
  .reg .u32 %self;
  call renamed, (%self);
}
)ptx"));

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(ResolvedModule, ReportsDirectCallArityAndElementRanges) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) one_return();
.func no_returns();
.func needs_input(.reg .u32 input);
.func no_inputs();
.func signed(.reg .s8 input);
.entry caller() {
  .reg .u32 narrow;
  .reg .u64 wide;
  .param .u32 return_value;
  call one_return;
  call (return_value), no_returns, ();
  call needs_input;
  call no_inputs, (narrow);
  call needs_input, (wide);
  call signed, (128);
}
)ptx");
  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 6u);
  EXPECT_EQ(resolved.error()[0].message,
            "Direct call to 'one_return' has 0 return arguments but callee "
            "requires 1.");
  EXPECT_EQ(resolved.error()[1].message,
            "Direct call to 'no_returns' has 1 return argument but callee "
            "requires 0.");
  EXPECT_EQ(resolved.error()[2].message,
            "Direct call to 'needs_input' has 0 input arguments but callee "
            "requires 1.");
  EXPECT_EQ(resolved.error()[3].message,
            "Direct call to 'no_inputs' has 1 input argument but callee "
            "requires 0.");
  EXPECT_EQ(resolved.error()[4].message,
            "Direct call input argument 1 for 'needs_input' has type or "
            "vector shape mismatch.");
  EXPECT_EQ(resolved.error()[5].message,
            "Integer literal '128' is out of range for scalar type 'S8'.");

  const auto& caller = std::get<syntax_ast::AstFunction>(ast.items[5]);
  const auto& extra_inputs = std::get<syntax_ast::AstInstruction>(caller.body[6]);
  const auto& type_mismatch =
      std::get<syntax_ast::AstInstruction>(caller.body[7]);
  const auto& extra_group =
      std::get<syntax_ast::AstCallParameterList>(extra_inputs.operands[1]);
  const auto& wide_group =
      std::get<syntax_ast::AstCallParameterList>(type_mismatch.operands[1]);
  EXPECT_EQ(resolved.error()[3].range, extra_group.range);
  EXPECT_EQ(resolved.error()[4].range,
            std::get<syntax_ast::AstIdentifierRef>(wide_group.parameters[0])
                .syntax.range);
}

TEST(ResolvedModule, RejectsLocalAndGlobalDirectCallActuals) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.global .u32 global_value;
.func needs_input(.reg .u32 input);
.entry caller() {
  .local .u32 local_value;
  call needs_input, (local_value);
  call needs_input, (global_value);
}
)ptx"));

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 2u);
  EXPECT_EQ(resolved.error()[0].message,
            "Call parameter 'local_value' must name a .reg or .param "
            "variable.");
  EXPECT_EQ(resolved.error()[1].message,
            "Call parameter 'global_value' must name a .reg or .param "
            "variable.");
}

TEST(ResolvedModule, EnforcesPtx93CallParameterContexts) {
  const auto ast = parseModule(R"ptx(
.func (.param .u32 result) callee(.param .u32 input0, .param .u32 input1);
.entry caller(.param .u32 entry_input) {
  .reg .pred %p;
  .reg .u32 %r0;
  .param .u32 input0, input1, output;
  ld.param::entry.u32 %r0, [entry_input];
  ld.param.u32 %r0, [entry_input];
  st.param::func.u32 [input0], %r0;
  st.param.u32 [input1], %r0;
  @%p call.uni (output), callee, (input0, input1);
  ld.param::func.u32 %r0, [output];
}
.func device(.param .u32 input) {
  .reg .u32 %r0;
  ld.param::func.u32 %r0, [input];
  ld.param.u32 %r0, [input];
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& caller = resolved->functions[1].body;
  ASSERT_EQ(caller.size(), 6u);
  const auto& entry_load = std::get<Ld::ExplicitScalar>(
      std::get<Ld>(caller[0]).variant);
  EXPECT_EQ(entry_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Entry);
  const auto& staged_store = std::get<St::ExplicitScalar>(
      std::get<St>(caller[2]).variant);
  EXPECT_EQ(staged_store.address.value.parameter_qualifier,
            ParameterAddressQualifier::Function);
  const auto& default_load = std::get<Ld::ExplicitScalar>(
      std::get<Ld>(caller[1]).variant);
  EXPECT_EQ(default_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Default);
  const auto& call = std::get<Call>(caller[4]);
  EXPECT_TRUE(call.execution_predicate.has_value());
  EXPECT_TRUE(std::get<Call::Direct>(call.variant).uni.value);
  const auto& return_load = std::get<Ld::ExplicitScalar>(
      std::get<Ld>(caller[5]).variant);
  EXPECT_EQ(return_load.address.value.parameter_qualifier,
            ParameterAddressQualifier::Function);
}

TEST(ResolvedModule, RejectsInvalidPtx93CallParameterContexts) {
  const auto resolve_source = [](std::string_view body) {
    return resolveModule(parseModule(fmt::format(R"ptx(
.func (.param .u32 result) callee(.param .u32 input);
.entry caller(.param .u32 entry_input) {{
  .reg .pred %p;
  .reg .u32 %r0;
  .param .u32 input, other, output;
  {}
}}
.func device(.param .u32 input) {{
  .reg .u32 %r0;
  ld.param::entry.u32 %r0, [input];
}}
)ptx",
                                                body)));
  };

  const auto entry_as_function = resolve_source(
      "ld.param::func.u32 %r0, [entry_input];");
  ASSERT_FALSE(entry_as_function.has_value());
  EXPECT_EQ(entry_as_function.error().front().message,
            ".param::func may access only a device-function parameter or "
            "function-local call parameter.");

  const auto device_as_entry = resolve_source("mov.u32 %r0, %r0;");
  ASSERT_FALSE(device_as_entry.has_value());
  EXPECT_EQ(device_as_entry.error().front().message,
            ".param::entry may access only a kernel entry input parameter.");

  const auto predicated_store = resolve_source(
      "@%p st.param.u32 [input], %r0;\n  call (output), callee, (input);");
  ASSERT_FALSE(predicated_store.has_value());
  EXPECT_EQ(predicated_store.error().front().message,
            "A function-local .param argument store cannot be predicated.");

  const auto non_adjacent_store = resolve_source(
      "st.param.u32 [input], %r0;\n  mov.u32 %r0, %r0;\n  call (output), callee, (input);");
  ASSERT_FALSE(non_adjacent_store.has_value());
  EXPECT_EQ(non_adjacent_store.error().front().message,
            "A function-local .param argument store must be in the contiguous "
            "block immediately before a call that uses it.");

  const auto wrong_call_argument = resolve_source(
      "st.param.u32 [input], %r0;\n  call (output), callee, (other);");
  ASSERT_FALSE(wrong_call_argument.has_value());
  EXPECT_EQ(wrong_call_argument.error().front().message,
            "A function-local .param argument store must be in the contiguous "
            "block immediately before a call that uses it.");

  const auto predicated_return_load = resolve_source(
      "call (output), callee, (input);\n  @%p ld.param.u32 %r0, [output];");
  ASSERT_FALSE(predicated_return_load.has_value());
  EXPECT_EQ(predicated_return_load.error().front().message,
            "A function-local .param return load cannot be predicated.");

  const auto non_adjacent_return_load = resolve_source(
      "call (output), callee, (input);\n  mov.u32 %r0, %r0;\n  ld.param.u32 %r0, [output];");
  ASSERT_FALSE(non_adjacent_return_load.has_value());
  EXPECT_EQ(non_adjacent_return_load.error().front().message,
            "A function-local .param return load must be in the contiguous "
            "block immediately after a call that returns it.");

  const auto entry_store = resolveModule(parseModule(R"ptx(
.entry caller() {
  .reg .u32 %r0;
  .param .u32 output;
  st.param::entry.u32 [output], %r0;
}
)ptx"));
  EXPECT_FALSE(entry_store.has_value());
}

TEST(ResolvedModule, ResolvesIndirectCallsWithFunctionLocalMetadata) {
  const auto& syntax = Call::get_syntax_descriptor();
  const auto& resolved_descriptor = Call::get_resolved_descriptor();
  const auto& checker_descriptor = Call::get_checker_descriptor();
  ASSERT_EQ(syntax.variants[0].operand_layouts.size(), 6u);
  EXPECT_EQ(syntax.variants[0].operand_layouts[0].kind,
            check_end::OperandLayoutKind::Call);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].kind,
            check_end::OperandLayoutKind::IndirectCall);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].slots[0].allowed_shapes,
            check_end::OperandSyntaxShape::CallTarget);
  EXPECT_EQ(syntax.variants[0].operand_layouts[3].slots[1].allowed_shapes,
            check_end::OperandSyntaxShape::CallTargetSet);
  EXPECT_EQ(resolved_descriptor.variants[0].operand_layouts[3]
                .bindings[0]
                .allowed_shapes,
            checker::OperandShape::IndirectCallee);
  EXPECT_EQ(resolved_descriptor.variants[0].operand_layouts[3]
                .fields[0]
                .value_kind,
            check_end::ResolvedValueKind::IndirectCallee);
  for (size_t index = 3; index != 6; ++index) {
    const auto& availability =
        checker_descriptor.variants[0].operand_layouts[index].availability;
    EXPECT_EQ(availability.minimum_ptx_version.major, 2u);
    EXPECT_EQ(availability.minimum_ptx_version.minor, 1u);
    EXPECT_EQ(availability.minimum_sm_version, 20u);
  }

  const auto indirect = parseModule(R"ptx(
.entry caller() {
  .reg .u64 %fptr;
  call %fptr;
}
)ptx");
  const auto indirect_resolved = resolveModule(indirect);
  ASSERT_FALSE(indirect_resolved.has_value());
  EXPECT_EQ(indirect_resolved.error().front().message,
            "Indirect call register targets require a function-local "
            ".callprototype or .calltargets metadata operand.");

  const auto indirect_forms = parseModule(R"ptx(
.func maybe_callee(.reg .u32 input);
.func another_callee(.reg .u32 value);
.entry caller() {
  .reg .u64 %fptr;
  .reg .u32 %result, %input;
empty_prototype: .callprototype _;
targets: .calltargets maybe_callee, another_callee;
returning_prototype: .callprototype (.reg .u32 result) _ (.reg .u32 input);
  call %fptr, empty_prototype;
  call %fptr, (%input), targets;
  call (%result), %fptr, (%input), returning_prototype;
}
)ptx");
  const auto resolved = resolveModule(indirect_forms);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto caller_scope =
      *resolved->symbols.symbol(resolved->functions[2].symbol_id).owned_scope;
  const auto fptr = resolved->symbols.lookup(caller_scope, "%fptr");
  const auto empty_prototype =
      resolved->symbols.lookup(caller_scope, "empty_prototype");
  ASSERT_TRUE(fptr.has_value());
  const auto targets = resolved->symbols.lookup(caller_scope, "targets");
  const auto returning_prototype =
      resolved->symbols.lookup(caller_scope, "returning_prototype");
  ASSERT_TRUE(empty_prototype.has_value());
  ASSERT_TRUE(targets.has_value());
  ASSERT_TRUE(returning_prototype.has_value());
  const auto& body = resolved->functions[2].body;
  ASSERT_EQ(body.size(), 3u);
  const auto& first = std::get<Call::Direct::TargetMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[0]).variant).operands);
  const auto& second = std::get<Call::Direct::TargetInputMetadataOperands>(
      std::get<Call::Direct>(std::get<Call>(body[1]).variant).operands);
  const auto& third =
      std::get<Call::Direct::ReturnTargetInputMetadataOperands>(
          std::get<Call::Direct>(std::get<Call>(body[2]).variant).operands);
  const auto& first_target = std::get<ResolvedRegisterRef>(first.target.value);
  ASSERT_TRUE(first_target.symbol_id.has_value());
  EXPECT_EQ(first_target.symbol_id, fptr->symbol);
  const auto& first_metadata =
      std::get<ResolvedIndirectMetadataRef>(first.metadata.value);
  EXPECT_EQ(first_metadata.symbol_id, empty_prototype->symbol);
  EXPECT_EQ(first_metadata.declaration_kind, binding::SymbolKind::CallPrototype);
  const auto& second_metadata =
      std::get<ResolvedIndirectMetadataRef>(second.metadata.value);
  EXPECT_EQ(second_metadata.symbol_id, targets->symbol);
  EXPECT_EQ(second_metadata.declaration_kind, binding::SymbolKind::CallTargetSet);
  const auto& second_target = std::get<ResolvedRegisterRef>(second.target.value);
  ASSERT_TRUE(second_target.symbol_id.has_value());
  EXPECT_EQ(second_target.symbol_id, fptr->symbol);
  const auto& third_target = std::get<ResolvedRegisterRef>(third.target.value);
  ASSERT_TRUE(third_target.symbol_id.has_value());
  EXPECT_EQ(third_target.symbol_id, fptr->symbol);
  const auto& third_metadata =
      std::get<ResolvedIndirectMetadataRef>(third.metadata.value);
  EXPECT_EQ(third_metadata.symbol_id, returning_prototype->symbol);
  EXPECT_EQ(third_metadata.declaration_kind,
            binding::SymbolKind::CallPrototype);

  const checker::Context old_target{
      .target = {.ptx_version = {2, 0}, .sm_version = 19},
      .instruction_range = indirect_forms.range,
  };
  const checker::Context supported_target{
      .target = {.ptx_version = {2, 1}, .sm_version = 20},
      .instruction_range = indirect_forms.range,
  };
  const auto rejected = checker::check(std::get<Call>(body[0]), old_target);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error()[1].kind,
            checker::CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      checker::check(std::get<Call>(body[0]), supported_target).has_value());
}

TEST(ResolvedModule, ReportsIndirectCallAbiMismatches) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func target(.reg .u32 input);
.func another_target(.reg .u32 value);
.entry caller(.param .u64 .ptr .shared .align 32 wrong_space) {
  .reg .u64 %fptr, %wide;
  .reg .b8 %byte;
arity: .callprototype (.reg .u32 result) _;
targets: .calltargets target, another_target;
bytes: .callprototype _ (.param .align 16 .b8 expected[8]);
pointer: .callprototype _ (.param .u64 .ptr .global .align 16 expected);
literal: .callprototype _ (.param .u16 expected);
  call %fptr, arity;
  call %fptr, (%wide), targets;
  call %fptr, (%byte), bytes;
  call %fptr, (wrong_space), pointer;
  call %fptr, (65536), literal;
}
)ptx"));

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 5u);
  EXPECT_EQ(resolved.error()[0].message,
            "Indirect call via metadata 'arity' has 0 return arguments but "
            "callee requires 1.");
  EXPECT_EQ(resolved.error()[1].message,
            "Indirect call via metadata 'targets' input argument 1 has type "
            "or vector shape mismatch.");
  EXPECT_EQ(resolved.error()[2].message,
            "Indirect call via metadata 'bytes' input argument 1 has call "
            "argument state-space mismatch.");
  EXPECT_EQ(resolved.error()[3].message,
            "Indirect call via metadata 'pointer' input argument 1 has "
            "pointed state-space mismatch.");
  EXPECT_EQ(resolved.error()[4].message,
            "Integer literal '65536' is out of range for scalar type 'U16'.");
}

TEST(ResolvedModule, IsolatesSameNamedIndirectMetadataByFunctionScope) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.func first() {
  .reg .u64 %fptr;
metadata: .callprototype _ (.reg .u32 input);
  call %fptr, metadata;
}
.func second() {
  .reg .u64 %fptr;
metadata: .callprototype _;
  call %fptr, metadata;
}
)ptx"));

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Indirect call via metadata 'metadata' has 0 input "
            "arguments but callee requires 1.");
}

TEST(ResolvedModule, RejectsEntryAsDirectCallTarget) {
  const auto ast = parseModule(R"ptx(
.entry callee() {}
.entry caller() {
  call callee;
}
)ptx");

  const auto resolved = resolveModule(ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().front().message,
            "Direct call target 'callee' must name a device .func, not an .entry.");
}

TEST(ResolvedModule, StandaloneDirectCallRemainsUnbound) {
  PtxSyntaxParser parser("call callee, (argument, 4);");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolveInstruction(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& call = std::get<Call>(*resolved);
  const auto& payload = std::get<Call::Direct::TargetInputOperands>(
      std::get<Call::Direct>(call.variant).operands);
  EXPECT_FALSE(payload.target.value.symbol_id.has_value());
  ASSERT_EQ(payload.arguments.value.values.size(), 2u);
  EXPECT_FALSE(std::get<ResolvedCallParameterRef>(
                   payload.arguments.value.values.front().value)
                   .symbol_id.has_value());
}

TEST(ResolvedModule, StandaloneResolutionRemainsDeclarationFree) {
  PtxSyntaxParser parser("@!%p7 add.u32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;

  const auto resolved = resolveInstruction(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto& instruction = std::get<Add>(*resolved);
  ASSERT_TRUE(instruction.execution_predicate.has_value());
  EXPECT_TRUE(instruction.execution_predicate->value.negated);
  EXPECT_EQ(instruction.execution_predicate->value.register_ref.index, 7u);
  EXPECT_FALSE(instruction.execution_predicate->value.register_ref.symbol_id
                   .has_value());
  const Add::IntegerNoSat& add = resolvedIntegerAdd(*resolved);
  EXPECT_FALSE(add.dst.value.symbol_id.has_value());
  EXPECT_FALSE(add.dst.value.declared_type.has_value());
  EXPECT_EQ(add.dst.value.index, 0u);
}

TEST(ResolvedModule, RunsDeclarationSemanticsBeforeInstructionResolution) {
  const auto ast = parseModule(R"ptx(
.global .u32 values[2] = {1, 2, 3};
.entry kernel() { ret; }
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().message,
            "Initializer dimension contains 3 elements but its declared "
            "extent is 2.");
}

TEST(ResolvedModule, ResolvesACompatibleFunctionDefinitionScope) {
  const auto ast = parseModule(R"ptx(
.func helper(.reg .u32 input);
.func helper(.reg .u32 input) {
  .reg .u32 %result;
  .reg .u32 %source;
  add.u32 %result, %source, 1;
}
)ptx");

  const auto resolved = resolveModule(ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_TRUE(resolved->functions.front().is_prototype);
  EXPECT_TRUE(resolved->functions.front().body.empty());
  EXPECT_FALSE(resolved->functions.back().is_prototype);
  ASSERT_EQ(resolved->functions.back().body.size(), 1u);
  const auto& add =
      std::get<Add>(resolved->functions.back().body.front()).variant;
  const auto& integer = std::get<Add::IntegerNoSat>(add);
  EXPECT_TRUE(integer.dst.value.symbol_id.has_value());
  EXPECT_TRUE(
      std::get<ResolvedRegisterRef>(integer.src1.value).symbol_id.has_value());
}

TEST(ResolvedModule, ResolvesSameModuleAliasCallsToCanonicalSignature) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.version 9.3
.func alias_fn(.param .u32 input);
.func target(.param .u32 input) {}
.alias alias_fn, target;
.entry kernel() {
  .reg .u32 %r;
  call alias_fn, (%r);
}
)ptx"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(ResolvedModule, ResolvesClusterSpecialRegisterFamilies) {
  const auto resolve_scalar = [](std::string_view instruction) {
    PtxSyntaxParser parser(instruction);
    const auto ast = parser.parseInstruction();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    return resolveInstruction(*ast);
  };
  for (const std::string_view source : {
           "mov.pred %p0, %is_explicit_cluster;",
           "mov.u32 %r0, %cluster_ctarank;",
           "mov.u32 %r0, %cluster_nctarank;",
           "mov.u32 %r0, %clusterid.x;",
           "mov.u32 %r0, %clusterid.y;",
           "mov.u32 %r0, %clusterid.z;",
           "mov.u32 %r0, %nclusterid.x;",
           "mov.u32 %r0, %nclusterid.y;",
           "mov.u32 %r0, %nclusterid.z;",
           "mov.u32 %r0, %cluster_ctaid.x;",
           "mov.u32 %r0, %cluster_ctaid.y;",
           "mov.u32 %r0, %cluster_ctaid.z;",
           "mov.u32 %r0, %cluster_nctaid.x;",
           "mov.u32 %r0, %cluster_nctaid.y;",
           "mov.u32 %r0, %cluster_nctaid.z;",
       }) {
    const auto resolved = resolve_scalar(source);
    ASSERT_TRUE(resolved.has_value())
        << source << ": " << resolved.error().message;
  }

  const auto explicit_cluster =
      resolve_scalar("mov.pred %p0, %is_explicit_cluster;");
  ASSERT_TRUE(explicit_cluster.has_value())
      << explicit_cluster.error().message;
  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = checker::TargetInfo{.ptx_version = {7, 8},
                                    .sm_version = 90,
                                    .capabilities = cluster_capabilities},
  };
  EXPECT_TRUE(checker::check(std::get<Mov>(*explicit_cluster), supported)
                  .has_value());
  const auto& special_source = std::get<ResolvedSpecialRegisterRef>(
      std::get<Mov::Pred>(std::get<Mov>(*explicit_cluster).variant)
          .src.value);
  EXPECT_EQ(special_source.id, base::lookup("%is_explicit_cluster")->id);
  auto old_ptx = supported;
  old_ptx.target.ptx_version = {7, 7};
  const auto rejected =
      checker::check(std::get<Mov>(*explicit_cluster), old_ptx);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  auto old_sm = supported;
  old_sm.target.sm_version = 80;
  const auto sm_rejected =
      checker::check(std::get<Mov>(*explicit_cluster), old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  EXPECT_FALSE(resolve_scalar("mov.pred %p0, %cluster_ctarank;").has_value());
  for (const std::string_view source : {
           R"ptx(.entry kernel() {
  .reg .u32 %r<3>;
  @%is_explicit_cluster add.u32 %r0, %r1, %r2;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u32 %r<3>;
  @%cluster_ctarank add.u32 %r0, %r1, %r2;
})ptx",
       }) {
    EXPECT_FALSE(resolveModule(parseModule(source)).has_value()) << source;
  }

  PtxSyntaxParser invalid_parser("mov.u32 %r0, %clusterid.w;");
  const auto invalid_ast = invalid_parser.parseInstruction();
  ASSERT_TRUE(invalid_ast.has_value())
      << invalid_ast.diagnostics.front().message;
  const auto invalid = resolveInstruction(*invalid_ast);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().message,
            "Unknown special register '%clusterid.w'.");
}

TEST(ResolvedModule, ResolvesV4ClusterSpecialRegisterMoves) {
  const auto ast = parseModule(R"ptx(
.entry kernel() {
  .reg .v4 .b32 %r0, %r1, %r2, %r3;
  mov.v4.u32 %r0, %clusterid;
  mov.v4.u32 %r1, %nclusterid;
  mov.v4.u32 %r2, %cluster_ctaid;
  mov.v4.u32 %r3, %cluster_nctaid;
}
)ptx");
  const auto resolved = resolveModule(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  for (const auto& instruction : body) {
    const auto& vector =
        std::get<Mov::V4U32>(std::get<Mov>(instruction).variant);
    EXPECT_EQ(vector.dst.value.register_ref.vector_width, 4u);
    EXPECT_EQ(vector.dst.value.register_ref.declared_type, ScalarType::B32);
    EXPECT_EQ(base::metadata(vector.src.value.id).vector_width, 4u);
  }

  constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};
  const checker::Context supported{
      .target = checker::TargetInfo{.ptx_version = {7, 8},
                                    .sm_version = 90,
                                    .capabilities = cluster_capabilities},
      .instruction_range = ast.range,
  };
  EXPECT_TRUE(checker::check(std::get<Mov>(body[0]), supported).has_value());
  auto old_ptx = supported;
  old_ptx.target.ptx_version = {7, 7};
  const auto ptx_rejected = checker::check(std::get<Mov>(body[0]), old_ptx);
  ASSERT_FALSE(ptx_rejected.has_value());
  EXPECT_EQ(ptx_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  auto old_sm = supported;
  old_sm.target.sm_version = 80;
  const auto sm_rejected = checker::check(std::get<Mov>(body[0]), old_sm);
  ASSERT_FALSE(sm_rejected.has_value());
  EXPECT_EQ(sm_rejected.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto reject = [](std::string_view declaration,
                         std::string_view source) {
    return resolveModule(parseModule(fmt::format(R"ptx(
.entry kernel() {{
  {}
  mov.v4.u32 %r0, {};
}}
)ptx",
                                                 declaration, source)));
  };
  EXPECT_FALSE(reject(".reg .u32 %r0;", "%clusterid").has_value());
  EXPECT_FALSE(reject(".reg .v4 .u32 %r0;", "%clusterid.x").has_value());
  EXPECT_FALSE(reject(".reg .v4 .u32 %r0;", "%cluster_ctarank").has_value());

  for (const std::string_view source : {
           R"ptx(.entry kernel() {
  .reg .u32 %r0;
  mov.u32 %r0, %clusterid;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u8 %b<4>;
  mov.b32 {%b0, %b1, %b2, %b3}, %clusterid;
})ptx",
           R"ptx(.entry kernel() {
  .reg .u64 %rd0;
  cvta.global.u64 %rd0, %clusterid;
})ptx",
       }) {
    EXPECT_FALSE(resolveModule(parseModule(source)).has_value()) << source;
  }
}

TEST(ResolvedModule, ChecksModuleTargetAvailabilityWithCatalogProfiles) {
  constexpr std::string_view no_target = R"ptx(
.version 7.8
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx";
  const auto resolved = resolveModule(parseModule(no_target));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto unavailable = checkModuleAvailability(parseModule(R"ptx(
.version 7.8
.target sm_80
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx"),
                                                   *resolved);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 2u);
  for (const auto& diagnostic : unavailable.error()) {
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto supported = checkModuleAvailability(parseModule(R"ptx(
.version 7.8
.target sm_90a, texmode_independent
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx"),
                                                 *resolved);
  EXPECT_TRUE(supported.has_value());

  const auto unknown = checkModuleAvailability(parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx"),
                                               *resolved);
  ASSERT_FALSE(unknown.has_value());
  ASSERT_EQ(unknown.error().size(), 1u);
  EXPECT_EQ(unknown.error().front().kind,
            checker::CheckDiagnosticKind::UnknownTarget);

  const auto pipeline_unknown = resolveModule(parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() { ret; }
)ptx"));
  ASSERT_FALSE(pipeline_unknown.has_value());
  ASSERT_EQ(pipeline_unknown.error().size(), 1u);
  EXPECT_EQ(pipeline_unknown.error().front().message,
            "Unknown validation target 'sm_123a'.");
}

TEST(ResolvedModule, AppliesFamilyProfilesThroughProductionAvailability) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.version 9.2
.entry kernel() {
  .reg .u8x4 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto sm120f = checkModuleAvailability(parseModule(R"ptx(
.version 9.2
.target sm_120f
.entry kernel() {
  .reg .u8x4 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx"), *resolved);
  EXPECT_TRUE(sm120f.has_value());

  const auto sm100f = resolveModule(parseModule(R"ptx(
.version 9.2
.target sm_100f
.entry kernel() {
  .reg .u8x4 %r<3>;
  add.u8x4 %r0, %r1, %r2;
}
)ptx"));
  ASSERT_FALSE(sm100f.has_value());
  EXPECT_EQ(sm100f.error().back().message,
            "Instruction variant 'PackedOptionalSat' requires target family "
            "'sm_120f'.");
}

TEST(ResolvedModule, AppliesTargetProfilesInSourceOrder) {
  const auto supported = resolveModule(parseModule(R"ptx(
.version 9.3
.target sm_80
.entry first() { ret; }
.target sm_90
.entry cluster() {
  .reg .u32 %r;
  { mov.u32 %r, %cluster_ctarank; }
}
)ptx"));
  ASSERT_TRUE(supported.has_value()) << supported.error().front().message;
  EXPECT_EQ(supported->functions.size(), 2u);

  const auto rejected = resolveModule(parseModule(R"ptx(
.version 9.3
.target sm_90
.entry first() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_80
.entry cluster() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx"));
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_EQ(rejected.error().front().message,
            "Operand value '%cluster_ctarank' has no matching availability "
            "clause.");
}

TEST(ResolvedModule, ClearsUnknownTargetAndKeepsFunctionIndicesAligned) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry first() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_123a
.entry skipped() { ret; }
.target sm_80
.entry rejected() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  const auto rejected = resolveModule(ast);
  ASSERT_FALSE(rejected.has_value());
  ASSERT_EQ(rejected.error().size(), 2u);
  EXPECT_EQ(rejected.error()[0].message,
            "Unknown validation target 'sm_123a'.");
  EXPECT_EQ(rejected.error()[0].range,
            std::get<syntax_ast::AstTargetDirective>(ast.items[3]).range);
  EXPECT_EQ(rejected.error()[1].message,
            "Operand value '%cluster_ctarank' has no matching availability "
            "clause.");
}

TEST(ResolvedModule, SharesDirectiveAvailabilityAcrossFunctionsAndPrototypes) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.version 9.3
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto sm30 = checkModuleAvailability(parseModule(R"ptx(
.version 9.3
.target sm_30
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx"), *resolved);
  ASSERT_FALSE(sm30.has_value());
  ASSERT_EQ(sm30.error().size(), 4u);
  for (const auto& diagnostic : sm30.error()) {
    EXPECT_EQ(diagnostic.kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }

  const auto sm80 = checkModuleAvailability(parseModule(R"ptx(
.version 9.3
.target sm_80
.func alias_fn(.param .u32 input) .noreturn;
.func target(.param .u32 input) .noreturn .abi_preserve 1 .abi_preserve_control 1 {
prototype: .callprototype _ .noreturn .abi_preserve 1 .abi_preserve_control 1;
}
.alias alias_fn, target;
)ptx"), *resolved);
  EXPECT_TRUE(sm80.has_value());
}

TEST(ResolvedModule, ChecksNestedInstructionModuleAvailability) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.version 0.9
.entry kernel() {
  .reg .u32 %r<3>;
  { add.u32 %r0, %r1, %r2; }
}
)ptx"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto unavailable = checkModuleAvailability(parseModule(R"ptx(
.version 0.9
.target sm_80
.entry kernel() {
  .reg .u32 %r<3>;
  { add.u32 %r0, %r1, %r2; }
}
)ptx"),
                                                   *resolved);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

TEST(ResolvedModule, ChecksAttributeModuleTargetAvailability) {
  const auto resolved = resolveModule(parseModule(R"ptx(
.version 8.0
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx"));
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto sm80 = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_80
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx"),
                                            *resolved);
  ASSERT_FALSE(sm80.has_value());
  ASSERT_EQ(sm80.error().size(), 1u);
  EXPECT_EQ(sm80.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);

  const auto sm90 = checkModuleAvailability(parseModule(R"ptx(
.version 8.0
.target sm_90
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx"),
                                            *resolved);
  EXPECT_TRUE(sm90.has_value());

  const auto unknown_without_version =
      checkModuleAvailability(parseModule(R"ptx(
.target sm_123a
.global .attribute(.unified(1, 2)) .u32 managed;
)ptx"),
                              *resolved);
  ASSERT_FALSE(unknown_without_version.has_value());
  ASSERT_EQ(unknown_without_version.error().size(), 1u);
  EXPECT_EQ(unknown_without_version.error().front().kind,
            checker::CheckDiagnosticKind::UnknownTarget);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
