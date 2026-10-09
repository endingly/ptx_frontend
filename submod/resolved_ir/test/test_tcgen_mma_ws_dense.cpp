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

/** Supply declared addresses and descriptors for one independent WS source. */
std::string ws_source(std::string_view body, std::string_view version = "9.3",
                      std::string_view target = "sm_100a") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i;
  .reg .b64 %ad, %bd, %z;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a tree whose lifetime can end before owned validation. */
std::optional<syntax_ast::AstModule> parse_ws(
    std::string_view body, std::string_view version = "9.3",
    std::string_view target = "sm_100a") {
  const std::string source = ws_source(body, version, target);
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check a complete WS module, including source and owned bindings. */
bool accepts_ws(std::string_view body, std::string_view version = "9.3",
                std::string_view target = "sm_100a") {
  auto ast = parse_ws(body, version, target);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find one typed violation or missing obligation. */
template <typename T>
  requires std::is_enum_v<T>
bool has(const std::vector<T>& values, T wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

/** Each WS kind owns four A/zero layouts and sixteen B-control spellings. */
TEST(TcgenMmaWsDense, CanonicalSourceAndExclusions) {
  for (std::string_view kind : {"f16", "tf32", "f8f6f4", "i8"}) {
    const std::string prefix =
        "tcgen05.mma.ws.cta_group::1.kind::" + std::string(kind);
    for (bool shared_a : {false, true}) {
      for (bool zero : {false, true}) {
        const std::string args =
            " [%d], " + std::string(shared_a ? "%ad" : "[%a]") +
            ", %bd, %i, %p" + std::string(zero ? ", %z;" : ";");
        SCOPED_TRACE(prefix + args);
        EXPECT_TRUE(accepts_ws(prefix + args));
        for (unsigned buffer = 0; buffer < 4; ++buffer)
          for (std::string_view op : {"fill", "use", "lastuse", "discard"})
            EXPECT_TRUE(accepts_ws(prefix + ".collector::b" +
                                   std::to_string(buffer) +
                                   "::" + std::string(op) + args));
      }
    }
    EXPECT_FALSE(accepts_ws("tcgen05.mma.ws.cta_group::2.kind::" +
                            std::string(kind) + " [%d], [%a], %bd, %i, %p;"));
    for (std::string_view suffix :
         {".collector::a::fill", ".ashift", ".block_scale", ".mask"})
      EXPECT_FALSE(accepts_ws(prefix + std::string(suffix) +
                              " [%d], [%a], %bd, %i, %p;"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, 0;"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, [%a];"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, %a;"));
    EXPECT_FALSE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "9.3", "sm_90a"));
    EXPECT_FALSE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "8.5", "sm_100a"));
    EXPECT_TRUE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "9.3", "sm_110a"));
  }
}

/** A borrowed WS view and its bindings remain valid after AST destruction. */
TEST(TcgenMmaWsDense, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_ws(
        "tcgen05.mma.ws.cta_group::1.kind::f16"
        ".collector::b2::lastuse [%d], [%a], %bd, %i, %p, %z;");
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaWsF16*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_ws_view(*form);
  ASSERT_TRUE(view);
  EXPECT_FALSE(view->a_shared.has_value());
  EXPECT_NE(view->a_tmem, nullptr);
  ASSERT_TRUE(view->zero_column.has_value());
  EXPECT_NE(view->zero_column->source, nullptr);
  EXPECT_EQ(view->collector->value.buffer, TcgenCollectorBuffer::B2);
  EXPECT_EQ(view->collector->value.operation, TcgenCollectorOp::LastUse);
  EXPECT_EQ(view->collector->locs.size(), 1);
  const auto collector = form->collector.value;
  form->collector.value.buffer = TcgenCollectorBuffer::A;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = collector;
  form->collector.value.operation = static_cast<TcgenCollectorOp>(255);
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = collector;
  const auto locs = form->collector.locs;
  form->collector.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.locs = locs;
  auto& zero = form->zero_column_desc->value;
  const auto zero_type = zero.declared_type;
  zero.declared_type = ScalarType::B32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  zero.declared_type = zero_type;
  const auto zero_locs = form->zero_column_desc->locs;
  form->zero_column_desc->locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->zero_column_desc->locs = zero_locs;
  form->zero_column_desc.reset();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Omission keeps an empty source control while B0/discard is derived. */
TEST(TcgenMmaWsDense, OmittedAndWrittenDiscard) {
  auto ast =
      parse_ws("tcgen05.mma.ws.cta_group::1.kind::i8 [%d], %ad, %bd, %i, %p;");
  ASSERT_TRUE(ast);
  auto owned = resolveAndValidateModule(*ast);
  ASSERT_TRUE(owned.has_value());
  auto* form = dynamic_cast<Tcgen05MmaWsI8*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  EXPECT_EQ(form->collector.value.buffer, TcgenCollectorBuffer::Unspecified);
  EXPECT_TRUE(form->collector.locs.empty());
  form->collector.locs.push_back(form->cta_group.locs.front());
  EXPECT_FALSE(validateModule(*owned).has_value());
  EXPECT_TRUE(
      accepts_ws("tcgen05.mma.ws.cta_group::1.kind::i8"
                 ".collector::b0::discard [%d], %ad, %bd, %i, %p;"));
}

/** Caller-known B history is per buffer; unknown history remains an obligation. */
TEST(TcgenMmaWsDense, KnownFactsAndTable48) {
  TcgenWsKnownFacts facts{
      .source_kind = TcgenMmaKind::F16,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(2U << 24) | (8U << 17), TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .collector = {TcgenCollectorBuffer::B2, TcgenCollectorOp::Use},
  };
  auto report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::CollectorHistory));
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::CollectorSequence));
  facts.collector_b_valid[0] = true;
  facts.collector_b_valid[2] = false;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::CollectorHistory));
  facts.collector_b_valid[2] = true;
  EXPECT_TRUE(check_tcgen_ws_known_operation(facts).supplied_facts_ok());
  facts.d_lane_half = 16;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::DLaneHalf));
  facts.d_lane_half = 0;
  facts.zero_column_operand_present = true;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).missing,
                  TcgenWsObligation::ZeroColumnWord));
  facts.zero_column_word = TcgenZeroColumnWord{0};
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::LiveZeroColumnWord));
  facts.collector = {};
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_EQ(report.effective_collector.buffer, TcgenCollectorBuffer::B0);
  EXPECT_EQ(report.effective_collector.operation, TcgenCollectorOp::Discard);
  EXPECT_NE(report.effective_collector, facts.collector);
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm90a = base::find_target_profile("sm_90a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm90a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{8, 6};
  EXPECT_FALSE(has(check_tcgen_ws_known_operation(facts).violations,
                   TcgenWsViolation::Target));
  facts.target = sm90a->identity;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::Target));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
