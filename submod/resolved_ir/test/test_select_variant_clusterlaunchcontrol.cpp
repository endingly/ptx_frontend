#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/clusterlaunchcontrol.gen.hpp>
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

TEST(SelectVariantClusterlaunchcontrol, SelectsTryCancelAsyncForms) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected =
        select_variant_name(parse_instruction(source), clusterlaunchcontrol_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_variant(
      "clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 "
      "[%rd0], [%rd1];",
      "TryCancelAsyncGeneric");
  expect_variant(
      "clusterlaunchcontrol.try_cancel.async.shared::cta.mbarrier::complete_tx:"
      ":bytes.b128 [%rd0], [%rd1];",
      "TryCancelAsyncSharedCta");
  expect_variant(
      "clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes."
      "multicast::cluster::all.b128 [%rd0], [%rd1];",
      "TryCancelAsyncMulticastGeneric");
  expect_variant(
      "clusterlaunchcontrol.try_cancel.async.shared::cta.mbarrier::complete_tx:"
      ":bytes.multicast::cluster::all.b128 [%rd0], [%rd1];",
      "TryCancelAsyncMulticastSharedCta");

  for (const std::string_view source : {
           "clusterlaunchcontrol.try_cancel.async.b128 [%rd0], [%rd1];",
           "clusterlaunchcontrol.try_cancel.mbarrier::complete_tx::bytes.async."
           "b128 [%rd0], [%rd1];",
           "clusterlaunchcontrol.try_cancel.async.shared::cta.multicast::"
           "cluster::all.mbarrier::complete_tx::bytes.b128 [%rd0], [%rd1];",
           "clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes."
           "b32 [%rd0], [%rd1];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source), clusterlaunchcontrol_syntax_descriptor())
                     .has_value());
  }
}

TEST(SelectVariantClusterlaunchcontrol, SelectsQueryCancelForms) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected =
        select_variant_name(parse_instruction(source), clusterlaunchcontrol_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };
  expect_variant(
      "clusterlaunchcontrol.query_cancel.is_canceled.pred.b128 %p0, %q0;",
      "QueryCancelIsCanceledPred");
  expect_variant(
      "clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b32.b128 {%r0, "
      "%r1, %r2, _}, %q0;",
      "QueryCancelGetFirstCtaidV4");
  expect_variant(
      "clusterlaunchcontrol.query_cancel.get_first_ctaid::x.b32.b128 %r0, %q0;",
      "QueryCancelGetFirstCtaidX");
  expect_variant(
      "clusterlaunchcontrol.query_cancel.get_first_ctaid::y.b32.b128 %r0, %q0;",
      "QueryCancelGetFirstCtaidY");
  expect_variant(
      "clusterlaunchcontrol.query_cancel.get_first_ctaid::z.b32.b128 %r0, %q0;",
      "QueryCancelGetFirstCtaidZ");

  for (const std::string_view source : {
           "clusterlaunchcontrol.query_cancel.is_canceled.b128 %p0, %q0;",
           "clusterlaunchcontrol.query_cancel.get_first_ctaid::w.b32.b128 %r0, "
           "%q0;",
           "clusterlaunchcontrol.query_cancel.get_first_ctaid.b32.v4.b128 "
           "{%r0, %r1, %r2, _}, %q0;",
           "clusterlaunchcontrol.query_cancel.get_first_ctaid.v4.b64.b128 "
           "{%r0, %r1, %r2, _}, %q0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source), clusterlaunchcontrol_syntax_descriptor())
                     .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
