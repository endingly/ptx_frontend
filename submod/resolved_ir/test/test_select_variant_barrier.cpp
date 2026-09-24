#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
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

TEST(SelectVariantBarrier, SelectsClusterArriveAndWaitForms) {
  const auto expect_variant = [](std::string_view source,
                                 Barrier::VariantType expected) {
    const auto selected = selectVariant<Barrier>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  for (const std::string_view source : {
           "barrier.cluster.arrive;",
           "barrier.cluster.arrive.aligned;",
           "barrier.cluster.arrive.release.aligned;",
           "barrier.cluster.arrive.relaxed;",
       }) {
    expect_variant(source, Barrier::VariantType::ClusterArrive);
  }
  for (const std::string_view source : {
           "barrier.cluster.wait;",
           "barrier.cluster.wait.aligned;",
           "barrier.cluster.wait.acquire.aligned;",
       }) {
    expect_variant(source, Barrier::VariantType::ClusterWait);
  }

  for (const std::string_view source : {
           "barrier.cluster.arrive.acquire;",
           "barrier.cluster.wait.release;",
           "barrier.cluster.arrive.aligned.release;",
       }) {
    const auto selected = selectVariant<Barrier>(parse_instruction(source));
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
