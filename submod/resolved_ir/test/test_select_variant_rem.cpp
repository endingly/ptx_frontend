#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/rem.gen.hpp>
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

TEST(ResolveRem, SelectsFrozenVariantsAndAcceptsZeroDivisor) {
  const auto s32 = resolveRem(parse_instruction("rem.s32 %r0, %r1, 0;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  const auto* signed_rem = dynamic_cast<RemS32*>(s32->get());
  ASSERT_NE(signed_rem, nullptr);
  EXPECT_EQ(RemS32::type, ScalarType::S32);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(signed_rem->src2.value));

  const auto u32 = resolveRem(parse_instruction("rem.u32 %r0, %r1, %r2;"));
  ASSERT_TRUE(u32.has_value()) << u32.error().message;
  ASSERT_NE(dynamic_cast<RemU32*>(u32->get()), nullptr);
  EXPECT_EQ(RemU32::type, ScalarType::U32);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedRemAvailability) {
  PtxSyntaxParser parser("rem.s32 %r0, %r1, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto rem = resolveRem(*ast);
  ASSERT_TRUE(rem.has_value()) << rem.error().message;
  const auto rejected =
      (*rem)->check(Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                            .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      (*rem)
          ->check(Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
