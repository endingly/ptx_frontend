#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/brev.gen.hpp>
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
    const auto brev = resolveBrev(parse_instruction(source));
    ASSERT_TRUE(brev.has_value()) << brev.error().message;
    EXPECT_TRUE((dynamic_cast<BrevB32*>(brev->get()) != nullptr) ||
                (dynamic_cast<BrevB64*>(brev->get()) != nullptr));
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
  const auto brev = resolveBrev(*ast);
  ASSERT_TRUE(brev.has_value()) << brev.error().message;
  const auto old_ptx = (*brev)->check(
      Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = (*brev)->check(
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*brev)
          ->check(Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
