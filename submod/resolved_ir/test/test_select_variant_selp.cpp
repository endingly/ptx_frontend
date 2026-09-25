#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp/resolution.gen.hpp>
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

TEST(ResolveSelp, SelectsFrozenU32Variant) {
  const auto ast = parse_instruction("selp.u32 %r0, %r1, 0, %p0;");
  const auto resolved = resolve<Selp>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* selp = std::get_if<Selp::U32>(&resolved->variant);
  ASSERT_NE(selp, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(selp->src_false.value));
  EXPECT_FALSE(std::get<ResolvedPredicate>(selp->predicate.value).negated);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedSelpU32Availability) {
  PtxSyntaxParser parser("selp.u32 %r0, %r1, %r2, %p0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto selp = resolve<Selp>(*ast);
  ASSERT_TRUE(selp.has_value()) << selp.error().message;
  const auto rejected =
      check(*selp, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*selp, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                           .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
