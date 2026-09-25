#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/brev/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/brev/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/brev/resolution.gen.hpp>
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

TEST(ResolveBrev, SelectsBothBitWidths) {
  for (const auto source : {"brev.b32 %r0, 1;", "brev.b64 %rd0, %rd1;"}) {
    const auto brev = resolve<Brev>(parse_instruction(source));
    ASSERT_TRUE(brev.has_value()) << brev.error().message;
    EXPECT_TRUE(std::holds_alternative<Brev::B32>(brev->variant) ||
                std::holds_alternative<Brev::B64>(brev->variant));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedBrevAvailability) {
  PtxSyntaxParser parser("brev.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto brev = resolve<Brev>(*ast);
  ASSERT_TRUE(brev.has_value()) << brev.error().message;
  const auto old_ptx =
      check(*brev, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*brev, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*brev, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                           .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
