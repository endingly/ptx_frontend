#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tensor_map_known_facts.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a complete kernel, leaving no syntax AST alive in the result. */
std::optional<ResolvedModule> cache_module(
    std::string_view instruction, std::string_view target = "sm_110a") {
  const std::string source =
      ".version 9.3\n.target " + std::string(target) +
      "\n.address_size 64\n"
      ".global .align 64 .b8 tensor_map[128];\n"
      ".shared .align 16 .b8 dst[1024];\n"
      ".shared .align 16 .b8 src[1024];\n"
      ".shared .align 8 .b64 mbar;\n"
      ".entry kernel() {\n"
      ".reg .s32 %r<5>;\n.reg .u16 %h<3>;\n.reg .b16 %mask;\n"
      ".reg .b64 %b;\n.reg .u64 %u;\n.reg .s64 %s;\n"
      ".reg .b32 %x32;\n.reg .f64 %f64;\n.reg .pred %pred;\n"
      ".reg .b64 %v<2>;\n" +
      std::string(instruction) + "\n}\n";
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Select the same catalog target used by full-module validation. */
checker::Context cache_context(std::string_view target = "sm_110a") {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Fixed source examples span every supported tensor transfer direction. */
struct CacheCase {
  /** Complete source spelling with a final scalar policy. */
  std::string_view text;
  /** Independently expected tensor data movement direction. */
  TensorKnownFactDirection direction;
  /** Target profile admitting this exact form. */
  std::string_view target = "sm_110a";
};

constexpr std::array kCases{
    CacheCase{
        "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx:"
        ":bytes.L2::cache_hint [dst], [tensor_map, {%r0}], [mbar], %b;",
        TensorKnownFactDirection::Load},
    CacheCase{"cp.async.bulk.tensor.1d.shared::cluster.global.tile.mbarrier::"
              "complete_tx::bytes.L2::cache_hint [dst], [tensor_map, {%r0}], "
              "[mbar], %b;",
              TensorKnownFactDirection::Load},
    CacheCase{"cp.async.bulk.tensor.1d.global.shared::cta.tile.bulk_group.L2::"
              "cache_hint [tensor_map, {%r0}], [src], %b;",
              TensorKnownFactDirection::Store},
    CacheCase{"cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.tile.bulk_"
              "group.L2::cache_hint [tensor_map, {%r0}], [src], %b;",
              TensorKnownFactDirection::Reduce},
    CacheCase{"cp.async.bulk.prefetch.tensor.1d.L2.global.tile.L2::cache_hint "
              "[tensor_map, {%r0}], %b;",
              TensorKnownFactDirection::Prefetch},
    CacheCase{"cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs.bulk_"
              "group.L2::cache_hint [tensor_map, {%r0, %r1, %r2}], [src], %b;",
              TensorKnownFactDirection::Store},
    CacheCase{"cp.async.bulk.tensor.2d.shared::cta.global.tile::gather4."
              "mbarrier::complete_tx::bytes.L2::cache_hint [dst], [tensor_map, "
              "{%r0, %r1, %r2, %r3, %r4}], [mbar], %b;",
              TensorKnownFactDirection::Load},
    CacheCase{
        "cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group."
        "L2::cache_hint [tensor_map, {%r0, %r1, %r2, %r3, %r4}], [src], %b;",
        TensorKnownFactDirection::Store, "sm_110f"},
};

/** Every canonical direction owns controls and retains the old facts projection. */
TEST(TensorCacheControls, DirectionsOwnedQueryAndKnownFacts) {
  for (const auto& item : kCases) {
    SCOPED_TRACE(std::string(item.text));
    auto module = cache_module(item.text, item.target);
    ASSERT_TRUE(module);
    const auto& instruction = *module->functions.front().body.front();
    EXPECT_TRUE(instruction.check(cache_context(item.target)));
    EXPECT_TRUE(validateModule(*module,
                               ModuleValidationPolicy::RequireCompleteContext));
    const auto facts = project_tensor_known_access_context(
        instruction, cache_context(item.target));
    ASSERT_TRUE(facts.diagnostics.empty());
    ASSERT_TRUE(facts.access);
    EXPECT_EQ(facts.access->direction, item.direction);
    auto selected = query_tensor_cache_controls(instruction);
    EXPECT_TRUE(selected.applicable);
    ASSERT_TRUE(selected.hint);
    ASSERT_TRUE(selected.policy);
    EXPECT_TRUE(selected.diagnostics.empty());
    EXPECT_EQ(std::get<ResolvedRegisterRef>(selected.policy->value).spelling,
              "%b");
    module.reset();
    EXPECT_TRUE(selected.hint->value);
    EXPECT_EQ(std::get<ResolvedRegisterRef>(selected.policy->value).spelling,
              "%b");
  }
}

/** The formal hint-only state and the unhinted parent remain distinct. */
TEST(TensorCacheControls, SourceStateCoupling) {
  const std::string_view base =
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes";
  auto hint =
      cache_module(std::string(base) +
                   ".L2::cache_hint [dst], [tensor_map, {%r0}], [mbar];");
  ASSERT_TRUE(hint);
  EXPECT_TRUE(hint->functions.front().body.front()->check(cache_context()));
  EXPECT_TRUE(
      validateModule(*hint, ModuleValidationPolicy::RequireCompleteContext));
  const auto hinted =
      query_tensor_cache_controls(*hint->functions.front().body.front());
  EXPECT_TRUE(hinted.applicable);
  EXPECT_TRUE(hinted.hint);
  EXPECT_FALSE(hinted.policy);
  EXPECT_TRUE(hinted.diagnostics.empty());
  auto parent =
      cache_module(std::string(base) + " [dst], [tensor_map, {%r0}], [mbar];");
  ASSERT_TRUE(parent);
  const auto plain =
      query_tensor_cache_controls(*parent->functions.front().body.front());
  EXPECT_TRUE(plain.applicable);
  EXPECT_FALSE(plain.hint);
  EXPECT_FALSE(plain.policy);
  EXPECT_FALSE(cache_module(std::string(base) +
                            " [dst], [tensor_map, {%r0}], [mbar], %b;"));
  auto unrelated =
      cache_module("cp.async.ca.shared.global [dst], [tensor_map], 16;");
  ASSERT_TRUE(unrelated);
  EXPECT_FALSE(
      query_tensor_cache_controls(*unrelated->functions.front().body.front())
          .applicable);
}

/** Literal bits and source sign remain owned independently after resolution. */
TEST(TensorCacheControls, PolicyLiteralsAndRegisterTypes) {
  struct Literal {
    /** Source spelling. */ std::string_view text;
    /** Converted B64 word. */ uint64_t bits;
    /** Original signed-negative provenance. */ bool negative;
  };
  constexpr std::array literals{
      Literal{"0", 0, false}, Literal{"1", 1, false},
      Literal{"18446744073709551615", UINT64_MAX, false},
      Literal{"-1", UINT64_MAX, true}};
  const std::string prefix =
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes.L2::cache_hint [dst], [tensor_map, {%r0}], [mbar], ";
  for (const auto& literal : literals) {
    auto module = cache_module(prefix + std::string(literal.text) + ";");
    ASSERT_TRUE(module);
    auto& instruction = *module->functions.front().body.front();
    EXPECT_TRUE(instruction.check(cache_context()));
    const auto selected = query_tensor_cache_controls(instruction);
    ASSERT_TRUE(selected.policy);
    const auto* value = std::get_if<ResolvedImmediate>(&selected.policy->value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->bits, literal.bits);
    EXPECT_EQ(value->integer_source_bits, literal.bits);
    EXPECT_EQ(value->is_negative, literal.negative);
  }
  for (const std::string_view register_name : {"%b", "%u", "%s"}) {
    auto module = cache_module(prefix + std::string(register_name) + ";");
    ASSERT_TRUE(module);
    EXPECT_TRUE(module->functions.front().body.front()->check(cache_context()));
  }
  for (const std::string_view register_name :
       {"%x32", "%f64", "%pred", "{%v0, %v1}"}) {
    auto module = cache_module(prefix + std::string(register_name) + ";");
    if (module)
      EXPECT_FALSE(
          module->functions.front().body.front()->check(cache_context()));
  }
}

/** Source damage and layout tampering fail direct and module validation. */
TEST(TensorCacheControls, OwnedTamperAndEqualArity) {
  auto module = cache_module(kCases.front().text);
  ASSERT_TRUE(module);
  auto* selected = dynamic_cast<CpAsyncBulkTensor1dSharedCtaCacheHint*>(
      module->functions.front().body.front().get());
  ASSERT_NE(selected, nullptr);
  ASSERT_TRUE(selected->cache_policy);
  ASSERT_TRUE(selected->check(cache_context()));
  const auto original_hint = selected->cache_hint;
  selected->cache_hint.locs.clear();
  EXPECT_FALSE(selected->check(cache_context()));
  EXPECT_FALSE(query_tensor_cache_controls(*selected).diagnostics.empty());
  selected->cache_hint = original_hint;
  const auto original_policy = *selected->cache_policy;
  auto* reg = std::get_if<ResolvedRegisterRef>(&selected->cache_policy->value);
  ASSERT_NE(reg, nullptr);
  reg->declared_type = ScalarType::B32;
  EXPECT_FALSE(selected->check(cache_context()));
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  selected->cache_policy = original_policy;
  selected->operand_layout.value = 0;
  EXPECT_FALSE(selected->check(cache_context()));
  EXPECT_FALSE(query_tensor_cache_controls(*selected).diagnostics.empty());
  selected->operand_layout.value = 1;
  EXPECT_TRUE(selected->check(cache_context()));
  const std::string_view absent_info =
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes.multicast::cluster.cta_group::2.L2::cache_hint "
      "[dst], [tensor_map, {%r0, %r1, %r2}], [mbar], %mask, %b;";
  const std::string_view present_info =
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes.cta_group::2.multicast::cluster.L2::cache_hint "
      "[dst], [tensor_map, {%r0, %r1, %r2}], [mbar], {%h0}, %mask;";
  for (const auto text : {absent_info, present_info}) {
    auto parsed = cache_module(text);
    ASSERT_TRUE(parsed);
    auto& instruction = *parsed->functions.front().body.front();
    EXPECT_TRUE(instruction.check(cache_context()));
    EXPECT_EQ(query_tensor_cache_controls(instruction).policy.has_value(),
              text == absent_info);
  }
}

/** A policy register contributes its exact borrowed source and location. */
TEST(TensorCacheControls, PolicyReferenceVisitorAndUnavailableTarget) {
  auto module = cache_module(kCases.front().text);
  ASSERT_TRUE(module);
  auto& instruction = *module->functions.front().body.front();
  const auto copied = query_tensor_cache_controls(instruction);
  ASSERT_TRUE(copied.policy);
  ASSERT_EQ(copied.policy->locs.size(), 1u);
  class PolicyObserver final : public detail::IReferenceObserver {
   public:
    /** Count only the selected policy reference at its owned source range. */
    void reg_or_imm(const RegOrImm& value,
                    std::span<const SourceRange> locations,
                    checker::AddressSymbolResolutionPolicy) override {
      if (locations.size() == 1 && locations.front() == expected &&
          std::holds_alternative<ResolvedRegisterRef>(value))
        ++count;
    }
    /** Policy source range copied from the selected query. */
    SourceRange expected;
    /** Matching policy references observed from this instruction. */
    int count = 0;
  } observer;
  observer.expected = copied.policy->locs.front();
  instruction.visit_references(observer);
  EXPECT_EQ(observer.count, 1);
  const auto unavailable = cache_context("sm_80");
  EXPECT_FALSE(instruction.check(unavailable));
  const auto projected =
      project_tensor_known_access_context(instruction, unavailable);
  ASSERT_TRUE(projected.diagnostics.empty());
  ASSERT_TRUE(projected.access);
  ASSERT_TRUE(projected.access->selected_variant_available);
  EXPECT_FALSE(*projected.access->selected_variant_available);
}

/** Recheck every mutable source carrier after the syntax AST is gone. */
TEST(TensorCacheControls, MutatedPolicyCarrierAndBindings) {
  auto module = cache_module(kCases.front().text);
  ASSERT_TRUE(module);
  auto* selected = dynamic_cast<CpAsyncBulkTensor1dSharedCtaCacheHint*>(
      module->functions.front().body.front().get());
  ASSERT_NE(selected, nullptr);
  ASSERT_TRUE(selected->cache_policy);
  auto direct = [&] {
    return selected->check(cache_context());
  };
  auto complete = [&] {
    return validateModule(*module,
                          ModuleValidationPolicy::RequireCompleteContext);
  };
  ASSERT_TRUE(direct());
  ASSERT_TRUE(complete());
  const auto original_hint = selected->cache_hint;
  selected->cache_hint.value = false;
  EXPECT_FALSE(direct());
  EXPECT_FALSE(complete());
  selected->cache_hint = original_hint;
  const auto original_policy = *selected->cache_policy;
  selected->cache_policy->locs.clear();
  EXPECT_FALSE(direct());
  EXPECT_FALSE(complete());
  selected->cache_policy = original_policy;
  auto* reg = std::get_if<ResolvedRegisterRef>(&selected->cache_policy->value);
  ASSERT_NE(reg, nullptr);
  const auto original_register = *reg;
  reg->register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(direct());
  EXPECT_FALSE(complete());
  *reg = original_register;
  reg->vector_width = 2;
  EXPECT_FALSE(direct());
  EXPECT_FALSE(complete());
  *reg = original_register;
  reg->declared_type.reset();
  EXPECT_FALSE(direct());
  EXPECT_FALSE(complete());
  *reg = original_register;
  reg->symbol_id = binding::SymbolId{UINT32_MAX};
  EXPECT_FALSE(complete());
  *reg = original_register;
  ASSERT_TRUE(reg->symbol_id);
  auto& declaration =
      const_cast<binding::Symbol&>(module->symbols.symbol(*reg->symbol_id));
  const auto original_type = declaration.type;
  declaration.type = "b32";
  EXPECT_FALSE(complete());
  declaration.type = original_type;
  const auto original_width = declaration.vector_width;
  declaration.vector_width = 2;
  EXPECT_FALSE(complete());
  declaration.vector_width = original_width;
  EXPECT_TRUE(direct());
  EXPECT_TRUE(complete());
}

/** Converted B64 words must retain their original integer source bits. */
TEST(TensorCacheControls, MutatedImmediateProvenance) {
  const std::string source =
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes.L2::cache_hint [dst], [tensor_map, {%r0}], [mbar], -1;";
  auto module = cache_module(source);
  ASSERT_TRUE(module);
  auto* selected = dynamic_cast<CpAsyncBulkTensor1dSharedCtaCacheHint*>(
      module->functions.front().body.front().get());
  ASSERT_NE(selected, nullptr);
  ASSERT_TRUE(selected->cache_policy);
  auto* value = std::get_if<ResolvedImmediate>(&selected->cache_policy->value);
  ASSERT_NE(value, nullptr);
  const auto original = *value;
  ASSERT_TRUE(selected->check(cache_context()));
  value->bits = 1;
  EXPECT_FALSE(selected->check(cache_context()));
  *value = original;
  value->integer_source_bits.reset();
  EXPECT_FALSE(selected->check(cache_context()));
  *value = original;
  value->type = ScalarType::B32;
  EXPECT_FALSE(selected->check(cache_context()));
  *value = original;
  value->is_negative = false;
  EXPECT_TRUE(selected->check(cache_context()));
  const auto copied = query_tensor_cache_controls(*selected);
  ASSERT_TRUE(copied.policy);
  EXPECT_FALSE(std::get<ResolvedImmediate>(copied.policy->value).is_negative);
  *value = original;
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Structural reference traversal checks non-tensor and nested registers alike. */
TEST(TensorCacheControls, GenericRegisterBindingTraversal) {
  const auto mismatch_at = [](const ResolvedModule& module,
                              SourceRange expected) {
    const auto checked =
        validateModule(module, ModuleValidationPolicy::RequireCompleteContext);
    if (checked)
      return false;
    for (const auto& issue : checked.error())
      if (issue.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch &&
          issue.range == expected &&
          issue.message ==
              "Resolved register metadata disagrees with its "
              "owned declaration.")
        return true;
    return false;
  };

  auto arithmetic = cache_module("add.s32 %r1, %r2, %r3;");
  ASSERT_TRUE(arithmetic);
  ASSERT_TRUE(validateModule(*arithmetic,
                             ModuleValidationPolicy::RequireCompleteContext));
  auto& add = dynamic_cast<AddIntegerNoSat&>(
      *arithmetic->functions.front().body.front());
  const auto destination_location = add.dst.locs.front();
  add.dst.value.declared_type = ScalarType::U32;
  EXPECT_TRUE(mismatch_at(*arithmetic, destination_location));
  add.dst.value.declared_type = ScalarType::S32;
  auto& source = std::get<ResolvedRegisterRef>(add.src1.value);
  const auto source_location = add.src1.locs.front();
  source.register_class = static_cast<ResolvedRegisterClass>(255);
  EXPECT_TRUE(mismatch_at(*arithmetic, source_location));

  auto vector = cache_module("st.global.v2.b64 [tensor_map], {%v0, %v1};");
  ASSERT_TRUE(vector);
  ASSERT_TRUE(
      validateModule(*vector, ModuleValidationPolicy::RequireCompleteContext));
  auto& store =
      dynamic_cast<StExplicitVector&>(*vector->functions.front().body.front());
  ASSERT_TRUE(store.src.value.elements.front());
  store.src.value.elements.front()->declared_type = ScalarType::B32;
  EXPECT_TRUE(mismatch_at(*vector, store.src.locs.front()));

  auto predicate =
      cache_module("cp.async.ca.shared.global [dst], [tensor_map], 4, %pred;");
  ASSERT_TRUE(predicate);
  ASSERT_TRUE(validateModule(*predicate,
                             ModuleValidationPolicy::RequireCompleteContext));
  auto& pred_copy = dynamic_cast<CpAsyncCaSharedGlobalControl&>(
      *predicate->functions.front().body.front());
  std::get<ResolvedPredicate>(pred_copy.source_control.value)
      .register_ref.declared_type = ScalarType::B32;
  EXPECT_TRUE(mismatch_at(*predicate, pred_copy.source_control.locs.front()));

  auto policy = cache_module(
      "cp.async.ca.shared.global.L2::cache_hint "
      "[dst], [tensor_map], 4, %b;");
  ASSERT_TRUE(policy);
  ASSERT_TRUE(
      validateModule(*policy, ModuleValidationPolicy::RequireCompleteContext));
  auto& policy_copy = dynamic_cast<CpAsyncCaSharedGlobalCacheHintControl&>(
      *policy->functions.front().body.front());
  std::get<ResolvedCpAsyncCachePolicy>(policy_copy.source_control.value)
      .register_ref.declared_type = ScalarType::B32;
  EXPECT_TRUE(mismatch_at(*policy, policy_copy.source_control.locs.front()));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
