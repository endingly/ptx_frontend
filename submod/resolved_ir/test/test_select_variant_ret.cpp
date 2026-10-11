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

/** Uniformity and execution guards remain independent on the canonical class. */
TEST(ResolveRet, PreservesUniformityAndExecutionGuards) {
  for (const auto source : {"ret;", "ret.uni;", "@%p0 ret;", "@%p0 ret.uni;",
                            "@!%p0 ret;", "@!%p0 ret.uni;"}) {
    SCOPED_TRACE(source);
    const auto ast = parse_instruction(source);
    const auto resolved = resolveRet(ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto* ret = dynamic_cast<const RetBare*>(resolved->get());
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->uni.value, !ast.modifiers.empty());
    ASSERT_EQ(ret->uni.locs.size(), ast.modifiers.size());
    if (!ast.modifiers.empty())
      EXPECT_EQ(ret->uni.locs.front(), ast.modifiers.front().syntax.range);
    ASSERT_EQ(ret->execution_predicate.has_value(), ast.predicate.has_value());
    if (ast.predicate) {
      EXPECT_EQ(ret->execution_predicate->value.negated,
                ast.predicate->negated);
      ASSERT_EQ(ret->execution_predicate->locs.size(), 1u);
      EXPECT_EQ(ret->execution_predicate->locs.front(), ast.predicate->range);
    }
    const checker::Context context{
        .target = {.ptx_version = {1, 0}, .sm_version = 0},
        .instruction_range = ast.range,
    };
    EXPECT_TRUE(ret->check(context).has_value());
  }
}

/** Unknown and repeated suffixes, and all operands, remain invalid. */
TEST(ResolveRet, RejectsInvalidModifiersAndOperands) {
  const auto modifier_ast = parse_instruction("ret.foo;");
  const auto modifier = resolveRet(modifier_ast);
  ASSERT_FALSE(modifier.has_value());
  EXPECT_EQ(modifier.error().range,
            modifier_ast.modifiers.front().syntax.range);
  EXPECT_EQ(modifier.error().message, "Unknown modifier '.foo'.");

  const auto duplicate_ast = parse_instruction("ret.uni.uni;");
  const auto duplicate = resolveRet(duplicate_ast);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error().range,
            duplicate_ast.modifiers.back().syntax.range);

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
  for (const auto source : {"ret;", "ret.uni;"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
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
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
