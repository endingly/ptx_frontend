#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/shl.gen.hpp>
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

TEST(ResolveShl, SelectsB32VariantAndAcceptsImmediateAmount) {
  const auto ast = parse_instruction("shl.b32 %r0, %r1, 1;");
  const auto resolved = resolveShl(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* shl_b32 = dynamic_cast<ShlB32*>(resolved->get());
  ASSERT_NE(shl_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(
      shl_b32->amount.value));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedShlB32Availability) {
  PtxSyntaxParser parser("shl.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto shl = resolveShl(*ast);
  ASSERT_TRUE(shl.has_value()) << shl.error().message;
  const auto rejected =
      (*shl)->check( Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(rejected.error().front().range, ast->range);
  EXPECT_TRUE(
      (*shl)->check( Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
