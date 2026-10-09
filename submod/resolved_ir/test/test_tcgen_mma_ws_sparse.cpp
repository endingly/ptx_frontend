#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply declaration-backed WS sparse operands for one selected source. */
std::string ws_sparse_source(std::string_view body,
                             std::string_view version = "9.3",
                             std::string_view target = "sm_100a") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %sp, %i;
  .reg .b64 %ad, %bd, %z;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a source whose syntax tree can die before module validation. */
std::optional<syntax_ast::AstModule> parse_ws_sparse(
    std::string_view body, std::string_view version = "9.3",
    std::string_view target = "sm_100a") {
  const std::string source = ws_sparse_source(body, version, target);
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check a complete WS sparse source with owned bindings. */
bool accepts_ws_sparse(std::string_view body, std::string_view version = "9.3",
                       std::string_view target = "sm_100a") {
  auto ast = parse_ws_sparse(body, version, target);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find a typed report item without relying on diagnostic wording. */
template <typename T>
  requires std::is_enum_v<T>
bool contains_ws_sparse(const std::vector<T>& values, T wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

/** Four exact sparse forms retain metadata order and all B controls. */
TEST(TcgenMmaWsSparse, SourceFormsAndExclusions) {
  for (std::string_view kind : {"f16", "tf32", "f8f6f4", "i8"}) {
    const std::string prefix =
        "tcgen05.mma.ws.sp.cta_group::1.kind::" + std::string(kind);
    for (bool shared_a : {false, true})
      for (bool zero : {false, true}) {
        const std::string args =
            " [%d], " + std::string(shared_a ? "%ad" : "[%a]") +
            ", %bd, [%sp], %i, %p" + std::string(zero ? ", %z;" : ";");
        SCOPED_TRACE(prefix + args);
        EXPECT_TRUE(accepts_ws_sparse(prefix + args));
        for (unsigned buffer = 0; buffer < 4; ++buffer)
          for (std::string_view op : {"fill", "use", "lastuse", "discard"})
            EXPECT_TRUE(accepts_ws_sparse(prefix + ".collector::b" +
                                          std::to_string(buffer) +
                                          "::" + std::string(op) + args));
      }
    EXPECT_FALSE(accepts_ws_sparse(
        "tcgen05.mma.sp.ws.cta_group::1.kind::" + std::string(kind) +
        " [%d], [%a], %bd, [%sp], %i, %p;"));
    EXPECT_FALSE(accepts_ws_sparse(
        "tcgen05.mma.ws.sp.cta_group::2.kind::" + std::string(kind) +
        " [%d], [%a], %bd, [%sp], %i, %p;"));
    EXPECT_FALSE(accepts_ws_sparse(prefix + " [%d], [%a], %bd, %i, %p;"));
    EXPECT_FALSE(
        accepts_ws_sparse(prefix + " [%d], [%a], [%sp], %bd, %i, %p;"));
    for (std::string_view suffix :
         {".collector::a::fill", ".ashift", ".block_scale", ".mask"})
      EXPECT_FALSE(accepts_ws_sparse(prefix + std::string(suffix) +
                                     " [%d], [%a], %bd, [%sp], %i, %p;"));
    const std::string args = " [%d], [%a], %bd, [%sp], %i, %p;";
    EXPECT_FALSE(accepts_ws_sparse(prefix + args, "9.3", "sm_90a"));
    EXPECT_FALSE(accepts_ws_sparse(prefix + args, "8.5", "sm_100a"));
    EXPECT_TRUE(accepts_ws_sparse(prefix + args, "9.3", "sm_110a"));
  }
}

/** Sparse metadata and B-control references survive syntax-tree destruction. */
TEST(TcgenMmaWsSparse, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_ws_sparse(
        "tcgen05.mma.ws.sp.cta_group::1.kind::f16.collector::b3::lastuse"
        " [%d], [%a], %bd, [%sp], %i, %p, %z;");
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaWsSpF16*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_ws_view(*form);
  ASSERT_TRUE(view);
  EXPECT_TRUE(view->sparse);
  EXPECT_EQ(view->metadata, &form->sp_meta.value);
  EXPECT_NE(view->a_tmem, nullptr);
  EXPECT_TRUE(view->zero_column.has_value());
  EXPECT_EQ(view->collector->value.buffer, TcgenCollectorBuffer::B3);
  auto& meta = std::get<ResolvedRegisterRef>(form->sp_meta.value.value);
  const auto original_type = meta.declared_type;
  meta.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  meta.declared_type = original_type;
  const auto locs = form->sp_meta.locs;
  form->sp_meta.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->sp_meta.locs = locs;
  form->collector.value.buffer = TcgenCollectorBuffer::A;
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Independent words check compressed K, zero lanes and B history only. */
TEST(TcgenMmaWsSparse, KnownMetadataAndZeroColumn) {
  TcgenWsKnownFacts facts{
      .source_kind = TcgenMmaKind::F16,
      .source_sparse = true,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(2U << 24) | (8U << 17) | 4U, TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .metadata_lane_half = 0,
      .metadata_nibbles = std::vector<uint8_t>{14},
      .zero_column_operand_present = true,
      .zero_column_word = TcgenZeroColumnWord{0},
      .collector = {TcgenCollectorBuffer::B1, TcgenCollectorOp::Use},
  };
  auto report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  ASSERT_TRUE(report.shape);
  EXPECT_TRUE(report.shape->sparse);
  EXPECT_EQ(report.shape->k, 32);
  EXPECT_EQ(report.compressed_a_k, 16);
  EXPECT_EQ(report.logical_b_k, 32);
  ASSERT_TRUE(report.metadata_rule);
  EXPECT_EQ(report.metadata_rule->granularity,
            TcgenSparseGranularity::TwoOfFour);
  EXPECT_TRUE(contains_ws_sparse(report.missing,
                                 TcgenWsObligation::MetadataLayoutRule));
  EXPECT_TRUE(
      contains_ws_sparse(report.missing, TcgenWsObligation::CollectorHistory));
  facts.collector_b_valid[0] = true;
  facts.collector_b_valid[1] = false;
  EXPECT_TRUE(
      contains_ws_sparse(check_tcgen_ws_known_operation(facts).violations,
                         TcgenWsViolation::CollectorHistory));
  facts.collector_b_valid[1] = true;
  facts.metadata_lane_half = 16;
  EXPECT_TRUE(
      contains_ws_sparse(check_tcgen_ws_known_operation(facts).violations,
                         TcgenWsViolation::MetadataLaneHalf));
  facts.metadata_lane_half = 0;
  facts.metadata_nibbles = std::vector<uint8_t>{0};
  EXPECT_TRUE(
      contains_ws_sparse(check_tcgen_ws_known_operation(facts).violations,
                         TcgenWsViolation::MetadataIndex));
  facts.metadata_nibbles = std::vector<uint8_t>{14};
  facts.instruction.bits &= ~4U;
  EXPECT_TRUE(
      contains_ws_sparse(check_tcgen_ws_known_operation(facts).violations,
                         TcgenWsViolation::SparseBit));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
