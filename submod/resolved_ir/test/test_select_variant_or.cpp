#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/or/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/or/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/or/checker.gen.hpp>
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

TEST(ResolveOr, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("or.b32 %r0, %r1, 1;");
  const auto resolved = resolve<Or>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* or_b32 = std::get_if<Or::B32>(&resolved->variant);
  ASSERT_NE(or_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(or_b32->src2.value));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedOrB32Availability) {
  PtxSyntaxParser parser("or.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto or_instruction = resolve<Or>(*ast);
  ASSERT_TRUE(or_instruction.has_value()) << or_instruction.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*or_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE(check(*or_instruction, supported_target).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
