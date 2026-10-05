#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/popc.gen.hpp>
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
    const auto popc = resolvePopc(parse_instruction(source));
    ASSERT_TRUE(popc.has_value()) << popc.error().message;
    EXPECT_TRUE(dynamic_cast<PopcB32*>(popc->get()) != nullptr ||
                dynamic_cast<PopcB64*>(popc->get()) != nullptr);
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
  const auto popc = resolvePopc(*ast);
  ASSERT_TRUE(popc.has_value()) << popc.error().message;
  const auto old_ptx =
      (*popc)->check(Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      (*popc)->check(Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*popc)->check(Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                           .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
