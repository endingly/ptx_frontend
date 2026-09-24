#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/shf/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/shf/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/shf/checker.gen.hpp>
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

TEST(ResolveShf, SelectsEveryDirectionAndModeVariant) {
  const auto left =
      resolve<Shf>(parse_instruction("shf.l.clamp.b32 %r0, %r1, %r2, 8;"));
  ASSERT_TRUE(left.has_value()) << left.error().message;
  ASSERT_NE(std::get_if<Shf::LClampB32>(&left->variant), nullptr);
  const auto right =
      resolve<Shf>(parse_instruction("shf.r.wrap.b32 %r0, %r1, %r2, %r3;"));
  ASSERT_TRUE(right.has_value()) << right.error().message;
  ASSERT_NE(std::get_if<Shf::RWrapB32>(&right->variant), nullptr);
  const auto left_wrap =
      resolve<Shf>(parse_instruction("shf.l.wrap.b32 %r0, 1, %r2, 32;"));
  ASSERT_TRUE(left_wrap.has_value()) << left_wrap.error().message;
  ASSERT_NE(std::get_if<Shf::LWrapB32>(&left_wrap->variant), nullptr);
  const auto right_clamp =
      resolve<Shf>(parse_instruction("shf.r.clamp.b32 %r0, %r1, 2, 33;"));
  ASSERT_TRUE(right_clamp.has_value()) << right_clamp.error().message;
  ASSERT_NE(std::get_if<Shf::RClampB32>(&right_clamp->variant), nullptr);
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
    const auto shf = resolve<Shf>(*ast);
    ASSERT_TRUE(shf.has_value()) << shf.error().message;
    const auto old_ptx =
        check(*shf, Context{.target = {.ptx_version = {3, 0}, .sm_version = 32},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm =
        check(*shf, Context{.target = {.ptx_version = {3, 1}, .sm_version = 31},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        check(*shf, Context{.target = {.ptx_version = {3, 1}, .sm_version = 32},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
