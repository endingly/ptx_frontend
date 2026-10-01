#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
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
  auto& instruction = *owned->functions.front().body[0].get_if<Tcgen05>();
  auto& form = std::get<Tcgen05::MmaF16>(instruction.variant);
  auto& payload =
      std::get<Tcgen05::MmaF16::SharedMaskScaleOperands>(form.operands);
  const auto context = mma_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(checker::check(instruction, context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  ASSERT_TRUE(tcgen_mma_f16_view(instruction));
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->a_shared->source,
            &payload.a.value);
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->instruction.source,
            &payload.idesc.value);
  EXPECT_EQ(tcgen_mma_f16_view(instruction)->disable_output_lane,
            &payload.disable_output_lane.value);
  EXPECT_TRUE(checker::check(instruction, context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  payload.a.value.register_class = ResolvedRegisterClass::Predicate;
  reject();
  payload.a.value.register_class = ResolvedRegisterClass::General;
  payload.a.value.vector_width = 2;
  reject();
  payload.a.value.vector_width.reset();
  const auto a_type = payload.a.value.declared_type;
  payload.a.value.declared_type = ScalarType::F64;
  reject();
  payload.a.value.declared_type.reset();
  reject();
  payload.a.value.declared_type = a_type;
  payload.b.value.declared_type = ScalarType::B32;
  reject();
  payload.b.value.declared_type = ScalarType::B64;
  payload.idesc.value.declared_type = ScalarType::F32;
  reject();
  payload.idesc.value.declared_type = ScalarType::B32;
  const auto last_lane = payload.disable_output_lane.value.elements.back();
  payload.disable_output_lane.value.elements.pop_back();
  reject();
  payload.disable_output_lane.value.elements.push_back(last_lane);
  payload.disable_output_lane.value.elements[0]->register_class =
      ResolvedRegisterClass::Predicate;
  reject();
  payload.disable_output_lane.value.elements[0]->register_class =
      ResolvedRegisterClass::General;
  payload.disable_output_lane.value.elements[0]->declared_type =
      ScalarType::B64;
  reject();
  payload.disable_output_lane.value.elements[0]->declared_type =
      ScalarType::B32;
  auto& predicate = std::get<ResolvedPredicate>(payload.enable_input_d.value);
  predicate.register_ref.register_class = ResolvedRegisterClass::General;
  reject();
  predicate.register_ref.register_class = ResolvedRegisterClass::Predicate;
  predicate.register_ref.vector_width = 2;
  reject();
  predicate.register_ref.vector_width.reset();
  predicate.register_ref.declared_type = ScalarType::B32;
  reject();
  predicate.register_ref.declared_type = ScalarType::Pred;
  payload.scale_input_d.value.bits = 16;
  reject();
  payload.scale_input_d.value.bits = 15;
  payload.scale_input_d.value.integer_source_bits = 1ull << 32;
  reject();
  payload.scale_input_d.value.integer_source_bits = 15;
  payload.d.value.bracketed = false;
  reject();
  payload.d.value.bracketed = true;
  auto& d_register = std::get<ResolvedRegisterRef>(payload.d.value.value);
  d_register.register_class = ResolvedRegisterClass::Predicate;
  reject();
  d_register.register_class = ResolvedRegisterClass::General;
  d_register.vector_width = 2;
  reject();
  d_register.vector_width.reset();
  d_register.declared_type = ScalarType::F32;
  reject();
  d_register.declared_type = ScalarType::B32;
  const auto saved_predicate = payload.enable_input_d.value;
  payload.enable_input_d.value = ResolvedPredicateConstant{true};
  EXPECT_TRUE(checker::check(instruction, context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
  payload.enable_input_d.value = saved_predicate;
  payload.scale_input_d.value.is_negative = true;
  reject();
  payload.scale_input_d.value.is_negative = false;
  payload.scale_input_d.value.type = ScalarType::F32;
  reject();
  payload.scale_input_d.value.type = ScalarType::U32;
  form.operand_layout.value = 0;
  reject();
  EXPECT_FALSE(tcgen_mma_f16_view(instruction));
  form.operand_layout.value = 3;
  EXPECT_TRUE(checker::check(instruction, context).has_value());
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
  EXPECT_EQ(Tcgen05::MmaF16::completion_kind,
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
