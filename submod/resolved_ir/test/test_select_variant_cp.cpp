#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/cp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp/checker.gen.hpp>
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

TEST(SelectVariantCp, SelectsAsyncMbarrierArriveForms) {
  const auto expect_variant = [](std::string_view source,
                                 Cp::VariantType expected) {
    const auto selected = selectVariant<Cp>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("cp.async.mbarrier.arrive.b64 [%rd0];",
                 Cp::VariantType::AsyncMbarrierArriveGenericOrShared);
  expect_variant("cp.async.mbarrier.arrive.shared.b64 [shared_value];",
                 Cp::VariantType::AsyncMbarrierArriveGenericOrShared);
  expect_variant("cp.async.mbarrier.arrive.shared::cta.b64 [shared_value];",
                 Cp::VariantType::AsyncMbarrierArriveSharedCta);
  expect_variant("cp.async.mbarrier.arrive.noinc.b64 [%rd0];",
                 Cp::VariantType::AsyncMbarrierArriveNoincGenericOrShared);
  expect_variant(
      "cp.async.mbarrier.arrive.noinc.shared::cta.b64 [shared_value];",
      Cp::VariantType::AsyncMbarrierArriveNoincSharedCta);

  for (const std::string_view source : {
           "cp.async.mbarrier.arrive.shared::cluster.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b32 [%rd0];",
           "cp.async.mbarrier.arrive.noinc.noinc.b64 [%rd0];",
           "cp.async.mbarrier.arrive.b64.noinc [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Cp>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Cp>(parse_instruction("cp.async.mbarrier.arrive.b64 [%rd0], 1;"))
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
