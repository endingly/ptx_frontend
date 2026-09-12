#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string_view>

namespace ptx_frontend::resolved_ir {
namespace {

/** Mutated public vector widths must be rejected before projection writes. */
TEST(CheckerProjectionSafety, RejectsMutatedVectorRegisterWidths) {
  PtxSyntaxParser parser(
      ".entry k() { .reg .v4 .b32 %r0; mov.v4.u32 %r0, %clusterid; }");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value());
  ASSERT_TRUE(ast.diagnostics.empty());
  auto module = resolveModule(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  ASSERT_FALSE(module->functions.empty());
  ASSERT_FALSE(module->functions.front().body.empty());
  auto* instruction = std::get_if<Mov>(&module->functions.front().body.front());
  ASSERT_NE(instruction, nullptr);
  auto* primitive = std::get_if<Mov::V4U32>(&instruction->variant);
  ASSERT_NE(primitive, nullptr);
  constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 90,
                 .capabilities = capabilities},
      .instruction_range = ast->range,
  };
  for (const uint8_t width : {0, 4, 65, 255}) {
    SCOPED_TRACE(static_cast<unsigned>(width));
    primitive->dst.value.register_ref.vector_width = width;
    const auto result = checker::check(*instruction, context);
    if (width == 4) {
      EXPECT_TRUE(result.has_value());
    } else {
      ASSERT_FALSE(result.has_value());
      ASSERT_FALSE(result.error().empty());
      EXPECT_EQ(result.error().front().kind,
                checker::CheckDiagnosticKind::InvalidVectorOperand);
      ASSERT_FALSE(primitive->dst.locs.empty());
      EXPECT_EQ(result.error().front().range, primitive->dst.locs.front());
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
