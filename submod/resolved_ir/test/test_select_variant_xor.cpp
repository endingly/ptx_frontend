#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/xor/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/xor/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/xor/resolution.gen.hpp>
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

TEST(ResolveXor, SelectsB32VariantAndAcceptsImmediateSource) {
  const auto ast = parse_instruction("xor.b32 %r0, %r1, 1;");
  const auto resolved = resolve<Xor>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* xor_b32 = std::get_if<Xor::B32>(&resolved->variant);
  ASSERT_NE(xor_b32, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(xor_b32->src2.value));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedXorB32Availability) {
  PtxSyntaxParser parser("xor.b32 %r0, %r1, %r2;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto xor_instruction = resolve<Xor>(*ast);
  ASSERT_TRUE(xor_instruction.has_value()) << xor_instruction.error().message;
  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = check(*xor_instruction, old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);
  EXPECT_TRUE(check(*xor_instruction,
                    Context{.target = {.ptx_version = {1, 0}, .sm_version = 0},
                            .instruction_range = ast->range})
                  .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
