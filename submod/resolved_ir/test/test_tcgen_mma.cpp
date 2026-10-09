#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Assemble a complete source body with declared carrier identities. */
std::string mma_source(std::string_view body,
                       std::string_view target = "sm_100a",
                       std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %m<8>;
  .reg .b64 %ad, %bd;
  .reg .u64 %ua, %ub;
  .reg .s64 %sa, %sb;
  .reg .f64 %fa;
  .reg .u32 %ui;
  .reg .s32 %si;
  .reg .f32 %fi, %fm<8>;
  .reg .pred %p;
  .shared .align 8 .b64 barrier;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a complete MMA module without holding AST storage in owned tests. */
std::optional<syntax_ast::AstModule> parse_mma(std::string_view source,
                                               bool report = true) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    if (report)
      ADD_FAILURE() << (ast && !ast.diagnostics.empty()
                            ? ast.diagnostics.front().message
                            : "TCGEN MMA module did not parse.");
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Resolve and validate the body under the module's written target. */
bool accepts_mma(std::string_view source) {
  auto ast = parse_mma(source, false);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Supply the direct checker with the same qualified source target. */
checker::Context mma_context(const ResolvedModule& module) {
  const auto profile = base::find_target_profile("sm_100a");
  EXPECT_TRUE(profile.has_value());
  return checker::Context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = module.functions.front().instruction_ranges[0],
  };
}

/** Cover both groups, both A locations and both optional source controls. */
TEST(TcgenMmaF16, SourceTopologyAndOneWrittenAlias) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared_a : {false, true}) {
      for (bool mask : {false, true}) {
        for (bool scale : {false, true}) {
          const std::string a = shared_a ? "%ad" : "[%a]";
          const std::string lanes =
              group == 1 ? "{%m0, %m1, %m2, %m3}"
                         : "{%m0, %m1, %m2, %m3, %m4, %m5, %m6, %m7}";
          const std::string body =
              "tcgen05.mma.cta_group::" + std::to_string(group) +
              ".kind::f16 [%d], " + a + ", %bd, %i" +
              (mask ? ", " + lanes : "") + ", %p" + (scale ? ", 15" : "") + ";";
          SCOPED_TRACE(body);
          EXPECT_TRUE(accepts_mma(mma_source(body)));
        }
      }
    }
  }
  EXPECT_TRUE(accepts_mma(mma_source(
      "tcgen05.mma.kind::f16.cta_group::1 [%d], %ad, %bd, %i, !%p;")));
  EXPECT_TRUE(accepts_mma(
      mma_source("tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, 2;")));
  EXPECT_TRUE(accepts_mma(
      mma_source("tcgen05.mma.cta_group::1.kind::f16 [%d], %ua, %ub, %ui, "
                 "{%fm0, %fm1, %fm2, %fm3}, 0;")));
  EXPECT_TRUE(accepts_mma(mma_source(
      "tcgen05.mma.cta_group::1.kind::f16 [%d], %sa, %sb, %si, 1;")));
}

/** Preserve integer truth after conversion and release of syntax storage. */
TEST(TcgenMmaF16, OwnedIntegerPredicateTruth) {
  for (const auto& [source, truth] : {
           std::pair{"0", false},
           std::pair{"1", true},
           std::pair{"2", true},
           std::pair{"-1", true},
           std::pair{"4294967296", true},
       }) {
    SCOPED_TRACE(source);
    std::optional<ResolvedModule> owned;
    {
      auto ast = parse_mma(mma_source(
          std::string("tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, "
                      "%i, ") +
          source + ";"));
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result.has_value()) << result.error().front().message;
      owned = std::move(*result);
    }
    const auto& instruction = *owned->functions.front().body[0].get();
    const auto view = tcgen_mma_f16_view(instruction);
    ASSERT_TRUE(view);
    ASSERT_TRUE(
        std::holds_alternative<ResolvedPredicateConstant>(*view->enable_d));
    EXPECT_EQ(std::get<ResolvedPredicateConstant>(*view->enable_d).value,
              truth);
    EXPECT_TRUE(instruction.check(mma_context(*owned)).has_value());
    EXPECT_TRUE(validateModule(*owned).has_value());
  }
}

/** Carrier, predicate, mask, scale and source-grammar negatives stay closed. */
TEST(TcgenMmaF16, RejectsMalformedSourcesAndTargets) {
  for (std::string_view instruction : {
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %fa, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], 0, %bd, %i, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %fi, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, 0, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %ui;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, {%m0, %m1, "
           "%m2}, %p;",
           "tcgen05.mma.cta_group::2.kind::f16 [%d], %ad, %bd, %i, {%m0, %m1, "
           "%m2, %m3}, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, {}, %p;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p, -1;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p, 16;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p, "
           "4294967296;",
           "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p, %ui;",
           "tcgen05.mma.kind::f16.cta_group::1.kind::f16 [%d], %ad, %bd, %i, "
           "%p;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_mma(mma_source(instruction)));
  }
  const std::string plain =
      "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p;";
  const std::string scaled =
      "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p, 0;";
  for (const auto& [target, version, plain_ok, scaled_ok] : {
           std::tuple{"sm_100a", "8.6", true, true},
           std::tuple{"sm_100f", "8.7", false, false},
           std::tuple{"sm_100f", "8.8", true, true},
           std::tuple{"sm_110a", "9.0", true, false},
           std::tuple{"sm_110f", "9.0", true, false},
           std::tuple{"sm_100", "9.3", false, false},
           std::tuple{"sm_120a", "9.3", false, false},
       }) {
    SCOPED_TRACE(std::string(target) + "/" + version);
    EXPECT_EQ(accepts_mma(mma_source(plain, target, version)), plain_ok);
    EXPECT_EQ(accepts_mma(mma_source(scaled, target, version)), scaled_ok);
  }
}

/** Corrupt actual owned payloads after the syntax tree has been destroyed. */
TEST(TcgenMmaF16, RechecksOwnedRolesAndRestoration) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_mma(
        mma_source("tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, "
                   "{%m0, %m1, %m2, %m3}, !%p, 15;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned = std::move(*result);
  }
  ASSERT_TRUE(owned);
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05MmaF16&>(instruction);
  ASSERT_TRUE(form.a_register);
  ASSERT_TRUE(form.disable_output_lane);
  ASSERT_TRUE(form.scale_input_d);
  const auto context = mma_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(instruction.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  ASSERT_TRUE(tcgen_mma_f16_view(instruction));
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->a_shared->source,
            &form.a_register->value);
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->instruction.source,
            &form.idesc.value);
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->disable_output_lane,
            &form.disable_output_lane->value);
  ASSERT_TRUE(std::holds_alternative<ResolvedPredicate>(
      *tcgen_mma_f16_view(instruction)->enable_d));
  EXPECT_TRUE(
      std::get<ResolvedPredicate>(*tcgen_mma_f16_view(instruction)->enable_d)
          .negated);
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  form.a_register->value.register_class = ResolvedRegisterClass::Predicate;
  reject();
  form.a_register->value.register_class = ResolvedRegisterClass::General;
  form.a_register->value.vector_width = 2;
  reject();
  form.a_register->value.vector_width.reset();
  const auto a_type = form.a_register->value.declared_type;
  form.a_register->value.declared_type = ScalarType::F64;
  reject();
  form.a_register->value.declared_type.reset();
  reject();
  form.a_register->value.declared_type = a_type;
  form.b.value.declared_type = ScalarType::B32;
  reject();
  form.b.value.declared_type = ScalarType::B64;
  form.idesc.value.declared_type = ScalarType::F32;
  reject();
  form.idesc.value.declared_type = ScalarType::B32;
  const auto last_lane = form.disable_output_lane->value.elements.back();
  form.disable_output_lane->value.elements.pop_back();
  reject();
  form.disable_output_lane->value.elements.push_back(last_lane);
  form.disable_output_lane->value.elements[0]->register_class =
      ResolvedRegisterClass::Predicate;
  reject();
  form.disable_output_lane->value.elements[0]->register_class =
      ResolvedRegisterClass::General;
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::B64;
  reject();
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::B32;
  ASSERT_TRUE(form.disable_output_lane->value.elements[0]->symbol_id);
  form.disable_output_lane->value.elements[0]->declared_type.reset();
  reject();
  form.disable_output_lane->value.elements[0]->declared_type = ScalarType::B32;
  auto& predicate = std::get<ResolvedPredicate>(form.enable_input_d.value);
  predicate.register_ref.register_class = ResolvedRegisterClass::General;
  reject();
  predicate.register_ref.register_class = ResolvedRegisterClass::Predicate;
  predicate.register_ref.vector_width = 2;
  reject();
  predicate.register_ref.vector_width.reset();
  predicate.register_ref.declared_type = ScalarType::B32;
  reject();
  predicate.register_ref.declared_type = ScalarType::Pred;
  ASSERT_TRUE(predicate.register_ref.symbol_id);
  predicate.register_ref.declared_type.reset();
  reject();
  predicate.register_ref.declared_type = ScalarType::Pred;
  form.scale_input_d->value.bits = 16;
  reject();
  form.scale_input_d->value.bits = 15;
  form.scale_input_d->value.integer_source_bits = 1ull << 32;
  reject();
  form.scale_input_d->value.integer_source_bits = 15;
  form.scale_input_d->value.integer_source_bits.reset();
  reject();
  form.scale_input_d->value.integer_source_bits = 15;
  form.scale_input_d->value.bits = 14;
  reject();
  form.scale_input_d->value.bits = 15;
  ASSERT_EQ(form.cta_group.locs.size(), 1U);
  const auto group_range = form.cta_group.locs.front();
  form.cta_group.locs.clear();
  reject();
  form.cta_group.locs.push_back(group_range);
  ASSERT_EQ(form.enable_input_d.locs.size(), 1U);
  const auto predicate_range = form.enable_input_d.locs.front();
  form.enable_input_d.locs.front().start.line = 0;
  reject();
  form.enable_input_d.locs.front() = predicate_range;
  ASSERT_EQ(form.disable_output_lane->locs.size(), 4U);
  const auto mask_range = form.disable_output_lane->locs.back();
  form.disable_output_lane->locs.pop_back();
  reject();
  form.disable_output_lane->locs.push_back(mask_range);
  ASSERT_EQ(form.scale_input_d->locs.size(), 1U);
  const auto scale_range = form.scale_input_d->locs.front();
  form.scale_input_d->locs.front().end.line = 0;
  reject();
  form.scale_input_d->locs.front() = scale_range;
  ASSERT_EQ(form.d.locs.size(), 1U);
  const auto d_range = form.d.locs.front();
  form.d.locs.clear();
  reject();
  form.d.locs.push_back(d_range);
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  form.d.value.bracketed = false;
  reject();
  form.d.value.bracketed = true;
  auto& d_register = std::get<ResolvedRegisterRef>(form.d.value.value);
  d_register.register_class = ResolvedRegisterClass::Predicate;
  reject();
  d_register.register_class = ResolvedRegisterClass::General;
  d_register.vector_width = 2;
  reject();
  d_register.vector_width.reset();
  d_register.declared_type = ScalarType::F32;
  reject();
  d_register.declared_type = ScalarType::B32;
  const auto saved_predicate = form.enable_input_d.value;
  form.enable_input_d.value = ResolvedPredicateConstant{true};
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  form.enable_input_d.value = saved_predicate;
  form.scale_input_d->value.is_negative = true;
  reject();
  form.scale_input_d->value.is_negative = false;
  form.scale_input_d->value.type = ScalarType::F32;
  reject();
  form.scale_input_d->value.type = ScalarType::U32;
  form.operand_layout.value = 0;
  reject();
  EXPECT_FALSE(tcgen_mma_f16_view(instruction));
  form.operand_layout.value = 3;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** MMA joins TCGEN-only per-body group checks and arrive-one eligibility. */
TEST(TcgenMmaF16, GroupParticipationAndBodyReset) {
  const std::string mma_one =
      "tcgen05.mma.cta_group::1.kind::f16 [%d], %ad, %bd, %i, %p;";
  const std::string mma_two =
      "tcgen05.mma.cta_group::2.kind::f16 [%d], %ad, %bd, %i, %p;";
  EXPECT_FALSE(accepts_mma(
      mma_source(mma_one + "\n  tcgen05.cp.cta_group::2.128x256b [%d], %ad;")));
  EXPECT_FALSE(accepts_mma(
      mma_source(mma_one + "\n  tcgen05.alloc.cta_group::2.sync.aligned.b32 "
                           "[%d], 32;")));
  EXPECT_TRUE(accepts_mma(
      mma_source(mma_one +
                 "\n  tcgen05.cp.cta_group::1.128x256b [%d], %ad;\n"
                 "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
                 "[barrier];\n  tcgen05.fence::after_thread_sync;")));
  std::string separate = mma_source(mma_one);
  separate += R"ptx(
.visible .entry other() {
  .reg .b32 %d, %i;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + mma_two +
              "\n  ret;\n}\n";
  EXPECT_TRUE(accepts_mma(separate));
  EXPECT_EQ(Tcgen05MmaF16::completion_kind,
            base::AsyncCompletionKind::TcgenMbarrierArriveOne);
}

/** Check literal Table 42 rows through their future installed generated API. */
TEST(TcgenMmaOperations, DenseF16ShapeRows) {
  const auto rows = tcgen_f16_shape_rows();
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].group, TcgenCtaGroup::One);
  EXPECT_EQ(rows[0].d_type, MatrixElementType::F16);
  EXPECT_TRUE(tcgen_f16_row_contains(rows[0], 64, 8, 16));
  EXPECT_TRUE(tcgen_f16_row_contains(rows[0], 128, 256, 16));
  EXPECT_FALSE(tcgen_f16_row_contains(rows[0], 64, 24, 32));
  EXPECT_FALSE(tcgen_f16_row_contains(rows[0], 64, 9, 16));
  EXPECT_FALSE(tcgen_f16_row_contains(rows[0], 256, 8, 16));
  EXPECT_EQ(rows[2].group, TcgenCtaGroup::Two);
  EXPECT_TRUE(tcgen_f16_row_contains(rows[2], 128, 16, 16));
  EXPECT_TRUE(tcgen_f16_row_contains(rows[2], 256, 256, 16));
  EXPECT_FALSE(tcgen_f16_row_contains(rows[2], 128, 8, 16));
}

/** Check the separate datapath source and its half-path identity. */
TEST(TcgenMmaOperations, DenseF16Paths) {
  const auto rows = tcgen_f16_path_rows();
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].layout, 'F');
  EXPECT_TRUE(rows[0].half_path);
  EXPECT_EQ(rows[1].layout, 'D');
  EXPECT_FALSE(rows[1].half_path);
  EXPECT_EQ(rows[2].layout, 'B');
  EXPECT_EQ(rows[3].layout, 'A');
}

/** Keep known field failures, operation findings and missing context distinct. */
TEST(TcgenMmaOperations, KnownWordReport) {
  const TcgenF16KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scaled_d = false,
      .instruction =
          TcgenInstructionWord{(4U << 24) | (1U << 17), TcgenMmaKind::F16},
  };
  const auto report = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(report.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(report.supplied_facts_ok());
  ASSERT_TRUE(report.path_layout);
  EXPECT_EQ(*report.path_layout, 'F');
  EXPECT_NE(std::find(report.missing.begin(), report.missing.end(),
                      TcgenF16Obligation::BSharedWord),
            report.missing.end());
  EXPECT_NE(std::find(report.missing.begin(), report.missing.end(),
                      TcgenF16Obligation::Target),
            report.missing.end());
  auto invalid = facts;
  invalid.instruction.kind = static_cast<TcgenMmaKind>(255);
  const auto bad = check_tcgen_f16_known_operation(invalid);
  EXPECT_NE(std::find(bad.violations.begin(), bad.violations.end(),
                      TcgenF16Violation::Kind),
            bad.violations.end());
}

/** Keep field validity, operational shape, and input-pair rules separate. */
TEST(TcgenMmaOperations, KnownWordFieldAndTypeRules) {
  const TcgenF16KnownFacts base{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(4U << 24) | (1U << 17), TcgenMmaKind::F16},
  };
  const auto contains = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  auto wrong_shape = base;
  wrong_shape.group = TcgenCtaGroup::Two;
  const auto shape = check_tcgen_f16_known_operation(wrong_shape);
  EXPECT_TRUE(shape.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(shape.checked, TcgenF16Checked::Shape));
  EXPECT_TRUE(contains(shape.violations, TcgenF16Violation::Shape));
  auto bad_field = base;
  bad_field.instruction.bits |= 1U << 6;
  const auto field = check_tcgen_f16_known_operation(bad_field);
  EXPECT_FALSE(field.instruction_fields.defined_fields_ok());
  EXPECT_FALSE(contains(field.violations, TcgenF16Violation::Shape));
  auto sparse = base;
  sparse.instruction.bits |= 1U << 2;
  const auto dense = check_tcgen_f16_known_operation(sparse);
  EXPECT_TRUE(dense.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(dense.violations, TcgenF16Violation::DenseSparse));
  auto wrong_input = base;
  wrong_input.instruction.bits |= 1U << 7;
  const auto input = check_tcgen_f16_known_operation(wrong_input);
  EXPECT_TRUE(input.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(input.checked, TcgenF16Checked::Types));
  EXPECT_TRUE(contains(input.violations, TcgenF16Violation::AType));
  auto mixed = base;
  mixed.instruction.bits |= (1U << 4) | (1U << 10);
  const auto pair = check_tcgen_f16_known_operation(mixed);
  EXPECT_TRUE(pair.instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(pair.missing, TcgenF16Obligation::MixedInputPair));
  EXPECT_FALSE(contains(pair.violations, TcgenF16Violation::AType));
  EXPECT_FALSE(contains(pair.violations, TcgenF16Violation::BType));
  mixed.instruction.bits |= 1U << 7;
  const auto equal = check_tcgen_f16_known_operation(mixed);
  EXPECT_FALSE(contains(equal.missing, TcgenF16Obligation::MixedInputPair));
}

/** Exercise known A/B shared roles independently of their opaque registers. */
TEST(TcgenMmaOperations, KnownSharedRolesAndContext) {
  const uint32_t base_bits = (4U << 24) | (1U << 17);
  const TcgenSharedWord normal{1ULL << 46};
  const TcgenSharedWord atom32{(1ULL << 46) | (1ULL << 61)};
  TcgenF16KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = false,
      .instruction =
          TcgenInstructionWord{base_bits | (1U << 15), TcgenMmaKind::F16},
      .a_shared_word = atom32,
      .b_shared_word = normal,
      .a_context = {.major = TcgenMajor::MN},
      .b_context = {.major = TcgenMajor::K},
  };
  const auto contains = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  const auto a = check_tcgen_f16_known_operation(facts);
  ASSERT_TRUE(a.a_shared_fields);
  ASSERT_TRUE(a.b_shared_fields);
  EXPECT_TRUE(a.a_shared_fields->defined_fields_ok());
  EXPECT_TRUE(a.b_shared_fields->defined_fields_ok());
  EXPECT_TRUE(contains(a.checked, TcgenF16Checked::AMajor));
  EXPECT_TRUE(contains(a.checked, TcgenF16Checked::BSwizzle));
  EXPECT_TRUE(contains(a.violations, TcgenF16Violation::ASwizzle));
  EXPECT_FALSE(contains(a.violations, TcgenF16Violation::BSwizzle));
  facts.instruction.bits = base_bits | (1U << 16);
  facts.a_shared_word = normal;
  facts.b_shared_word = atom32;
  facts.a_context.major = TcgenMajor::K;
  facts.b_context.major = TcgenMajor::MN;
  const auto b = check_tcgen_f16_known_operation(facts);
  EXPECT_FALSE(contains(b.violations, TcgenF16Violation::ASwizzle));
  EXPECT_TRUE(contains(b.violations, TcgenF16Violation::BSwizzle));
  facts.b_context.major = TcgenMajor::K;
  const auto major = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(major.violations, TcgenF16Violation::BMajor));
  facts.a_in_tmem = true;
  const auto misplaced = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(
      contains(misplaced.violations, TcgenF16Violation::APlacementFacts));
  EXPECT_FALSE(misplaced.a_shared_fields.has_value());
  facts.a_shared_word.reset();
  const auto tensor = check_tcgen_f16_known_operation(facts);
  EXPECT_FALSE(contains(tensor.missing, TcgenF16Obligation::ASharedWord));

  const auto profile = base::find_target_profile("sm_100a");
  const auto other = base::find_target_profile("sm_100f");
  ASSERT_TRUE(profile);
  ASSERT_TRUE(other);
  facts.target = profile->identity;
  facts.ptx_version = checker::PtxVersion{9, 3};
  facts.a_context = {};
  facts.b_context = {.major = TcgenMajor::K};
  const auto inherited = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(inherited.checked, TcgenF16Checked::Target));
  EXPECT_FALSE(
      contains(inherited.violations, TcgenF16Violation::InvalidContext));
  facts.b_context.target = other->identity;
  const auto conflicting = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(
      contains(conflicting.violations, TcgenF16Violation::InvalidContext));
  facts.b_context.target = profile->identity;
  facts.b_context.ptx_version = checker::PtxVersion{8, 6};
  const auto version = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(version.violations, TcgenF16Violation::InvalidContext));
}

/** Preserve missing, invalid and disagreeing half-lane facts separately. */
TEST(TcgenMmaOperations, KnownHalfPathFacts) {
  TcgenF16KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(4U << 24) | (1U << 17), TcgenMmaKind::F16},
  };
  const auto contains = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  const auto missing = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(missing.missing, TcgenF16Obligation::ALaneHalf));
  EXPECT_TRUE(contains(missing.missing, TcgenF16Obligation::DLaneHalf));
  facts.a_lane_half = 0;
  facts.d_lane_half = 16;
  const auto mismatch = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(mismatch.checked, TcgenF16Checked::HalfAlignment));
  EXPECT_TRUE(contains(mismatch.violations, TcgenF16Violation::HalfAlignment));
  facts.a_lane_half = 17;
  facts.d_lane_half = 9;
  const auto invalid = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(invalid.violations, TcgenF16Violation::ALaneHalf));
  EXPECT_TRUE(contains(invalid.violations, TcgenF16Violation::DLaneHalf));
  EXPECT_FALSE(contains(invalid.checked, TcgenF16Checked::HalfAlignment));
}

/** Full-path lane rules apply to D even when A is sourced from shared memory. */
TEST(TcgenMmaOperations, FullPathLaneFacts) {
  TcgenF16KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(8U << 24) | (2U << 17), TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
  };
  const auto contains = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  const auto aligned = check_tcgen_f16_known_operation(facts);
  EXPECT_EQ(aligned.path_layout, 'D');
  EXPECT_FALSE(contains(aligned.violations, TcgenF16Violation::ALaneHalf));
  facts.a_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_f16_known_operation(facts).violations,
                       TcgenF16Violation::ALaneHalf));
  facts.a_lane_half.reset();
  EXPECT_TRUE(contains(check_tcgen_f16_known_operation(facts).missing,
                       TcgenF16Obligation::ALaneHalf));
  facts.a_in_tmem = false;
  facts.d_lane_half = 16;
  const auto shared = check_tcgen_f16_known_operation(facts);
  EXPECT_TRUE(contains(shared.violations, TcgenF16Violation::DLaneHalf));
  EXPECT_FALSE(contains(shared.missing, TcgenF16Obligation::ALaneHalf));
  facts.d_lane_half.reset();
  EXPECT_TRUE(contains(check_tcgen_f16_known_operation(facts).missing,
                       TcgenF16Obligation::DLaneHalf));
}

/** Match known-word target checks to exact, inherited and scaled gates. */
TEST(TcgenMmaOperations, KnownTargetIntersections) {
  TcgenF16KnownFacts facts{
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(4U << 24) | (1U << 17), TcgenMmaKind::F16},
  };
  const auto contains = [](const auto& values, const auto value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  };
  for (const auto& [name, version, scaled, accepted] : {
           std::tuple{"sm_100a", checker::PtxVersion{8, 6}, false, true},
           std::tuple{"sm_100a", checker::PtxVersion{8, 6}, true, true},
           std::tuple{"sm_100f", checker::PtxVersion{8, 8}, true, true},
           std::tuple{"sm_103a", checker::PtxVersion{8, 8}, false, true},
           std::tuple{"sm_103f", checker::PtxVersion{8, 8}, true, true},
           std::tuple{"sm_110a", checker::PtxVersion{9, 0}, false, true},
           std::tuple{"sm_110f", checker::PtxVersion{9, 0}, false, true},
           std::tuple{"sm_110a", checker::PtxVersion{9, 0}, true, false},
           std::tuple{"sm_110f", checker::PtxVersion{9, 0}, true, false},
           std::tuple{"sm_100", checker::PtxVersion{9, 3}, false, false},
           std::tuple{"sm_110", checker::PtxVersion{9, 3}, false, false},
           std::tuple{"sm_120a", checker::PtxVersion{9, 3}, false, false},
       }) {
    SCOPED_TRACE(name);
    const auto profile = base::find_target_profile(name);
    ASSERT_TRUE(profile);
    facts.target = profile->identity;
    facts.ptx_version = version;
    facts.scaled_d = scaled;
    const auto report = check_tcgen_f16_known_operation(facts);
    EXPECT_TRUE(contains(report.checked, TcgenF16Checked::Target));
    EXPECT_EQ(!contains(report.violations, TcgenF16Violation::Target),
              accepted);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
