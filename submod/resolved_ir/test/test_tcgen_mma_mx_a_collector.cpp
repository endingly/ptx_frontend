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

/** Wrap one MX source with declared opaque descriptors and TMEM addresses. */
std::string mx_collector_source(std::string_view body) {
  return R"ptx(.version 9.3
.target sm_100a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %sa, %sb, %meta, %i;
  .reg .b64 %ad, %bd;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a source tree that may be destroyed before owned-module validation. */
std::optional<syntax_ast::AstModule> parse_mx_collector(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check a complete module through the public source and owned contracts. */
bool accepts_mx_collector(std::string_view body) {
  auto ast = parse_mx_collector(mx_collector_source(body));
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find one typed report entry without comparing diagnostic prose. */
template <typename T>
  requires std::is_enum_v<T>
bool contains_mx(const std::vector<T>& items, T wanted) {
  return std::find(items.begin(), items.end(), wanted) != items.end();
}

/** Existing MX dense/sparse identities accept four A actions in both layouts. */
TEST(TcgenMmaMxACollector, SourceAndExclusions) {
  for (bool sparse : {false, true}) {
    for (std::string_view kind : {"mxf8f6f4", "mxf4", "mxf4nvf4"}) {
      const std::string selector =
          kind == "mxf8f6f4" ? ".scale_vec::1X" : ".scale_vec::2X";
      for (unsigned group : {1U, 2U}) {
        const std::string prefix =
            "tcgen05.mma" + std::string(sparse ? ".sp" : "") +
            ".cta_group::" + std::to_string(group) +
            ".kind::" + std::string(kind) + ".block_scale" + selector;
        for (bool shared : {false, true}) {
          const std::string operands =
              " [%d], " + std::string(shared ? "%ad" : "[%a]") + ", %bd, " +
              std::string(sparse ? "[%meta], " : "") + "%i, [%sa], [%sb], %p;";
          SCOPED_TRACE(prefix + operands);
          EXPECT_TRUE(accepts_mx_collector(prefix + operands));
          for (std::string_view op : {"fill", "use", "lastuse", "discard"})
            EXPECT_TRUE(accepts_mx_collector(
                prefix + ".collector::a::" + std::string(op) + operands));
          EXPECT_FALSE(
              accepts_mx_collector(prefix + ".collector::b0::fill" + operands));
          EXPECT_FALSE(accepts_mx_collector(
              prefix + ".collector::a::fill.ashift" + operands));
          EXPECT_FALSE(accepts_mx_collector(prefix + ".ashift" + operands));
          EXPECT_FALSE(accepts_mx_collector(
              "tcgen05.mma" + std::string(sparse ? ".sp" : "") +
              ".cta_group::" + std::to_string(group) +
              ".kind::" + std::string(kind) + ".block_scale.collector::a::use" +
              selector + operands));
        }
      }
    }
  }
  EXPECT_FALSE(accepts_mx_collector(
      "tcgen05.mma.cta_group::1.kind::mxf4nvf4.block_scale"
      ".collector::a::fill [%d], [%a], %bd, %i, [%sa], [%sb], %p;"));
}

/** Omitted and explicit controls retain distinct owned source provenance. */
TEST(TcgenMmaMxACollector, OwnedViewAndTamperAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_mx_collector(
        mx_collector_source("tcgen05.mma.sp.cta_group::1.kind::mxf4.block_scale"
                            ".scale_vec::2X.collector::a::lastuse "
                            "[%d], [%a], %bd, [%meta], %i, [%sa], [%sb], %p;"));
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaSpMxf4*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  auto view = tcgen_mma_mx_a_collector_view(*form);
  ASSERT_TRUE(view);
  EXPECT_TRUE(view->sparse);
  EXPECT_TRUE(view->a_in_tmem);
  EXPECT_EQ(view->kind, TcgenMmaKind::MxF4);
  EXPECT_EQ(view->collector->value.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(view->collector->value.operation, TcgenCollectorOp::LastUse);
  EXPECT_EQ(view->collector->locs.size(), 1);
  const auto value = form->collector.value;
  const auto locs = form->collector.locs;
  form->collector.value.buffer = TcgenCollectorBuffer::Unspecified;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = value;
  form->collector.value.operation = static_cast<TcgenCollectorOp>(255);
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = value;
  form->collector.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.locs = locs;
  EXPECT_TRUE(validateModule(*owned).has_value());

  auto omitted_ast = parse_mx_collector(
      mx_collector_source("tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale "
                          "[%d], [%a], %bd, %i, [%sa], [%sb], %p;"));
  ASSERT_TRUE(omitted_ast);
  auto omitted = resolveAndValidateModule(*omitted_ast);
  ASSERT_TRUE(omitted.has_value());
  auto* omitted_form = dynamic_cast<Tcgen05MmaMxf8f6f4*>(
      omitted->functions.front().body.front().get());
  ASSERT_TRUE(omitted_form);
  EXPECT_EQ(omitted_form->collector.value.buffer,
            TcgenCollectorBuffer::Unspecified);
  EXPECT_TRUE(omitted_form->collector.locs.empty());
  EXPECT_EQ(omitted_form->scale_vector_size.value,
            TcgenScaleVectorSize::Absent);
  omitted_form->collector.locs.push_back(omitted_form->cta_group.locs.front());
  EXPECT_FALSE(validateModule(*omitted).has_value());

  auto explicit_ast = parse_mx_collector(mx_collector_source(
      "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale"
      ".collector::a::discard [%d], [%a], %bd, %i, [%sa], [%sb], %p;"));
  ASSERT_TRUE(explicit_ast);
  auto explicit_owned = resolveAndValidateModule(*explicit_ast);
  ASSERT_TRUE(explicit_owned.has_value());
  auto* explicit_form = dynamic_cast<Tcgen05MmaMxf8f6f4*>(
      explicit_owned->functions.front().body.front().get());
  ASSERT_TRUE(explicit_form);
  EXPECT_EQ(explicit_form->collector.value.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(explicit_form->collector.value.operation,
            TcgenCollectorOp::Discard);
  EXPECT_EQ(explicit_form->collector.locs.size(), 1);
}

/** MX history report excludes the unrelated ashift/M facts. */
TEST(TcgenMmaMxACollector, IndependentHistory) {
  TcgenMxACollectorKnownFacts facts{
      .collector = {TcgenCollectorBuffer::A, TcgenCollectorOp::Use}};
  auto report = check_tcgen_mx_a_collector_known_facts(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(contains_mx(report.missing, TcgenACollectorObligation::History));
  EXPECT_TRUE(contains_mx(report.missing,
                          TcgenACollectorObligation::CollectorSequence));
  EXPECT_FALSE(contains_mx(report.missing, TcgenACollectorObligation::AshiftM));
  facts.collector_a_valid = false;
  EXPECT_TRUE(
      contains_mx(check_tcgen_mx_a_collector_known_facts(facts).violations,
                  TcgenACollectorViolation::History));
  facts.collector_a_valid = true;
  EXPECT_TRUE(contains_mx(check_tcgen_mx_a_collector_known_facts(facts).missing,
                          TcgenACollectorObligation::CollectorSequence));
  facts.collector = {};
  report = check_tcgen_mx_a_collector_known_facts(facts);
  EXPECT_EQ(report.effective.buffer, TcgenCollectorBuffer::A);
  EXPECT_EQ(report.effective.operation, TcgenCollectorOp::Discard);
  EXPECT_NE(report.effective, facts.collector);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
