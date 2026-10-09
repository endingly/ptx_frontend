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

namespace ptx_frontend::resolved_ir {
namespace {

/** Place one canonical dense source in a target-qualified complete module. */
std::string f8f6f4_source(std::string_view body,
                          std::string_view target = "sm_100a",
                          std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %m<8>;
  .reg .b64 %ad, %bd;
  .reg .f32 %fm<8>;
  .reg .pred %p;
  .shared .align 8 .b64 barrier;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a source module with no assembler or GPU execution. */
std::optional<syntax_ast::AstModule> parse_f8f6f4(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Exercise the owned source and module checker path. */
bool accepts_f8f6f4(std::string_view source) {
  auto ast = parse_f8f6f4(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Build the explicitly declared context for direct corruption checks. */
checker::Context f8f6f4_context(const ResolvedModule& module) {
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

/** Search a typed report without equating missing facts with success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& values, T item) {
  return std::find(values.begin(), values.end(), item) != values.end();
}

/** Four layouts and two typed groups retain only canonical source order. */
TEST(TcgenMmaF8F6F4, CanonicalTopologiesAndSourceExclusions) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      for (bool mask : {false, true}) {
        const std::string lanes = group == 1 ? "{%m0, %m1, %m2, %m3}"
                                             : "{%m0, %m1, %m2, %m3, %m4, %m5, "
                                               "%m6, %m7}";
        const std::string instruction =
            "tcgen05.mma.cta_group::" + std::to_string(group) +
            ".kind::f8f6f4 [%d], " + (shared_a ? "%ad" : "[%a]") + ", %bd, %i" +
            (mask ? ", " + lanes : "") + ", %p;";
        SCOPED_TRACE(instruction);
        EXPECT_TRUE(accepts_f8f6f4(f8f6f4_source(instruction)));
      }
    }
  }
  auto group_two_ast = parse_f8f6f4(f8f6f4_source(
      "tcgen05.mma.cta_group::2.kind::f8f6f4 [%d], %ad, %bd, %i, %p;"));
  ASSERT_TRUE(group_two_ast);
  auto group_two_owned = resolveAndValidateModule(*group_two_ast);
  ASSERT_TRUE(group_two_owned.has_value());
  const auto& group_two_instruction =
      *group_two_owned->functions.front().body[0].get();
  const auto* group_two_form =
      dynamic_cast<const Tcgen05MmaF8f6f4*>(&group_two_instruction);
  ASSERT_TRUE(group_two_form);
  ASSERT_EQ(group_two_form->cta_group.locs.size(), 1U);
  EXPECT_GT(group_two_form->cta_group.locs.front().start.column, 0);
  const auto group_two_view = tcgen_mma_f8f6f4_view(group_two_instruction);
  ASSERT_TRUE(group_two_view);
  EXPECT_EQ(group_two_view->group, TcgenCtaGroup::Two);
  for (std::string_view bad : {
           "tcgen05.mma.kind::f8f6f4.cta_group::1 [%d], %ad, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, %i, "
           "{}, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, %i, %p, "
           "0;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4.satfinite "
           "[%d], %ad, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4.block_scale "
           "[%d], %ad, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], 0, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, 0, %p;",
           "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, %i, "
           "{%m0, %m1, %m2}, %p;",
       }) {
    SCOPED_TRACE(bad);
    EXPECT_FALSE(accepts_f8f6f4(f8f6f4_source(bad)));
  }
}

/** Borrow source roles after AST death and reject corrupted owned metadata. */
TEST(TcgenMmaF8F6F4, OwnedTruthMaskAndRestoration) {
  for (const auto& [text, truth] : {
           std::pair{"0", false},
           std::pair{"2", true},
           std::pair{"-1", true},
           std::pair{"4294967296", true},
       }) {
    SCOPED_TRACE(text);
    std::optional<ResolvedModule> owned;
    {
      auto ast = parse_f8f6f4(
          f8f6f4_source(std::string("tcgen05.mma.cta_group::1.kind::f8f6f4 "
                                    "[%d], %ad, %bd, %i, ") +
                        text + ";"));
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result.has_value());
      owned = std::move(*result);
    }
    const auto* instruction = owned->functions.front().body[0].get();
    ASSERT_TRUE(instruction);
    const auto view = tcgen_mma_f8f6f4_view(*instruction);
    ASSERT_TRUE(view);
    ASSERT_TRUE(
        std::holds_alternative<ResolvedPredicateConstant>(*view->enable_d));
    EXPECT_EQ(std::get<ResolvedPredicateConstant>(*view->enable_d).value,
              truth);
    EXPECT_TRUE(view->a_shared);
    EXPECT_EQ(view->a_shared->role, MatrixFragmentRole::A);
    EXPECT_EQ(view->b.role, MatrixFragmentRole::B);
    EXPECT_FALSE(view->disable_output_lane);
    EXPECT_TRUE(validateModule(*owned).has_value());
  }
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_f8f6f4(f8f6f4_source(
        "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, %i, "
        "{%fm0, %fm1, %fm2, %fm3}, !%p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned = std::move(*result);
  }
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05MmaF8f6f4&>(instruction);
  ASSERT_TRUE(form.a_register);
  ASSERT_TRUE(form.disable_output_lane);
  const auto view = tcgen_mma_f8f6f4_view(instruction);
  ASSERT_TRUE(view);
  EXPECT_TRUE(std::get<ResolvedPredicate>(*view->enable_d).negated);
  ASSERT_TRUE(view->disable_output_lane);
  EXPECT_EQ(view->disable_output_lane->elements.size(), 4U);
  const auto context = f8f6f4_context(*owned);
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
  const auto group_ranges = form.cta_group.locs;
  form.cta_group.locs.clear();
  reject();
  form.cta_group.locs = group_ranges;
  const auto original_layout = form.operand_layout.value;
  form.operand_layout.value = original_layout == 0 ? 1 : 0;
  reject();
  EXPECT_FALSE(tcgen_mma_f8f6f4_view(instruction));
  form.operand_layout.value = original_layout;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Table 42 rows cover group-one multiples of eight and typed input pairs. */
TEST(TcgenMmaF8F6F4, ShapeRowsAndPairDomain) {
  const auto rows = tcgen_f8f6f4_shape_rows();
  EXPECT_EQ(rows.size(), 4U);
  EXPECT_EQ(tcgen_f8f6f4_path_rows().size(), 4U);
  TcgenF8F6F4KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{0x10U | (4U << 24) | (1U << 17),
                                          TcgenMmaKind::F8F6F4},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K},
  };
  for (unsigned n : {8u, 16u, 24u, 32u, 256u}) {
    SCOPED_TRACE(n);
    facts.instruction.bits =
        (facts.instruction.bits & ~(63U << 17)) | ((n >> 3) << 17);
    const auto report = check_tcgen_f8f6f4_known_operation(facts);
    EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
    EXPECT_FALSE(contains(report.violations, TcgenF8F6F4Violation::Shape));
  }
  facts.instruction.bits = (facts.instruction.bits & ~(63U << 17)) | (1U << 17);
  for (unsigned a_code : {0u, 1u, 3u, 4u, 5u}) {
    for (unsigned b_code : {0u, 1u, 3u, 4u, 5u}) {
      auto pair = facts;
      pair.instruction.bits =
          (pair.instruction.bits & ~((7U << 7) | (7U << 10))) | (a_code << 7) |
          (b_code << 10);
      const auto report = check_tcgen_f8f6f4_known_operation(pair);
      EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
      EXPECT_FALSE(contains(report.violations, TcgenF8F6F4Violation::AType));
      EXPECT_FALSE(contains(report.violations, TcgenF8F6F4Violation::BType));
      EXPECT_EQ(
          contains(report.missing, TcgenF8F6F4Obligation::ALowBitPackingRule),
          a_code == 3 || a_code == 4 || a_code == 5);
      EXPECT_EQ(
          contains(report.missing, TcgenF8F6F4Obligation::BLowBitPackingRule),
          b_code == 3 || b_code == 4 || b_code == 5);
    }
  }
  facts.instruction.bits &= ~(1U << 4);
  EXPECT_TRUE(check_tcgen_f8f6f4_known_operation(facts)
                  .instruction_fields.defined_fields_ok());
  facts.instruction.bits |= 1U << 4;
  facts.instruction.bits |= 1U << 3;
  EXPECT_FALSE(check_tcgen_f8f6f4_known_operation(facts)
                   .instruction_fields.defined_fields_ok());
  facts.instruction.bits &= ~(1U << 3);
  facts.instruction.bits |= 1U << 2;
  const auto sparse = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_TRUE(sparse.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(sparse.violations, TcgenF8F6F4Violation::DenseSparse));
}

/** Role widths control low-bit layout and B-only N restrictions. */
TEST(TcgenMmaF8F6F4, IndependentTransposePackingAndContexts) {
  const TcgenSharedWord normal{1ULL << 46};
  const TcgenSharedWord atom32{(1ULL << 46) | (1ULL << 61)};
  TcgenF8F6F4KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{0x10U | (4U << 24) | (2U << 17) |
                                              (1U << 15) | (1U << 16),
                                          TcgenMmaKind::F8F6F4},
      .a_shared_word = normal,
      .b_shared_word = normal,
      .a_context = {.major = TcgenMajor::MN},
      .b_context = {.major = TcgenMajor::MN},
  };
  facts.instruction.bits |= 5U << 7;
  auto report = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_TRUE(
      contains(report.missing, TcgenF8F6F4Obligation::ATransposeLayoutRule));
  EXPECT_FALSE(
      contains(report.missing, TcgenF8F6F4Obligation::BTransposeLayoutRule));
  facts.a_shared_word = atom32;
  report = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_TRUE(contains(report.violations, TcgenF8F6F4Violation::ASwizzle));
  facts.a_shared_word = normal;
  facts.instruction.bits |= 3U << 10;
  report = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_TRUE(
      contains(report.missing, TcgenF8F6F4Obligation::BTransposeLayoutRule));
  EXPECT_FALSE(contains(report.violations, TcgenF8F6F4Violation::BTransposeN));
  facts.b_shared_word = atom32;
  EXPECT_TRUE(contains(check_tcgen_f8f6f4_known_operation(facts).violations,
                       TcgenF8F6F4Violation::BSwizzle));
  facts.b_shared_word = normal;
  facts.instruction.bits &= ~(7U << 10);
  facts.instruction.bits = (facts.instruction.bits & ~(63U << 17)) | (1U << 17);
  report = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_TRUE(contains(report.violations, TcgenF8F6F4Violation::BTransposeN));
  facts.a_in_tmem = true;
  facts.a_shared_word.reset();
  report = check_tcgen_f8f6f4_known_operation(facts);
  EXPECT_FALSE(contains(report.missing, TcgenF8F6F4Obligation::ASharedWord));
  EXPECT_TRUE(
      contains(report.missing, TcgenF8F6F4Obligation::ALowBitPackingRule));
  facts.instruction.bits &= ~((1U << 15) | (1U << 16));
  facts.a_lane_half = 0;
  facts.d_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_f8f6f4_known_operation(facts).violations,
                       TcgenF8F6F4Violation::HalfAlignment));
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm110a = base::find_target_profile("sm_110a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm110a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{9, 3};
  facts.b_context.target = sm110a->identity;
  EXPECT_TRUE(contains(check_tcgen_f8f6f4_known_operation(facts).violations,
                       TcgenF8F6F4Violation::InvalidContext));
}

/** Current target identities use the ordinary four unscaled branches. */
TEST(TcgenMmaF8F6F4, SourceAndKnownTargetIntersections) {
  const std::string source =
      "tcgen05.mma.cta_group::1.kind::f8f6f4 [%d], %ad, %bd, %i, %p;";
  for (const auto& [target, version, accepted] : {
           std::tuple{"sm_100a", "8.5", false},
           std::tuple{"sm_100a", "8.6", true},
           std::tuple{"sm_100f", "8.7", false},
           std::tuple{"sm_100f", "8.8", true},
           std::tuple{"sm_110a", "8.8", false},
           std::tuple{"sm_110a", "9.0", true},
           std::tuple{"sm_110f", "9.0", true},
           std::tuple{"sm_103a", "8.8", true},
           std::tuple{"sm_103f", "8.8", true},
           std::tuple{"sm_100", "9.3", false},
       }) {
    SCOPED_TRACE(std::string(target) + "/" + version);
    EXPECT_EQ(accepts_f8f6f4(f8f6f4_source(source, target, version)), accepted);
  }
  TcgenF8F6F4KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction = TcgenInstructionWord{0x10U | (4U << 24) | (2U << 17),
                                          TcgenMmaKind::F8F6F4},
      .a_shared_word = TcgenSharedWord{1ULL << 46},
      .b_shared_word = TcgenSharedWord{1ULL << 46},
      .a_context = {.major = TcgenMajor::K},
      .b_context = {.major = TcgenMajor::K},
  };
  for (const auto& [target, version, accepted] : {
           std::tuple{"sm_100a", checker::PtxVersion{8, 6}, true},
           std::tuple{"sm_100f", checker::PtxVersion{8, 8}, true},
           std::tuple{"sm_110a", checker::PtxVersion{9, 0}, true},
           std::tuple{"sm_110f", checker::PtxVersion{9, 0}, true},
           std::tuple{"sm_103a", checker::PtxVersion{8, 8}, true},
           std::tuple{"sm_103f", checker::PtxVersion{8, 8}, true},
           std::tuple{"sm_100", checker::PtxVersion{9, 3}, false},
       }) {
    SCOPED_TRACE(target);
    const auto profile = base::find_target_profile(target);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    const auto report = check_tcgen_f8f6f4_known_operation(facts);
    EXPECT_EQ(!contains(report.violations, TcgenF8F6F4Violation::Target),
              accepted);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
