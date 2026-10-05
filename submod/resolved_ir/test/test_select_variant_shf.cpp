#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/shf.gen.hpp>
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

TEST(ResolveShf, SelectsEveryDirectionAndModeVariant) {
  const auto left =
      resolveShf(parse_instruction("shf.l.clamp.b32 %r0, %r1, %r2, 8;"));
  ASSERT_TRUE(left.has_value()) << left.error().message;
  ASSERT_NE(dynamic_cast<ShfLClampB32*>(left->get()), nullptr);
  const auto right =
      resolveShf(parse_instruction("shf.r.wrap.b32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(right.has_value()) << right.error().message;
  ASSERT_NE(dynamic_cast<ShfRWrapB32*>(right->get()), nullptr);
  const auto left_wrap =
      resolveShf(parse_instruction("shf.l.wrap.b32 %r0, 1, %r2, 32;"));
  ASSERT_TRUE(left_wrap.has_value()) << left_wrap.error().message;
  ASSERT_NE(dynamic_cast<ShfLWrapB32*>(left_wrap->get()),
            nullptr);
  const auto right_clamp =
      resolveShf(parse_instruction("shf.r.clamp.b32 %r0, %r1, 2, 33;"));
  ASSERT_TRUE(right_clamp.has_value()) << right_clamp.error().message;
  ASSERT_NE(dynamic_cast<ShfRClampB32*>(right_clamp->get()),
            nullptr);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedShfAvailability) {
  for (const auto source : {"shf.l.clamp.b32 %r0, %r1, %r2, 8;",
                            "shf.l.wrap.b32 %r0, %r1, %r2, 32;",
                            "shf.r.clamp.b32 %r0, %r1, %r2, 33;",
                            "shf.r.wrap.b32 %r0, %r1, %r2, %r3;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto shf = resolveShf(*ast);
    ASSERT_TRUE(shf.has_value()) << shf.error().message;
    const auto old_ptx =
        (*shf)->check( Context{.target = {.ptx_version = {3, 0}, .sm_version = 32},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm =
        (*shf)->check( Context{.target = {.ptx_version = {3, 1}, .sm_version = 31},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        (*shf)->check( Context{.target = {.ptx_version = {3, 1}, .sm_version = 32},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
