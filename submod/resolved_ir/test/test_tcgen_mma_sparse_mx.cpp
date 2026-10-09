#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap one block-scaled sparse source statement with opaque registers. */
std::string sparse_mx_source(std::string_view body,
                             std::string_view target = "sm_100a",
                             std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %sa, %sb, %sp;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n ret;\n}\n";
}

/** Parse while keeping the syntax tree independently destructible. */
std::optional<syntax_ast::AstModule> parse_sparse_mx(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check a complete module through owned resolution and validation. */
bool accepts_sparse_mx(std::string_view source) {
  auto ast = parse_sparse_mx(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Query a closed enum report without equating missing facts with success. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& items, T wanted) {
  return std::find(items.begin(), items.end(), wanted) != items.end();
}

/** Three kinds use exactly two placements and the mandatory metadata role. */
TEST(TcgenMmaSparseMx, SourceTargetsAndTopology) {
  for (std::string_view kind : {"mxf8f6f4", "mxf4", "mxf4nvf4"}) {
    for (unsigned group : {1U, 2U}) {
      for (bool shared : {false, true}) {
        const std::string prefix =
            "tcgen05.mma.sp.cta_group::" + std::to_string(group) +
            ".kind::" + std::string(kind) + ".block_scale";
        const std::string args = " [%d], " +
                                 std::string(shared ? "%ad" : "[%a]") +
                                 ", %bd, [%sp], %i, [%sa], [%sb], %p;";
        SCOPED_TRACE(prefix + args);
        if (kind != "mxf4nvf4")
          EXPECT_TRUE(accepts_sparse_mx(
              sparse_mx_source(prefix + args, "sm_100a", "8.6")));
        else
          EXPECT_FALSE(accepts_sparse_mx(sparse_mx_source(prefix + args)));
        const std::string vector =
            kind == "mxf8f6f4" ? ".scale_vec::1X" : ".scale_vec::2X";
        EXPECT_TRUE(accepts_sparse_mx(
            sparse_mx_source(prefix + vector + args, "sm_100a",
                             kind == "mxf4nvf4" ? "8.7" : "8.6")));
        const std::string block = kind == "mxf4nvf4" ? ".block16" : ".block32";
        EXPECT_TRUE(accepts_sparse_mx(
            sparse_mx_source(prefix + block + args, "sm_103a", "8.8")));
        EXPECT_EQ(accepts_sparse_mx(sparse_mx_source(prefix + block + args,
                                                     "sm_100f", "8.8")),
                  kind == "mxf8f6f4");
        EXPECT_FALSE(accepts_sparse_mx(
            sparse_mx_source(prefix + vector + args, "sm_100f", "8.8")));
        EXPECT_FALSE(accepts_sparse_mx(sparse_mx_source(
            prefix + block + " [%d], %ad, %bd, %i, [%sp], [%sa], [%sb], %p;",
            "sm_103a", "8.8")));
        EXPECT_FALSE(accepts_sparse_mx(
            sparse_mx_source(prefix + block +
                                 " [%d], %ad, %bd, [%sp], %i, [%sa], [%sb], "
                                 "{%a}, %p;",
                             "sm_103a", "8.8")));
      }
    }
  }
}

/** Borrow metadata and scale roles after AST death and reject owned tamper. */
TEST(TcgenMmaSparseMx, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_sparse_mx(sparse_mx_source(
        "tcgen05.mma.sp.cta_group::1.kind::mxf4nvf4.block_scale.block16 "
        "[%d], [%a], %bd, [%sp], %i, [%sa], [%sb], %p;",
        "sm_103a", "8.8"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaSpMxf4nvf4*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_sparse_mx_view(*form);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->kind, TcgenMmaKind::MxF4NvF4);
  EXPECT_EQ(view->metadata, &form->sp_meta.value);
  EXPECT_EQ(view->scale_a, &form->scale_a.value);
  EXPECT_EQ(view->scale_selector->value, TcgenScaleVectorSize::Block16);
  EXPECT_NE(view->a_tmem, nullptr);
  EXPECT_FALSE(view->a_shared.has_value());
  form->scale_vector_size.value = TcgenScaleVectorSize::Absent;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->scale_vector_size.value = TcgenScaleVectorSize::Block16;
  auto& metadata = std::get<ResolvedRegisterRef>(form->sp_meta.value.value);
  const auto old_type = metadata.declared_type;
  metadata.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  metadata.declared_type = old_type;
  EXPECT_TRUE(validateModule(*owned).has_value());
  form->sp_meta.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Word values and source spelling independently constrain sparse MX rows. */
TEST(TcgenMmaSparseMx, KnownRowsMetadataScaleAndTarget) {
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm103a = base::find_target_profile("sm_103a");
  const auto sm100f = base::find_target_profile("sm_100f");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm103a);
  ASSERT_TRUE(sm100f);
  constexpr uint32_t mx8 =
      (1U << 27) | (2U << 17) | (1U << 23) | (1U << 10) | (1U << 2);
  TcgenSparseMxKnownFacts facts{
      .source_kind = TcgenMmaKind::MxF8F6F4,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .scale_selector = TcgenScaleVectorSize::Absent,
      .instruction = {mx8, TcgenMmaKind::MxF8F6F4},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .metadata_lane_half = 0,
      .metadata_nibbles = std::vector<uint8_t>{14},
      .target = sm100a->identity,
      .ptx_version = checker::PtxVersion{8, 6},
  };
  auto report = check_tcgen_sparse_mx_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  ASSERT_TRUE(report.shape.has_value());
  EXPECT_EQ(report.shape->k, 64);
  EXPECT_EQ(report.scale_a_factor_count, 1);
  EXPECT_EQ(report.path_layout, 'D');
  EXPECT_TRUE(
      contains(report.missing, TcgenSparseMxObligation::LiveMetadataContents));
  auto bad = facts;
  bad.instruction.bits &= ~(1U << 2);
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::SparseBit));
  bad = facts;
  bad.metadata_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::MetadataLaneHalf));
  bad = facts;
  bad.instruction.kind = TcgenMmaKind::MxF4;
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::Kind));
  constexpr uint32_t mx4 =
      (2U << 27) | (4U << 17) | (1U << 23) | (1U << 7) | (1U << 10) | (1U << 2);
  facts.source_kind = TcgenMmaKind::MxF4;
  facts.group = TcgenCtaGroup::Two;
  facts.scale_selector = TcgenScaleVectorSize::Block32;
  facts.instruction = {mx4, TcgenMmaKind::MxF4};
  facts.target = sm103a->identity;
  facts.ptx_version = checker::PtxVersion{8, 8};
  report = check_tcgen_sparse_mx_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  ASSERT_TRUE(report.shape.has_value());
  EXPECT_EQ(report.shape->k, 128);
  EXPECT_EQ(report.scale_a_factor_count, 2);
  bad = facts;
  bad.instruction.bits |= 1U << 31;
  EXPECT_FALSE(check_tcgen_sparse_mx_known_operation(bad)
                   .instruction_fields.defined_fields_ok());
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::KChoice));
  bad = facts;
  bad.target = sm100f->identity;
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::Target));
  bad = facts;
  bad.instruction.bits |= 1U << 29;
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::ScaleAId));
  bad = facts;
  bad.metadata_nibbles = std::vector<uint8_t>{0};
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::MetadataIndex));
  constexpr uint32_t nv =
      (1U << 27) | (2U << 17) | (1U << 7) | (1U << 10) | (1U << 2);
  facts.source_kind = TcgenMmaKind::MxF4NvF4;
  facts.group = TcgenCtaGroup::One;
  facts.scale_selector = TcgenScaleVectorSize::Block16;
  facts.instruction = {nv, TcgenMmaKind::MxF4NvF4};
  report = check_tcgen_sparse_mx_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_EQ(report.scale_a_factor_count, 4);
  bad = facts;
  bad.scale_selector = TcgenScaleVectorSize::Absent;
  EXPECT_TRUE(contains(check_tcgen_sparse_mx_known_operation(bad).violations,
                       TcgenSparseMxViolation::Selector));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
