#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/bfe/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfe/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/bfe/resolution.gen.hpp>
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

TEST(ResolveBfe, SelectsAllIntegerWidthsAndControlShapes) {
  for (const auto source :
       {"bfe.u32 %r0, 1, 0, 8;", "bfe.u64 %rd0, %rd1, 255, 255;",
        "bfe.s32 %r0, %r1, %r2, 8;", "bfe.s64 %rd0, %rd1, 8, %r2;"}) {
    SCOPED_TRACE(source);
    const auto resolved = resolve<Bfe>(parse_instruction(source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_TRUE(std::holds_alternative<Bfe::U32>(resolved->variant) ||
                std::holds_alternative<Bfe::U64>(resolved->variant) ||
                std::holds_alternative<Bfe::S32>(resolved->variant) ||
                std::holds_alternative<Bfe::S64>(resolved->variant));
  }
  EXPECT_FALSE(
      resolve<Bfe>(parse_instruction("bfe.b32 %r0, %r1, 0, 8;")).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedBfeAvailabilityAndImmediateRanges) {
  for (const auto source :
       {"bfe.u32 %r0, %r1, 0, 8;", "bfe.u32 %r0, %r1, 255, 255;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfe = resolve<Bfe>(*ast);
    ASSERT_TRUE(bfe.has_value()) << bfe.error().message;
    const auto old_ptx =
        check(*bfe, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_ptx.has_value());
    EXPECT_EQ(old_ptx.error().front().kind,
              CheckDiagnosticKind::UnsupportedPtxVersion);
    const auto old_sm =
        check(*bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                            .instruction_range = ast->range});
    ASSERT_FALSE(old_sm.has_value());
    EXPECT_EQ(old_sm.error().front().kind,
              CheckDiagnosticKind::UnsupportedSmVersion);
    EXPECT_TRUE(
        check(*bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range})
            .has_value());
  }

  for (const auto source :
       {"bfe.u32 %r0, %r1, 256, 8;", "bfe.u32 %r0, %r1, 8, 256;",
        "bfe.u32 %r0, %r1, -1, 8;", "bfe.u32 %r0, %r1, 8, -1;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto bfe = resolve<Bfe>(*ast);
    ASSERT_TRUE(bfe.has_value()) << bfe.error().message;
    const auto checked =
        check(*bfe, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                            .instruction_range = ast->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
