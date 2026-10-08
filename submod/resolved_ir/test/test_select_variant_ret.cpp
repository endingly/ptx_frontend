#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/control_flow/ret.gen.hpp>
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

TEST(ResolveRet, SelectsBareVariantAndRejectsModifiersAndOperands) {
  const auto bare_ast = parse_instruction("ret;");
  const auto bare = resolveRet(bare_ast);
  ASSERT_TRUE(bare.has_value()) << bare.error().message;
  EXPECT_TRUE((dynamic_cast<RetBare*>(bare->get()) != nullptr));

  const auto modifier_ast = parse_instruction("ret.uni;");
  const auto modifier = resolveRet(modifier_ast);
  ASSERT_FALSE(modifier.has_value());
  EXPECT_EQ(modifier.error().range,
            modifier_ast.modifiers.front().syntax.range);
  EXPECT_EQ(modifier.error().message, "Unknown modifier '.uni'.");

  const auto operand_ast = parse_instruction("ret %r0;");
  const auto operand = resolveRet(operand_ast);
  ASSERT_FALSE(operand.has_value());
  EXPECT_EQ(operand.error().range, operand_ast.range);
  EXPECT_EQ(operand.error().message,
            "Operands do not match any layout of instruction variant 'Bare'.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

TEST(ResolvedIrChecker, ChecksGeneratedBareRetAvailability) {
  PtxSyntaxParser parser("ret;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto ret = resolveRet(*ast);
  ASSERT_TRUE(ret.has_value()) << ret.error().message;

  const Context old_target{
      .target = {.ptx_version = {0, 9}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  const auto unavailable = (*ret)->check(old_target);
  ASSERT_FALSE(unavailable.has_value());
  ASSERT_EQ(unavailable.error().size(), 1u);
  EXPECT_EQ(unavailable.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  EXPECT_EQ(unavailable.error().front().range, ast->range);

  const Context supported_target{
      .target = {.ptx_version = {1, 0}, .sm_version = 0},
      .instruction_range = ast->range,
  };
  EXPECT_TRUE((*ret)->check(supported_target).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
