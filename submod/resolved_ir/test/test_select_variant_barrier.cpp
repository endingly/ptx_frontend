#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

/** Select standalone CTA and cluster barriers without aliasing their forms. */
TEST(SelectVariantBarrier, SelectsCtaAndClusterForms) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected = select_variant_name(parse_instruction(source),
                                              barrier_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  for (const std::string_view source : {
           "barrier.sync 0;",
           "barrier.sync.aligned %r0, 32;",
       }) {
    expect_variant(source, "Sync");
  }
  for (const std::string_view source : {
           "barrier.cta.sync 0;",
           "barrier.cta.sync.aligned %r0, %r1;",
       }) {
    expect_variant(source, "CtaSync");
  }
  for (const std::string_view source : {
           "barrier.arrive 0, 32;",
           "barrier.arrive.aligned %r0, %r1;",
       }) {
    expect_variant(source, "Arrive");
  }
  for (const std::string_view source : {
           "barrier.cta.arrive 15, 64;",
           "barrier.cta.arrive.aligned %r0, 32;",
       }) {
    expect_variant(source, "CtaArrive");
  }
  for (const auto& [source, expected] :
       std::array<std::pair<std::string_view, std::string_view>, 12>{{
           {"barrier.red.popc.u32 %r0, 0, %p0;", "RedPopcU32"},
           {"barrier.red.popc.aligned.u32 %r0, 15, 32, !%p0;", "RedPopcU32"},
           {"barrier.cta.red.popc.u32 %r0, 0, %p0;", "CtaRedPopcU32"},
           {"barrier.cta.red.popc.aligned.u32 %r0, %r1, %r2, !%p0;",
            "CtaRedPopcU32"},
           {"barrier.red.and.pred %p0, 0, %p1;", "RedAndPred"},
           {"barrier.red.and.aligned.pred %p0, 15, 32, !%p1;", "RedAndPred"},
           {"barrier.cta.red.and.pred %p0, 0, %p1;", "CtaRedAndPred"},
           {"barrier.cta.red.and.aligned.pred %p0, %r1, %r2, !%p1;",
            "CtaRedAndPred"},
           {"barrier.red.or.pred %p0, 0, %p1;", "RedOrPred"},
           {"barrier.red.or.aligned.pred %p0, 15, 32, !%p1;", "RedOrPred"},
           {"barrier.cta.red.or.pred %p0, 0, %p1;", "CtaRedOrPred"},
           {"barrier.cta.red.or.aligned.pred %p0, %r1, %r2, !%p1;",
            "CtaRedOrPred"},
       }}) {
    expect_variant(source, expected);
  }
  for (const std::string_view source : {
           "barrier.cluster.arrive;",
           "barrier.cluster.arrive.aligned;",
           "barrier.cluster.arrive.release.aligned;",
           "barrier.cluster.arrive.relaxed;",
       }) {
    expect_variant(source, "ClusterArrive");
  }
  for (const std::string_view source : {
           "barrier.cluster.wait;",
           "barrier.cluster.wait.aligned;",
           "barrier.cluster.wait.acquire.aligned;",
       }) {
    expect_variant(source, "ClusterWait");
  }

  for (const std::string_view source : {
           "barrier.sync.aligned.sync 0;",
           "barrier.sync.aligned.aligned 0;",
           "barrier.cta.sync.sync 0;",
           "barrier.arrive.aligned.arrive 0, 32;",
           "barrier.arrive.aligned.aligned 0, 32;",
           "barrier.cta.arrive.arrive 0, 32;",
           "barrier.red.popc.u32.aligned %r0, 0, %p0;",
           "barrier.red.and.pred.aligned %p0, 0, %p1;",
           "barrier.red.or.or.pred %p0, 0, %p1;",
           "barrier.cta.red.popc.aligned.aligned.u32 %r0, 0, %p0;",
           "barrier.cluster.red.popc.u32 %r0, 0, %p0;",
           "barrier.cluster.arrive.acquire;",
           "barrier.cluster.wait.release;",
           "barrier.cluster.arrive.aligned.release;",
       }) {
    const auto selected = select_variant_name(parse_instruction(source),
                                              barrier_syntax_descriptor());
    EXPECT_FALSE(selected.has_value()) << source;
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
