#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/bfi/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfi/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfi/resolution.gen.hpp>
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

TEST(ResolveBfi, SelectsBothBitWidthsAndControlShapes) {
  for (const auto source :
       {"bfi.b32 %r0, 1, 2, 0, 8;", "bfi.b64 %rd0, %rd1, %rd2, 255, 255;",
        "bfi.b32 %r0, %r1, %r2, %r3, %r4;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Bfi>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_TRUE(std::holds_alternative<Bfi::B32>(resolved->variant) ||
                std::holds_alternative<Bfi::B64>(resolved->variant));
  }
  EXPECT_FALSE(resolve<Bfi>(parse_instruction("bfi.u32 %r0, %r1, %r2, 0, 8;"))
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedBfiAvailabilityAndImmediateRanges) {
  for (const auto source :
       {"bfi.b32 %r0, %r1, %r2, 0, 8;", "bfi.b32 %r0, %r1, %r2, 255, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfi = resolve<Bfi>(*ast);
    ASSERT_TRUE(bfi.has_value()) << bfi.error().message;
    const auto old_ptx =
        check(*bfi, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm =
        check(*bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        check(*bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
            .has_value());
  }
  for (const auto source :
       {"bfi.b32 %r0, %r1, %r2, 256, 8;", "bfi.b32 %r0, %r1, %r2, 8, 256;",
        "bfi.b32 %r0, %r1, %r2, -1, 8;", "bfi.b32 %r0, %r1, %r2, 8, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfi = resolve<Bfi>(*ast);
    ASSERT_TRUE(bfi.has_value()) << bfi.error().message;
    const auto checked =
        check(*bfi, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
