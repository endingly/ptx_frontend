#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

// Tests below keep opaque source registers distinct from caller-known words.
namespace ptx_frontend::resolved_ir {
namespace {

/** Enclose one dense integer source in a target-qualified module. */
std::string i8_source(std::string_view body,
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

/** Parse a module without retaining its parser after the returned AST. */
std::optional<syntax_ast::AstModule> parse_i8(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Run the ordinary owned module path for a single source case. */
bool accepts_i8(std::string_view source) {
  auto ast = parse_i8(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Give direct checker mutations the exact declared target context. */
checker::Context i8_context(const ResolvedModule& module) {
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

/** Match one literal enum value without treating missing facts as success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& values, T item) {
  return std::find(values.begin(), values.end(), item) != values.end();
}

/** Eight source topologies are four owned layouts times two typed groups. */
TEST(TcgenMmaI8, CanonicalSourceTopologies) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      for (bool mask : {false, true}) {
        const std::string lanes = group == 1 ? "{%m0, %m1, %m2, %m3}"
                                             : "{%m0, %m1, %m2, %m3, %m4, %m5, "
                                               "%m6, %m7}";
        const std::string instruction =
            "tcgen05.mma.cta_group::" + std::to_string(group) +
            ".kind::i8 [%d], " + (shared_a ? "%ad" : "[%a]") + ", %bd, %i" +
            (mask ? ", " + lanes : "") + ", %p;";
        SCOPED_TRACE(instruction);
        EXPECT_TRUE(accepts_i8(i8_source(instruction)));
      }
    }
  }
  EXPECT_FALSE(accepts_i8(i8_source(
      "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, {}, %p;")));
  EXPECT_FALSE(accepts_i8(i8_source(
      "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, %p, 0;")));
}

/** Reject source saturation, malformed roles and excluded target profiles. */
TEST(TcgenMmaI8, RejectedSourceAndExactAvailability) {
  for (std::string_view instruction : {
           "tcgen05.mma.cta_group::1.kind::i8.satfinite "
           "[%d], %ad, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], %fa, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], 0, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %fi, %p;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, 0, %p;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, %ui;",
           "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, "
           "{%m0, %m1, %m2}, %p;",
           "tcgen05.mma.cta_group::2.kind::i8 [%d], %ad, %bd, %i, "
           "{%m0, %m1, %m2, %m3}, %p;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_i8(i8_source(instruction)));
  }
  const std::string plain =
      "tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, %p;";
  for (const auto& [target, version, accepted] : {
           std::tuple{"sm_100a", "8.5", false},
           std::tuple{"sm_100a", "8.6", true},
           std::tuple{"sm_110a", "8.8", false},
           std::tuple{"sm_110a", "9.0", true},
           std::tuple{"sm_103a", "8.8", false},
           std::tuple{"sm_100f", "8.8", false},
           std::tuple{"sm_110f", "9.0", false},
           std::tuple{"sm_100", "9.3", false},
           std::tuple{"sm_120a", "9.3", false},
       }) {
    SCOPED_TRACE(std::string(target) + "/" + version);
    EXPECT_EQ(accepts_i8(i8_source(plain, target, version)), accepted);
  }
}

/** Both approved written orders retain one typed form, layout and group range. */
TEST(TcgenMmaI8, ExactOrderAliasPreservesIdentityAndLocations) {
  for (unsigned group : {1u, 2u}) {
    const std::string operands = " [%d], %ad, %bd, %i, %p;";
    const std::string canonical =
        "tcgen05.mma.cta_group::" + std::to_string(group) + ".kind::i8" +
        operands;
    const std::string alias =
        "tcgen05.mma.kind::i8.cta_group::" + std::to_string(group) + operands;
    SCOPED_TRACE(alias);
    auto canonical_ast = parse_i8(i8_source(canonical));
    auto alias_ast = parse_i8(i8_source(alias));
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
        dynamic_cast<const Tcgen05MmaI8*>(&canonical_instruction);
    const auto* alias_form =
        dynamic_cast<const Tcgen05MmaI8*>(&alias_instruction);
    ASSERT_TRUE(canonical_form);
    ASSERT_TRUE(alias_form);
    EXPECT_EQ(canonical_form->operand_layout.value,
              alias_form->operand_layout.value);
    ASSERT_EQ(canonical_form->cta_group.locs.size(), 1U);
    ASSERT_EQ(alias_form->cta_group.locs.size(), 1U);
    EXPECT_GT(canonical_form->cta_group.locs.front().start.column, 0);
    EXPECT_GT(alias_form->cta_group.locs.front().start.column, 0);
    const auto alias_view = tcgen_mma_i8_view(alias_instruction);
    ASSERT_TRUE(alias_view);
    EXPECT_EQ(alias_view->group,
              group == 1 ? TcgenCtaGroup::One : TcgenCtaGroup::Two);
  }
  EXPECT_FALSE(accepts_i8(
      i8_source("tcgen05.kind::i8.mma.cta_group::1 [%d], %ad, %bd, %i, %p;")));
  EXPECT_FALSE(
      accepts_i8(i8_source("tcgen05.mma.kind::i8.cta_group::1.kind::i8 "
                           "[%d], %ad, %bd, %i, %p;")));
}

/** Borrow selected roles and recheck actual owned payload after AST death. */
TEST(TcgenMmaI8, OwnedTruthMaskAndCorruptionRestoration) {
  for (const auto& [source, truth] : {
           std::pair{"0", false},
           std::pair{"2", true},
           std::pair{"-1", true},
           std::pair{"4294967296", true},
       }) {
    SCOPED_TRACE(source);
    std::optional<ResolvedModule> owned;
    {
      auto ast = parse_i8(
          i8_source(std::string("tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, "
                                "%bd, %i, ") +
                    source + ";"));
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result.has_value());
      owned = std::move(*result);
    }
    const auto* instruction = owned->functions.front().body[0].get();
    ASSERT_TRUE(instruction);
    const auto view = tcgen_mma_i8_view(*instruction);
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
    auto ast = parse_i8(
        i8_source("tcgen05.mma.cta_group::1.kind::i8 [%d], %ad, %bd, %i, "
                  "{%fm0, %fm1, %fm2, %fm3}, !%p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned = std::move(*result);
  }
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05MmaI8&>(instruction);
  ASSERT_TRUE(form.a_register);
  ASSERT_TRUE(form.disable_output_lane);
  const auto view = tcgen_mma_i8_view(instruction);
  ASSERT_TRUE(view);
  EXPECT_TRUE(std::get<ResolvedPredicate>(*view->enable_d).negated);
  ASSERT_TRUE(view->disable_output_lane);
  EXPECT_EQ(view->disable_output_lane->elements.size(), 4U);
  const auto context = i8_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(instruction.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  const auto a_ranges = form.a_register->locs;
  form.a_register->locs.clear();
  reject();
  form.a_register->locs = a_ranges;
  form.a_register->value.register_class = ResolvedRegisterClass::Predicate;
  reject();
  form.a_register->value.register_class = ResolvedRegisterClass::General;
  form.a_register->value.vector_width = 2;
  reject();
  form.a_register->value.vector_width.reset();
  form.b.value.declared_type = ScalarType::B32;
  reject();
  form.b.value.declared_type = ScalarType::B64;
  ASSERT_TRUE(form.b.value.symbol_id);
  form.b.value.declared_type.reset();
  reject();
  form.b.value.declared_type = ScalarType::B64;
  form.idesc.value.declared_type = ScalarType::F32;
  reject();
  form.idesc.value.declared_type = ScalarType::B32;
  form.idesc.value.vector_width = 2;
  reject();
  form.idesc.value.vector_width.reset();
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::B64;
  reject();
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::F32;
  auto& predicate = std::get<ResolvedPredicate>(form.enable_input_d.value);
  predicate.register_ref.register_class = ResolvedRegisterClass::General;
  reject();
  predicate.register_ref.register_class = ResolvedRegisterClass::Predicate;
  const auto predicate_ranges = form.enable_input_d.locs;
  form.enable_input_d.locs.clear();
  reject();
  form.enable_input_d.locs = predicate_ranges;
  const auto mask_ranges = form.disable_output_lane->locs;
  form.disable_output_lane->locs.pop_back();
  reject();
  form.disable_output_lane->locs = mask_ranges;
  const auto group_ranges = form.cta_group.locs;
  form.cta_group.locs.clear();
  reject();
  form.cta_group.locs = group_ranges;
  const auto original_layout = form.operand_layout.value;
  form.operand_layout.value = original_layout == 0 ? 1 : 0;
  reject();
  EXPECT_FALSE(tcgen_mma_i8_view(instruction));
  form.operand_layout.value = original_layout;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Field validators, saturation and operation facts remain distinct. */
TEST(TcgenMmaI8, LiteralDefinedFieldAndMixedPairRules) {
  constexpr uint32_t bits = 0x20U | (4U << 24) | (1U << 17);
  TcgenI8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{bits, TcgenMmaKind::I8},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K},
  };
  const auto unsaturated = check_tcgen_i8_known_operation(facts);
  EXPECT_TRUE(unsaturated.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(unsaturated.supplied_facts_ok());
  facts.instruction.bits |= 1U << 3;
  const auto saturated = check_tcgen_i8_known_operation(facts);
  EXPECT_TRUE(saturated.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(saturated.supplied_facts_ok());
  facts.instruction.bits |= 1U << 13;
  EXPECT_FALSE(check_tcgen_i8_known_operation(facts)
                   .instruction_fields.defined_fields_ok());
  facts.instruction.bits &= ~(1U << 13);
  facts.instruction.bits |= 1U << 14;
  EXPECT_FALSE(check_tcgen_i8_known_operation(facts)
                   .instruction_fields.defined_fields_ok());
  facts.instruction.bits &= ~(1U << 14);
  facts.instruction.bits |= 1U << 6;
  EXPECT_FALSE(check_tcgen_i8_known_operation(facts)
                   .instruction_fields.defined_fields_ok());
  facts.instruction.bits &= ~(1U << 6);
  for (uint32_t mixed_type : {1U << 7, 1U << 10}) {
    auto mixed = facts;
    mixed.instruction.bits |= mixed_type;
    const auto report = check_tcgen_i8_known_operation(mixed);
    EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
    EXPECT_FALSE(contains(report.violations, TcgenI8Violation::AType));
    EXPECT_FALSE(contains(report.violations, TcgenI8Violation::BType));
    EXPECT_TRUE(contains(report.missing, TcgenI8Obligation::MixedInputPair));
  }
  facts.instruction.bits |= (1U << 7) | (1U << 10);
  EXPECT_FALSE(contains(check_tcgen_i8_known_operation(facts).missing,
                        TcgenI8Obligation::MixedInputPair));
  facts.instruction.bits = bits | (1U << 2);
  const auto sparse = check_tcgen_i8_known_operation(facts);
  EXPECT_TRUE(sparse.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(sparse.violations, TcgenI8Violation::DenseSparse));
}

/** Table 55 rejects B transpose N8/24 separately from Table 42 shape. */
TEST(TcgenMmaI8, BTransposeShapeAndIndependentSwizzles) {
  constexpr uint32_t bits = 0x20U | (4U << 24);
  const TcgenSharedWord normal{1ULL << 46};
  const TcgenSharedWord atom32{(1ULL << 46) | (1ULL << 61)};
  TcgenI8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction =
          TcgenInstructionWord{bits | (2U << 17) | (1U << 15) | (1U << 16),
                               TcgenMmaKind::I8},
      .a_shared_word = normal,
      .b_shared_word = normal,
      .a_context = {.major = TcgenMajor::MN},
      .b_context = {.major = TcgenMajor::MN},
  };
  for (const auto& [n, valid] : {
           std::pair{8U, false},
           std::pair{16U, true},
           std::pair{24U, false},
           std::pair{32U, true},
           std::pair{48U, true},
       }) {
    SCOPED_TRACE(n);
    facts.instruction.bits =
        (facts.instruction.bits & ~(63U << 17)) | ((n >> 3) << 17);
    const auto report = check_tcgen_i8_known_operation(facts);
    EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
    EXPECT_FALSE(contains(report.violations, TcgenI8Violation::Shape));
    EXPECT_EQ(!contains(report.violations, TcgenI8Violation::BTransposeN),
              valid);
  }
  facts.a_shared_word = atom32;
  EXPECT_TRUE(contains(check_tcgen_i8_known_operation(facts).violations,
                       TcgenI8Violation::ASwizzle));
  facts.a_shared_word = normal;
  facts.b_shared_word = atom32;
  EXPECT_TRUE(contains(check_tcgen_i8_known_operation(facts).violations,
                       TcgenI8Violation::BSwizzle));
  facts.b_shared_word = normal;
  facts.a_in_tmem = true;
  facts.a_shared_word.reset();
  facts.instruction.bits &= ~((1U << 15) | (1U << 16));
  facts.instruction.bits = (facts.instruction.bits & ~(63U << 17)) | (1U << 17);
  const auto missing = check_tcgen_i8_known_operation(facts);
  EXPECT_FALSE(contains(missing.missing, TcgenI8Obligation::ASharedWord));
  EXPECT_TRUE(contains(missing.missing, TcgenI8Obligation::ALaneHalf));
  EXPECT_TRUE(contains(missing.missing, TcgenI8Obligation::DLaneHalf));
  facts.a_lane_half = 0;
  facts.d_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_i8_known_operation(facts).violations,
                       TcgenI8Violation::HalfAlignment));
}

/** Exact profiles and inherited descriptor contexts do not widen i8 MMA. */
TEST(TcgenMmaI8, ExactTargetsAndInvalidContexts) {
  TcgenI8KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{0x20U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::I8},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K},
  };
  for (const auto& [name, version, accepted] : {
           std::tuple{"sm_100a", checker::PtxVersion{8, 6}, true},
           std::tuple{"sm_110a", checker::PtxVersion{9, 0}, true},
           std::tuple{"sm_103a", checker::PtxVersion{8, 8}, false},
           std::tuple{"sm_100f", checker::PtxVersion{8, 8}, false},
           std::tuple{"sm_110f", checker::PtxVersion{9, 0}, false},
           std::tuple{"sm_100", checker::PtxVersion{9, 3}, false},
           std::tuple{"sm_120a", checker::PtxVersion{9, 3}, false},
       }) {
    SCOPED_TRACE(name);
    const auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    const auto report = check_tcgen_i8_known_operation(facts);
    EXPECT_EQ(!contains(report.violations, TcgenI8Violation::Target), accepted);
  }
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm110a = base::find_target_profile("sm_110a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm110a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{9, 3};
  facts.a_context.target = sm110a->identity;
  EXPECT_TRUE(contains(check_tcgen_i8_known_operation(facts).violations,
                       TcgenI8Violation::InvalidContext));
  facts.a_context.target = sm100a->identity;
  facts.b_context.ptx_version = checker::PtxVersion{9, 0};
  EXPECT_TRUE(contains(check_tcgen_i8_known_operation(facts).violations,
                       TcgenI8Violation::InvalidContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
