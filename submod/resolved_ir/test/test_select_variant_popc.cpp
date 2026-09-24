#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/popc/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/popc/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/popc/resolution.gen.hpp>
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

TEST(ResolvePopc, SelectsBothBitWidths) {
  for (const auto source : {"popc.b32 %r0, 1;", "popc.b64 %r0, %rd1;"}) {
    const auto popc = resolve<Popc>(parse_instruction(source));
    ASSERT_TRUE(popc.has_value()) << popc.error().message;
    EXPECT_TRUE(std::holds_alternative<Popc::B32>(popc->variant) ||
                std::holds_alternative<Popc::B64>(popc->variant));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedPopcAvailability) {
  PtxSyntaxParser parser("popc.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto popc = resolve<Popc>(*ast);
  ASSERT_TRUE(popc.has_value()) << popc.error().message;
  const auto old_ptx =
      check(*popc, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*popc, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*popc, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                           .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
