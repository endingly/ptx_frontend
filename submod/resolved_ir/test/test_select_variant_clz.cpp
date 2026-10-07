#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/clz.gen.hpp>
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
  const auto b32 = resolveClz(parse_instruction("clz.b32 %r0, 1;"));
  ASSERT_TRUE(b32.has_value()) << b32.error().message;
  EXPECT_NE(dynamic_cast<ClzB32*>(b32->get()), nullptr);
  const auto b64 = resolveClz(parse_instruction("clz.b64 %r0, %rd1;"));
  ASSERT_TRUE(b64.has_value()) << b64.error().message;
  EXPECT_NE(dynamic_cast<ClzB64*>(b64->get()), nullptr);
  EXPECT_FALSE(select_variant_name(parse_instruction("clz.u32 %r0, %r1;"),
                                   clz_syntax_descriptor())
                   .has_value());
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
    const auto clz = resolveClz(*ast);
    ASSERT_TRUE(clz.has_value()) << clz.error().message;
    const auto old_ptx = (*clz)->check(
        Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm = (*clz)->check(
        Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        (*clz)
            ->check(Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
