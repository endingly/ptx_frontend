#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

// These checks use owned source and independently supplied known words.
namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap one source body in a complete, target-qualified PTX module. */
std::string tf32_source(std::string_view body,
                        std::string_view target = "sm_100a",
                        std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %m<8>;
  .reg .b64 %ad, %bd;
  .reg .u32 %ui;
  .reg .f64 %fa;
  .reg .f32 %fi;
  .reg .f32 %fm<8>;
  .reg .pred %p;
  .shared .align 8 .b64 barrier;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse without retaining a parser beyond an owned-resolution block. */
std::optional<syntax_ast::AstModule> parse_tf32(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Ask the existing module path to resolve and validate one source case. */
bool accepts_tf32(std::string_view source) {
  auto ast = parse_tf32(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Use the written target for direct owned-checker mutations. */
checker::Context tf32_context(const ResolvedModule& module) {
  const auto profile = base::find_target_profile("sm_100a");
  EXPECT_TRUE(profile);
  return checker::Context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = module.functions.front().instruction_ranges[0],
  };
}

/** Exercise all 16 typed group/A/mask/scale source choices. */
TEST(TcgenMmaTf32, CanonicalSourceTopologies) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      for (bool mask : {false, true}) {
        for (bool scale : {false, true}) {
          const std::string lanes = group == 1
                                        ? "{%m0, %m1, %m2, %m3}"
                                        : "{%m0, %m1, %m2, %m3, %m4, %m5, "
                                          "%m6, %m7}";
          const std::string instruction =
              "tcgen05.mma.cta_group::" + std::to_string(group) +
              ".kind::tf32 [%d], " + (shared_a ? "%ad" : "[%a]") + ", %bd, %i" +
              (mask ? ", " + lanes : "") + ", %p" + (scale ? ", 0" : "") + ";";
          SCOPED_TRACE(instruction);
          EXPECT_TRUE(accepts_tf32(tf32_source(instruction)));
        }
      }
    }
  }
  EXPECT_FALSE(accepts_tf32(tf32_source(
      "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, {}, %p;")));
  EXPECT_FALSE(accepts_tf32(tf32_source(
      "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %p, 16;")));
}

/** Reject wrong source roles and intersect source availability by profile. */
TEST(TcgenMmaTf32, RejectsMalformedSourcesAndTargets) {
  for (std::string_view instruction : {
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %fa, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], 0, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %fi, %p;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, 0, %p;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %ui;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, "
           "{%m0, %m1, %m2}, %p;",
           "tcgen05.mma.cta_group::2.kind::tf32 [%d], %ad, %bd, %i, "
           "{%m0, %m1, %m2, %m3}, %p;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %p, -1;",
           "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %p, "
           "4294967296;",
           "tcgen05.mma.kind::tf32.cta_group::1.kind::tf32 "
           "[%d], %ad, %bd, %i, %p;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_tf32(tf32_source(instruction)));
  }
  const std::string plain =
      "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %p;";
  const std::string scaled =
      "tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, %p, 0;";
  for (const auto& [target, version, plain_ok, scaled_ok] : {
           std::tuple{"sm_100a", "8.6", true, true},
           std::tuple{"sm_100f", "8.7", false, false},
           std::tuple{"sm_100f", "8.8", true, true},
           std::tuple{"sm_103a", "8.8", true, true},
           std::tuple{"sm_110a", "9.0", true, false},
           std::tuple{"sm_110f", "9.0", true, false},
           std::tuple{"sm_100", "9.3", false, false},
           std::tuple{"sm_120a", "9.3", false, false},
       }) {
    SCOPED_TRACE(std::string(target) + "/" + version);
    EXPECT_EQ(accepts_tf32(tf32_source(plain, target, version)), plain_ok);
    EXPECT_EQ(accepts_tf32(tf32_source(scaled, target, version)), scaled_ok);
  }
}

/** Both written orders select one typed form with located group tokens. */
TEST(TcgenMmaTf32, ExactOrderAliasPreservesIdentityAndLocations) {
  for (unsigned group : {1u, 2u}) {
    const std::string operands = " [%d], %ad, %bd, %i, %p;";
    const std::string canonical =
        "tcgen05.mma.cta_group::" + std::to_string(group) + ".kind::tf32" +
        operands;
    const std::string alias =
        "tcgen05.mma.kind::tf32.cta_group::" + std::to_string(group) + operands;
    SCOPED_TRACE(alias);
    auto canonical_ast = parse_tf32(tf32_source(canonical));
    auto alias_ast = parse_tf32(tf32_source(alias));
    ASSERT_TRUE(canonical_ast);
    ASSERT_TRUE(alias_ast);
    auto canonical_owned = resolveAndValidateModule(*canonical_ast);
    auto alias_owned = resolveAndValidateModule(*alias_ast);
    ASSERT_TRUE(canonical_owned.has_value());
    ASSERT_TRUE(alias_owned.has_value());
    const auto& canonical_instruction =
        *canonical_owned->functions.front().body[0].get();
    const auto& alias_instruction =
        *alias_owned->functions.front().body[0].get();
    const auto* canonical_form =
        dynamic_cast<const Tcgen05MmaTf32*>(&canonical_instruction);
    const auto* alias_form =
        dynamic_cast<const Tcgen05MmaTf32*>(&alias_instruction);
    ASSERT_TRUE(canonical_form);
    ASSERT_TRUE(alias_form);
    EXPECT_EQ(canonical_form->operand_layout.value,
              alias_form->operand_layout.value);
    ASSERT_EQ(canonical_form->cta_group.locs.size(), 1U);
    ASSERT_EQ(alias_form->cta_group.locs.size(), 1U);
    EXPECT_GT(canonical_form->cta_group.locs.front().start.column, 0);
    EXPECT_GT(alias_form->cta_group.locs.front().start.column, 0);
    const auto alias_view = tcgen_mma_tf32_view(alias_instruction);
    ASSERT_TRUE(alias_view);
    EXPECT_EQ(alias_view->group,
              group == 1 ? TcgenCtaGroup::One : TcgenCtaGroup::Two);
  }
  EXPECT_FALSE(
      accepts_tf32(tf32_source("tcgen05.mma.kind::tf32.cta_group::1.kind::tf32 "
                               "[%d], %ad, %bd, %i, %p;")));
}

/** Retain written negation, integer truth and borrowed roles after AST death. */
TEST(TcgenMmaTf32, OwnedPredicateAndRoles) {
  for (const auto& [source, truth] : {
           std::pair{"0", false},
           std::pair{"2", true},
           std::pair{"-1", true},
           std::pair{"4294967296", true},
       }) {
    SCOPED_TRACE(source);
    std::optional<ResolvedModule> owned;
    {
      auto ast = parse_tf32(tf32_source(
          std::string("tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, "
                      "%bd, %i, ") +
          source + ";"));
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result.has_value());
      owned = std::move(*result);
    }
    const auto* instruction = owned->functions.front().body[0].get();
    ASSERT_TRUE(instruction);
    const auto view = tcgen_mma_tf32_view(*instruction);
    ASSERT_TRUE(view);
    ASSERT_TRUE(
        std::holds_alternative<ResolvedPredicateConstant>(*view->enable_d));
    EXPECT_EQ(std::get<ResolvedPredicateConstant>(*view->enable_d).value,
              truth);
    EXPECT_TRUE(view->a_shared);
    EXPECT_EQ(view->a_shared->role, MatrixFragmentRole::A);
    EXPECT_EQ(view->b.role, MatrixFragmentRole::B);
    EXPECT_TRUE(validateModule(*owned).has_value());
  }
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_tf32(
        tf32_source("tcgen05.mma.cta_group::1.kind::tf32 [%d], %ad, %bd, %i, "
                    "{%fm0, %fm1, %fm2, %fm3}, !%p, 15;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned = std::move(*result);
  }
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05MmaTf32&>(instruction);
  ASSERT_TRUE(form.a_register);
  ASSERT_TRUE(form.disable_output_lane);
  ASSERT_TRUE(form.scale_input_d);
  const auto view = tcgen_mma_tf32_view(instruction);
  ASSERT_TRUE(view);
  EXPECT_TRUE(std::get<ResolvedPredicate>(*view->enable_d).negated);
  EXPECT_EQ(view->disable_output_lane->elements.size(), 4U);
  EXPECT_EQ(view->scale_d->integer_source_bits, 15U);
  const auto context = tf32_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(instruction.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  const auto saved = form.enable_input_d.locs;
  form.enable_input_d.locs.clear();
  reject();
  form.enable_input_d.locs = saved;
  form.scale_input_d->value.integer_source_bits.reset();
  reject();
  form.scale_input_d->value.integer_source_bits = 15;
  form.a_register->value.register_class = ResolvedRegisterClass::Predicate;
  reject();
  form.a_register->value.register_class = ResolvedRegisterClass::General;
  form.b.value.declared_type = ScalarType::B32;
  reject();
  form.b.value.declared_type = ScalarType::B64;
  form.idesc.value.declared_type = ScalarType::F32;
  reject();
  form.idesc.value.declared_type = ScalarType::B32;
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::B64;
  reject();
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::F32;
  auto& predicate = std::get<ResolvedPredicate>(form.enable_input_d.value);
  predicate.register_ref.register_class = ResolvedRegisterClass::General;
  reject();
  predicate.register_ref.register_class = ResolvedRegisterClass::Predicate;
  form.scale_input_d->value.bits = 14;
  reject();
  form.scale_input_d->value.bits = 15;
  form.scale_input_d->value.integer_source_bits = 1ULL << 32;
  reject();
  form.scale_input_d->value.integer_source_bits = 15;
  const auto group_ranges = form.cta_group.locs;
  form.cta_group.locs.clear();
  reject();
  form.cta_group.locs = group_ranges;
  const auto mask_ranges = form.disable_output_lane->locs;
  form.disable_output_lane->locs.pop_back();
  reject();
  form.disable_output_lane->locs = mask_ranges;
  form.operand_layout.value = 0;
  reject();
  EXPECT_FALSE(tcgen_mma_tf32_view(instruction));
  form.operand_layout.value = 3;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Field, Table 42, Table 57 and missing-fact reports stay separate. */
TEST(TcgenMmaTf32, LiteralKnownOperationRules) {
  constexpr uint32_t bits = 0x910U | (4U << 24) | (1U << 17);
  const TcgenSharedWord normal{1ULL << 46};
  const TcgenSharedWord atom32{(1ULL << 46) | (1ULL << 61)};
  TcgenTf32KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction =
          TcgenInstructionWord{bits | (1U << 15), TcgenMmaKind::Tf32},
      .a_shared_word = atom32,
      .b_shared_word = normal,
      .a_context = {.major = TcgenMajor::MN},
      .b_context = {.major = TcgenMajor::K},
  };
  const auto has = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  const auto a = check_tcgen_tf32_known_operation(facts);
  EXPECT_TRUE(a.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(a.supplied_facts_ok());
  EXPECT_FALSE(has(a.violations, TcgenTf32Violation::ASwizzle));
  facts.a_shared_word = normal;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::ASwizzle));
  facts.instruction.bits = bits | (1U << 16);
  facts.a_shared_word = normal;
  facts.b_shared_word = atom32;
  facts.a_context.major = TcgenMajor::K;
  facts.b_context.major = TcgenMajor::MN;
  EXPECT_FALSE(has(check_tcgen_tf32_known_operation(facts).violations,
                   TcgenTf32Violation::BSwizzle));
  facts.b_shared_word = normal;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::BSwizzle));
  facts.group = TcgenCtaGroup::Two;
  const auto bad_shape = check_tcgen_tf32_known_operation(facts);
  EXPECT_TRUE(bad_shape.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(has(bad_shape.violations, TcgenTf32Violation::Shape));
  facts.group = TcgenCtaGroup::One;
  facts.a_in_tmem = true;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::APlacementFacts));
  facts.a_shared_word.reset();
  facts.instruction.bits = bits;
  const auto unknown = check_tcgen_tf32_known_operation(facts);
  EXPECT_TRUE(has(unknown.missing, TcgenTf32Obligation::ALaneHalf));
  EXPECT_TRUE(has(unknown.missing, TcgenTf32Obligation::DLaneHalf));
  facts.a_lane_half = 0;
  facts.d_lane_half = 16;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::HalfAlignment));
}

/** Keep defined fields, dense operation facts and conflicting context apart. */
TEST(TcgenMmaTf32, KnownFieldAndContextSeparation) {
  TcgenTf32KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{0x910U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::Tf32},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K},
  };
  const auto has = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  const auto clean = check_tcgen_tf32_known_operation(facts);
  EXPECT_TRUE(clean.instruction_fields.defined_fields_ok());
  EXPECT_FALSE(has(clean.violations, TcgenTf32Violation::DenseSparse));
  EXPECT_TRUE(has(clean.missing, TcgenTf32Obligation::Target));
  facts.instruction.bits |= 1U << 6;
  EXPECT_FALSE(check_tcgen_tf32_known_operation(facts)
                   .instruction_fields.defined_fields_ok());
  facts.instruction.bits &= ~(1U << 6);
  for (uint32_t type_bit : {1U << 4, 1U << 8, 1U << 11}) {
    auto wrong_type = facts;
    wrong_type.instruction.bits ^= type_bit;
    const auto report = check_tcgen_tf32_known_operation(wrong_type);
    EXPECT_FALSE(report.instruction_fields.defined_fields_ok());
    EXPECT_FALSE(report.supplied_facts_ok());
  }
  facts.instruction.bits |= 1U << 2;
  const auto sparse = check_tcgen_tf32_known_operation(facts);
  EXPECT_TRUE(sparse.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(has(sparse.violations, TcgenTf32Violation::DenseSparse));
  facts.instruction.bits &= ~(1U << 2);
  facts.instruction.kind = TcgenMmaKind::F16;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::Kind));
  facts.instruction.kind = TcgenMmaKind::Tf32;
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm110a = base::find_target_profile("sm_110a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm110a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{9, 3};
  facts.a_context.target = sm110a->identity;
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::InvalidContext));
  facts.a_context.target = sm100a->identity;
  facts.b_context.ptx_version = checker::PtxVersion{9, 0};
  EXPECT_TRUE(has(check_tcgen_tf32_known_operation(facts).violations,
                  TcgenTf32Violation::InvalidContext));
  facts.b_context.ptx_version = checker::PtxVersion{9, 3};
  EXPECT_FALSE(has(check_tcgen_tf32_known_operation(facts).violations,
                   TcgenTf32Violation::InvalidContext));
}

/** Use profile inheritance for plain/scaled tf32 rather than numeric SMs. */
TEST(TcgenMmaTf32, KnownTargetIntersections) {
  TcgenTf32KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction = TcgenInstructionWord{0x910U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::Tf32},
  };
  const auto has = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  for (const auto& [name, version, scaled, accepted] : {
           std::tuple{"sm_100a", checker::PtxVersion{8, 6}, true, true},
           std::tuple{"sm_103a", checker::PtxVersion{8, 8}, true, true},
           std::tuple{"sm_103f", checker::PtxVersion{8, 8}, false, true},
           std::tuple{"sm_110a", checker::PtxVersion{9, 0}, false, true},
           std::tuple{"sm_110f", checker::PtxVersion{9, 0}, true, false},
           std::tuple{"sm_100", checker::PtxVersion{9, 3}, false, false},
           std::tuple{"sm_120a", checker::PtxVersion{9, 3}, true, false},
       }) {
    SCOPED_TRACE(name);
    const auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    facts.scaled_d = scaled;
    const auto report = check_tcgen_tf32_known_operation(facts);
    EXPECT_EQ(!has(report.violations, TcgenTf32Violation::Target), accepted);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
