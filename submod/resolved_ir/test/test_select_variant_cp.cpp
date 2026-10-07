#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
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

TEST(SelectVariantCp, SelectsAsyncMbarrierArriveForms) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected =
        select_variant_name(parse_instruction(source), cp_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("cp.async.mbarrier.arrive.b64 [%rd0];",
                 "AsyncMbarrierArriveGenericOrShared");
  expect_variant("cp.async.mbarrier.arrive.shared.b64 [shared_value];",
                 "AsyncMbarrierArriveGenericOrShared");
  expect_variant("cp.async.mbarrier.arrive.shared::cta.b64 [shared_value];",
                 "AsyncMbarrierArriveSharedCta");
  expect_variant("cp.async.mbarrier.arrive.noinc.b64 [%rd0];",
                 "AsyncMbarrierArriveNoincGenericOrShared");
  expect_variant(
      "cp.async.mbarrier.arrive.noinc.shared::cta.b64 [shared_value];",
      "AsyncMbarrierArriveNoincSharedCta");

  for (const std::string_view source : {
           "cp.async.mbarrier.arrive.shared::cluster.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b32 [%rd0];",
           "cp.async.mbarrier.arrive.noinc.noinc.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b64.noinc [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        select_variant_name(parse_instruction(source), cp_syntax_descriptor())
            .has_value());
  }
  EXPECT_FALSE(
      resolveCp(parse_instruction("cp.async.mbarrier.arrive.b64 [%rd0], 1;"))
          .has_value());
}

TEST(SelectVariantCp, SeparatesOriginalAndSourceControlledCopyByArity) {
  const auto original = select_variant_name(
      parse_instruction("cp.async.ca.shared.global [dst], [src], 4;"),
      cp_syntax_descriptor());
  ASSERT_TRUE(original.has_value());
  EXPECT_EQ(*original, "AsyncCaSharedGlobal");
  const auto extended = select_variant_name(
      parse_instruction("cp.async.ca.shared.global [dst], [src], 4, 2;"),
      cp_syntax_descriptor());
  ASSERT_TRUE(extended.has_value());
  EXPECT_EQ(*extended, "AsyncCaSharedGlobalControl");
  EXPECT_FALSE(
      select_variant_name(
          parse_instruction("cp.async.ca.shared.global [dst], [src], 4, 2, 1;"),
          cp_syntax_descriptor())
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
