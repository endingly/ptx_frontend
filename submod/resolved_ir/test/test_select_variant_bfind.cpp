#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/bfind/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfind/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfind/checker.gen.hpp>
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

TEST(ResolveBfind, SelectsEveryTypeAndShiftAmountForm) {
  for (const auto source : {
           "bfind.u32 %r0, 1;",
           "bfind.u64 %r0, %rd1;",
           "bfind.s32 %r0, %r1;",
           "bfind.s64 %r0, %rd1;",
           "bfind.shiftamt.u32 %r0, %r1;",
           "bfind.shiftamt.u64 %r0, %rd1;",
           "bfind.shiftamt.s32 %r0, %r1;",
           "bfind.shiftamt.s64 %r0, %rd1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(resolve<Bfind>(parse_instruction(source)).has_value());
  }
  EXPECT_TRUE(Bfind::ShiftamtU32::shiftamt);
  EXPECT_FALSE(selectVariant<Bfind>(parse_instruction("bfind.b32 %r0, %r1;"))
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedBfindAvailability) {
  PtxSyntaxParser parser("bfind.shiftamt.u32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto bfind = resolve<Bfind>(*ast);
  ASSERT_TRUE(bfind.has_value()) << bfind.error().message;
  const auto old_ptx =
      check(*bfind, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*bfind, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                            .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*bfind, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
