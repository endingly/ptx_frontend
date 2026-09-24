#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/rem/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rem/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rem/checker.gen.hpp>
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

TEST(ResolveRem, SelectsFrozenVariantsAndAcceptsZeroDivisor) {
  const auto s32 = resolve<Rem>(parse_instruction("rem.s32 %r0, %r1, 0;"));
  ASSERT_TRUE(s32.has_value()) << s32.error().message;
  const auto* signed_rem = std::get_if<Rem::S32>(&s32->variant);
  ASSERT_NE(signed_rem, nullptr);
  EXPECT_EQ(Rem::S32::type, ScalarType::S32);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(signed_rem->src2.value));

  const auto u32 = resolve<Rem>(parse_instruction("rem.u32 %r0, %r1, %r2;"));
  ASSERT_TRUE(u32.has_value()) << u32.error().message;
  ASSERT_NE(std::get_if<Rem::U32>(&u32->variant), nullptr);
  EXPECT_EQ(Rem::U32::type, ScalarType::U32);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedRemAvailability) {
  PtxSyntaxParser parser("rem.s32 %r0, %r1, 0;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto rem = resolve<Rem>(*ast);
  ASSERT_TRUE(rem.has_value()) << rem.error().message;
  const auto rejected =
      check(*rem, Context{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                          .instruction_range = ast->range});
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_TRUE(
      check(*rem, Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                          .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
