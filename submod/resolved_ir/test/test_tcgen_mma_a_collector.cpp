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

/** Wrap one ordinary MMA with declaration-backed opaque addresses. */
std::string a_collector_source(std::string_view body) {
  return R"ptx(.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i, %sp;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse an independently destructible syntax tree. */
std::optional<syntax_ast::AstModule> parse_a_collector(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Resolve and check one complete source module. */
bool accepts_a_collector(std::string_view body) {
  auto ast = parse_a_collector(a_collector_source(body));
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find a typed report entry without relying on diagnostic text. */
template <typename T>
  requires std::is_enum_v<T>
bool contains(const std::vector<T>& items, T wanted) {
  return std::find(items.begin(), items.end(), wanted) != items.end();
}

/** Original identities accept an ordered A collector with old operands. */
TEST(TcgenMmaACollector, DenseSparseKindsAndSourceOrder) {
  for (bool sparse : {false, true}) {
    for (std::string_view kind : {"f16", "tf32", "f8f6f4", "i8"}) {
      for (unsigned group : {1U, 2U}) {
        const std::string prefix = "tcgen05.mma" +
                                   std::string(sparse ? ".sp" : "") +
                                   ".cta_group::" + std::to_string(group) +
                                   ".kind::" + std::string(kind);
        const std::string args = " [%d], [%a], %bd, " +
                                 std::string(sparse ? "[%sp], " : "") +
                                 "%i, %p;";
        SCOPED_TRACE(prefix);
        EXPECT_TRUE(accepts_a_collector(prefix + args));
        for (std::string_view op : {"fill", "use", "lastuse", "discard"})
          EXPECT_TRUE(accepts_a_collector(
              prefix + ".collector::a::" + std::string(op) + args));
        EXPECT_TRUE(accepts_a_collector(
            prefix + ".ashift.collector::a::lastuse" + args));
        EXPECT_TRUE(accepts_a_collector(prefix + ".ashift" + args));
        EXPECT_FALSE(
            accepts_a_collector(prefix + ".ashift.collector::a::fill" + args));
        EXPECT_FALSE(
            accepts_a_collector(prefix + ".ashift.collector::a::use" + args));
        EXPECT_FALSE(accepts_a_collector(
            prefix + ".collector::a::discard.ashift" + args));
        EXPECT_FALSE(
            accepts_a_collector(prefix + ".collector::b0::fill" + args));
        const std::string shared = " [%d], %ad, %bd, " +
                                   std::string(sparse ? "[%sp], " : "") +
                                   "%i, %p;";
        EXPECT_TRUE(
            accepts_a_collector(prefix + ".collector::a::fill" + shared));
        EXPECT_FALSE(accepts_a_collector(prefix + ".ashift" + shared));
      }
    }
  }
}

/** Owned control values and locations survive syntax-tree destruction. */
TEST(TcgenMmaACollector, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_a_collector(a_collector_source(
        "tcgen05.mma.sp.cta_group::1.kind::f16.ashift"
        ".collector::a::lastuse [%d], [%a], %bd, [%sp], %i, %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaSpF16*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  auto view = tcgen_mma_a_collector_view(*form);
  ASSERT_TRUE(view);
  EXPECT_TRUE(view->a_in_tmem);
  EXPECT_TRUE(view->ashift->value);
  EXPECT_EQ(view->collector->value.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(view->collector->value.operation, TcgenCollectorOp::LastUse);
  EXPECT_EQ(view->collector->locs.size(), 1);
  auto original = form->collector.value;
  form->collector.value.buffer = TcgenCollectorBuffer::Unspecified;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = original;
  form->collector.value.operation = static_cast<TcgenCollectorOp>(255);
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = original;
  const auto original_locs = form->collector.locs;
  form->collector.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.locs = original_locs;
  form->ashift.value = false;
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Omitted and explicit discard controls retain distinct source provenance. */
TEST(TcgenMmaACollector, OmittedAndWrittenDiscard) {
  auto absent_ast = parse_a_collector(a_collector_source(
      "tcgen05.mma.cta_group::1.kind::i8 [%d], [%a], %bd, %i, %p;"));
  ASSERT_TRUE(absent_ast);
  auto absent = resolveAndValidateModule(*absent_ast);
  ASSERT_TRUE(absent.has_value());
  auto* omitted =
      dynamic_cast<Tcgen05MmaI8*>(absent->functions.front().body.front().get());
  ASSERT_TRUE(omitted);
  EXPECT_EQ(omitted->collector.value.buffer, TcgenCollectorBuffer::Unspecified);
  EXPECT_EQ(omitted->collector.value.operation, TcgenCollectorOp::Unspecified);
  EXPECT_TRUE(omitted->collector.locs.empty());
  omitted->collector.locs.push_back(omitted->cta_group.locs.front());
  EXPECT_FALSE(validateModule(*absent).has_value());

  auto written_ast = parse_a_collector(a_collector_source(
      "tcgen05.mma.cta_group::1.kind::i8.collector::a::discard "
      "[%d], [%a], %bd, %i, %p;"));
  ASSERT_TRUE(written_ast);
  auto written = resolveAndValidateModule(*written_ast);
  ASSERT_TRUE(written.has_value());
  auto* explicit_discard = dynamic_cast<Tcgen05MmaI8*>(
      written->functions.front().body.front().get());
  ASSERT_TRUE(explicit_discard);
  EXPECT_EQ(explicit_discard->collector.value.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(explicit_discard->collector.value.operation,
            TcgenCollectorOp::Discard);
  EXPECT_EQ(explicit_discard->collector.locs.size(), 1);
}

/** Missing history differs from a contradictory caller assertion. */
TEST(TcgenMmaACollector, KnownMAndHistoryAreConditional) {
  TcgenACollectorKnownFacts facts{
      .collector = {TcgenCollectorBuffer::A, TcgenCollectorOp::LastUse},
      .ashift = true,
      .a_in_tmem = true,
      .m = 128,
  };
  auto report = check_tcgen_a_collector_known_facts(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(contains(report.missing, TcgenACollectorObligation::History));
  EXPECT_TRUE(
      contains(report.missing, TcgenACollectorObligation::CollectorSequence));
  auto changed = facts;
  changed.collector_a_valid = false;
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).violations,
                       TcgenACollectorViolation::History));
  changed.collector_a_valid = true;
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).missing,
                       TcgenACollectorObligation::CollectorSequence));
  changed = facts;
  changed.m = 64;
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).violations,
                       TcgenACollectorViolation::AshiftM));
  changed.m.reset();
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).missing,
                       TcgenACollectorObligation::AshiftM));
  changed = facts;
  changed.a_in_tmem = false;
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).violations,
                       TcgenACollectorViolation::AshiftAPlacement));
  changed = facts;
  changed.collector = {};
  report = check_tcgen_a_collector_known_facts(changed);
  EXPECT_EQ(report.effective.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(report.effective.operation, TcgenCollectorOp::Discard);
  EXPECT_NE(changed.collector, report.effective);
  changed.collector = {TcgenCollectorBuffer::B0, TcgenCollectorOp::Fill};
  EXPECT_TRUE(contains(check_tcgen_a_collector_known_facts(changed).violations,
                       TcgenACollectorViolation::CollectorDomain));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
