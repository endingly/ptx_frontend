#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/clz/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/clz/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/clz/resolution.gen.hpp>
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

TEST(ResolveClz, SelectsFrozenBitWidthVariantsAndRejectsUnfrozenType) {
  const auto b32 = resolve<Clz>(parse_instruction("clz.b32 %r0, 1;"));
  ASSERT_TRUE(b32.has_value()) << b32.error().message;
  EXPECT_NE(std::get_if<Clz::B32>(&b32->variant), nullptr);
  const auto b64 = resolve<Clz>(parse_instruction("clz.b64 %r0, %rd1;"));
  ASSERT_TRUE(b64.has_value()) << b64.error().message;
  EXPECT_NE(std::get_if<Clz::B64>(&b64->variant), nullptr);
  EXPECT_FALSE(
      selectVariant<Clz>(parse_instruction("clz.u32 %r0, %r1;")).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedClzAvailability) {
  for (const auto source : {"clz.b32 %r0, %r1;", "clz.b64 %r0, %rd1;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto clz = resolve<Clz>(*ast);
    ASSERT_TRUE(clz.has_value()) << clz.error().message;
    const auto old_ptx =
        check(*clz, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm =
        check(*clz, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        check(*clz, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
