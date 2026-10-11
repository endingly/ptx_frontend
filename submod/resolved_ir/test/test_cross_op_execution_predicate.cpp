#include <gtest/gtest.h>

#include <expected>
#include <memory>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/ret.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::checker {
namespace {

const SourceRange kInstructionRange{{4, 3}, {4, 17}};

/** Resolve a guarded instruction through the active exact-class dispatch. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolve_guarded(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value())
      << (ast.diagnostics.empty() ? "PTX source did not parse."
                                  : ast.diagnostics.front().message);
  if (!ast)
    return std::unexpected(ResolveDiagnostic{});
  return resolveInstruction(*ast);
}

/** Execution guards are checked independently of an instruction's operands. */
TEST(ResolvedIrChecker, RevalidatesExecutionPredicateMetadata) {
  const Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = kInstructionRange,
  };
  constexpr std::string_view mov_source = "@%p0 mov.b32 %r0, {%h0, %h1};";
  const Context mov_context{
      .target = context.target,
      .instruction_range = {{1, 1},
                            {1, static_cast<int32_t>(mov_source.size() + 1)}}};
  auto mov = resolve_guarded(mov_source);
  ASSERT_TRUE(mov.has_value()) << mov.error().message;
  auto& mov_value = **mov;
  ASSERT_TRUE(mov_value.execution_predicate.has_value());
  ASSERT_FALSE(mov_value.execution_predicate->locs.empty());
  const SourceRange guard_range = mov_value.execution_predicate->locs.front();
  EXPECT_TRUE(mov_value.check(mov_context).has_value());
  mov_value.execution_predicate->value.register_ref.register_class =
      ResolvedRegisterClass::General;
  const auto invalid_class = mov_value.check(mov_context);
  ASSERT_FALSE(invalid_class.has_value());
  EXPECT_EQ(invalid_class.error().front().kind,
            CheckDiagnosticKind::InvalidExecutionPredicate);
  EXPECT_EQ(invalid_class.error().front().range, guard_range);

  auto ret = resolve_guarded("@!%p0 ret;");
  ASSERT_TRUE(ret.has_value()) << ret.error().message;
  auto& ret_value = **ret;
  ASSERT_TRUE(ret_value.execution_predicate.has_value());
  EXPECT_TRUE(ret_value.execution_predicate->value.negated);
  ret_value.execution_predicate->value.register_ref.declared_type =
      ScalarType::U32;
  const auto invalid_type = ret_value.check(context);
  ASSERT_FALSE(invalid_type.has_value());
  EXPECT_EQ(invalid_type.error().front().kind,
            CheckDiagnosticKind::InvalidExecutionPredicate);

  auto vector_guard = resolve_guarded("@%p0 ret;");
  ASSERT_TRUE(vector_guard.has_value()) << vector_guard.error().message;
  auto& vector_ret = **vector_guard;
  ASSERT_TRUE(vector_ret.execution_predicate.has_value());
  vector_ret.execution_predicate->value.register_ref.vector_width = 2;
  vector_ret.execution_predicate->locs.clear();
  const auto invalid_vector = vector_ret.check(context);
  ASSERT_FALSE(invalid_vector.has_value());
  EXPECT_EQ(invalid_vector.error().front().kind,
            CheckDiagnosticKind::InvalidExecutionPredicate);
  EXPECT_EQ(invalid_vector.error().front().range, kInstructionRange);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
