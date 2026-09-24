#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/not/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/not/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/not/checker.gen.hpp>
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

TEST(ResolveNot, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("not.b32 %r0, 1;");
  const auto resolved = resolve<Not>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* not_b32 = std::get_if<Not::B32>(&resolved->variant);
  ASSERT_NE(not_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(not_b32->src.value));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedNotB32Availability) {
  PtxSyntaxParser parser("not.b32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto not_instruction = resolve<Not>(*ast);
  ASSERT_TRUE(not_instruction.has_value()) << not_instruction.error().message;
  const Context old_target{.target = {.ptx_version = {0, 9}, .sm_version = 0},
                           .instruction_range = ast->range};
  const auto unavailable = check(*not_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);
  EXPECT_TRUE(check(*not_instruction,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
