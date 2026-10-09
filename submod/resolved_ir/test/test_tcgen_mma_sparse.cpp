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

/** Build a module whose descriptor and metadata registers remain opaque. */
std::string sparse_source(std::string_view body,
                          std::string_view target = "sm_100a",
                          std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %sp;
  .reg .b32 %m<8>;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse source while retaining independent syntax-tree ownership. */
std::optional<syntax_ast::AstModule> parse_sparse(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Evaluate source acceptance through the complete owned-module checker. */
bool accepts_sparse(std::string_view source) {
  auto ast = parse_sparse(source);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Query membership for one closed enum diagnostic. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& items, T wanted) {
  return std::find(items.begin(), items.end(), wanted) != items.end();
}

/** Four exact kinds retain both A placements and reject bad metadata syntax. */
TEST(TcgenMmaSparse, SourceTopologyAndTargets) {
  for (std::string_view kind : {"f16", "tf32", "f8f6f4", "i8"}) {
    for (unsigned group : {1U, 2U}) {
      for (bool shared_a : {false, true}) {
        const std::string prefix =
            "tcgen05.mma.sp.cta_group::" + std::to_string(group) +
            ".kind::" + std::string(kind) + " [%d], " +
            (shared_a ? "%ad" : "[%a]") + ", %bd, ";
        const std::string tail = ", %i, %p;";
        SCOPED_TRACE(prefix);
        EXPECT_TRUE(accepts_sparse(sparse_source(prefix + "[%sp]" + tail)));
        EXPECT_FALSE(accepts_sparse(sparse_source(prefix + "%sp" + tail)));
        EXPECT_FALSE(accepts_sparse(sparse_source(prefix + "%i, [%sp], %p;")));
        EXPECT_FALSE(accepts_sparse(
            sparse_source(prefix + "[%sp]" + tail, "sm_90a", "8.6")));
        const std::string mask = group == 1
                                     ? "{%m0,%m1,%m2,%m3}"
                                     : "{%m0,%m1,%m2,%m3,%m4,%m5,%m6,%m7}";
        EXPECT_TRUE(accepts_sparse(
            sparse_source(prefix + "[%sp], %i, " + mask + ", %p;")));
        EXPECT_FALSE(accepts_sparse(
            sparse_source(prefix + "[%sp], %i, " +
                          (group == 1 ? "{%m0,%m1,%m2,%m3,%m4,%m5,%m6,%m7}"
                                      : "{%m0,%m1,%m2,%m3}") +
                          ", %p;")));
      }
    }
  }
  EXPECT_TRUE(accepts_sparse(sparse_source(
      "tcgen05.mma.sp.cta_group::1.kind::f16 [%d], %ad, %bd, [%sp], %i, "
      "{%a,%a,%a,%a}, %p, 15;")));
  EXPECT_FALSE(accepts_sparse(sparse_source(
      "tcgen05.mma.sp.cta_group::1.kind::i8 [%d], %ad, %bd, [%sp], %i, "
      "%p, 1;")));
  EXPECT_FALSE(accepts_sparse(sparse_source(
      "tcgen05.sp.mma.cta_group::1.kind::f16 [%d], %ad, %bd, [%sp], %i, "
      "%p;")));
}

/** Sparse metadata and bound register facts survive syntax-tree destruction. */
TEST(TcgenMmaSparse, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_sparse(sparse_source(
        "tcgen05.mma.sp.cta_group::1.kind::f16 [%d], %ad, %bd, [%sp], "
        "%i, %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaSpF16*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_sparse_view(*form);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->kind, TcgenMmaKind::F16);
  EXPECT_EQ(view->metadata, &form->sp_meta.value);
  EXPECT_TRUE(view->a_shared.has_value());
  EXPECT_EQ(view->a_tmem, nullptr);
  const auto original_layout = form->operand_layout.value;
  form->operand_layout.value = 99;
  EXPECT_FALSE(tcgen_mma_sparse_view(*form).has_value());
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->operand_layout.value = original_layout;
  auto& metadata_register =
      std::get<ResolvedRegisterRef>(form->sp_meta.value.value);
  const auto original_type = metadata_register.declared_type;
  metadata_register.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  metadata_register.declared_type = original_type;
  EXPECT_TRUE(validateModule(*owned).has_value());
  form->sp_meta.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Known descriptor and metadata claims are conditional, not live decoding. */
TEST(TcgenMmaSparse, KnownSparseRulesAndHalfLanes) {
  constexpr uint32_t f16_word = (4U << 24) | (1U << 17) | (1U << 2);
  TcgenSparseKnownFacts facts{
      .source_kind = TcgenMmaKind::F16,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction = {f16_word, TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .metadata_lane_half = 0,
      .metadata_nibbles = std::vector<uint8_t>{14},
  };
  const auto report = check_tcgen_sparse_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_EQ(report.compressed_a_k, 16);
  EXPECT_EQ(report.logical_b_k, 32);
  EXPECT_TRUE(
      contains(report.missing, TcgenSparseObligation::LiveMetadataContents));
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm110a = base::find_target_profile("sm_110a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm110a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{8, 6};
  EXPECT_FALSE(contains(check_tcgen_sparse_known_operation(facts).violations,
                        TcgenSparseViolation::Target));
  facts.scale_d = 15;
  facts.target = sm110a->identity;
  facts.ptx_version = checker::PtxVersion{9, 0};
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(facts).violations,
                       TcgenSparseViolation::Target));
  facts.scale_d.reset();
  facts.target.reset();
  facts.ptx_version.reset();
  auto bad = facts;
  bad.instruction.bits &= ~(1U << 2);
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::SparseBit));
  bad = facts;
  bad.instruction.kind = TcgenMmaKind::Tf32;
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::Kind));
  bad = facts;
  bad.metadata_nibbles = std::vector<uint8_t>{0};
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::MetadataIndex));
  bad = facts;
  bad.metadata_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::HalfAlignment));
  bad = facts;
  bad.instruction.bits = (8U << 24) | (1U << 17) | (1U << 2);
  bad.a_lane_half = 16;
  bad.d_lane_half = 16;
  bad.metadata_lane_half = 16;
  const auto full_path = check_tcgen_sparse_known_operation(bad);
  EXPECT_EQ(full_path.path_layout, 'D');
  EXPECT_TRUE(contains(full_path.violations, TcgenSparseViolation::ALaneHalf));
  EXPECT_TRUE(contains(full_path.violations, TcgenSparseViolation::DLaneHalf));
  EXPECT_TRUE(
      contains(full_path.violations, TcgenSparseViolation::MetadataLaneHalf));
  bad.a_lane_half = 0;
  bad.d_lane_half = 0;
  bad.metadata_lane_half = 0;
  EXPECT_TRUE(check_tcgen_sparse_known_operation(bad).supplied_facts_ok());
  bad.a_lane_half.reset();
  bad.d_lane_half.reset();
  bad.metadata_lane_half.reset();
  const auto missing_lanes = check_tcgen_sparse_known_operation(bad);
  EXPECT_TRUE(
      contains(missing_lanes.missing, TcgenSparseObligation::ALaneHalf));
  EXPECT_TRUE(
      contains(missing_lanes.missing, TcgenSparseObligation::DLaneHalf));
  EXPECT_TRUE(
      contains(missing_lanes.missing, TcgenSparseObligation::MetadataLaneHalf));
  bad = facts;
  bad.source_kind = TcgenMmaKind::F8F6F4;
  bad.instruction = {
      0x10U | (4U << 24) | (1U << 17) | (1U << 16) | (1U << 2) | (5U << 7),
      TcgenMmaKind::F8F6F4};
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::BTransposeN));
  bad.instruction.bits |= 3U << 10;
  const auto low_bits = check_tcgen_sparse_known_operation(bad);
  EXPECT_TRUE(
      contains(low_bits.missing, TcgenSparseObligation::ALowBitPackingRule));
  EXPECT_TRUE(
      contains(low_bits.missing, TcgenSparseObligation::BLowBitPackingRule));
  bad = facts;
  bad.a_in_tmem = false;
  bad.a_lane_half.reset();
  bad.metadata_lane_half = 16;
  EXPECT_TRUE(contains(check_tcgen_sparse_known_operation(bad).violations,
                       TcgenSparseViolation::HalfAlignment));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
