#include <gtest/gtest.h>
#include "test_instruction_access.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/selp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/exit.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/ret.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/trap.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a standalone instruction for global-dispatch contract tests. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

/** Construct a malformed indirect call target without parser validation. */
syntax_ast::AstInstruction indirect_metadata_instruction(std::string spelling) {
  const SourceRange range{{1, 1}, {1, 1}};
  syntax_ast::AstInstruction ast{
      .opcode = syntax_ast::AstOpcode{.syntax = {"call", range}},
      .range = range,
  };
  ast.operands.emplace_back(syntax_ast::AstCallTargetSet{
      .name =
          syntax_ast::AstIdentifierRef{.syntax = {std::move(spelling), range}},
      .range = range,
  });
  return ast;
}

TEST(ResolveLogicAndShift, SelectsExpandedWidths) {
  for (const auto source : {
           "and.pred %p0, !%p1, 1;",
           "or.b16 %h0, %h1, 1;",
           "xor.b64 %rd0, %rd1, 1;",
           "not.b16 %h0, %h1;",
           "cnot.b64 %rd0, 0;",
           "shl.b64 %rd0, %rd1, 64;",
           "shr.b16 %h0, %h1, 16;",
           "shr.u64 %rd0, %rd1, 1;",
           "shr.s32 %r0, %r1, 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(resolveInstruction(parse_instruction(source)).has_value());
  }
}

TEST(ResolveInstruction, DispatchesByOpcodeIntoGeneratedVariant) {
  const ResolvedModule empty_module{};
  EXPECT_TRUE(empty_module.functions.empty());

  const auto add_ast = parse_instruction("add.u32 %r0, %r1, %r2;");
  const auto add = resolveInstruction(add_ast);
  ASSERT_TRUE(add.has_value()) << add.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Add>(*add));

  const auto sub_ast = parse_instruction("sub.u32 %r0, %r1, %r2;");
  const auto sub = resolveInstruction(sub_ast);
  ASSERT_TRUE(sub.has_value()) << sub.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Sub>(*sub));

  const auto ret_ast = parse_instruction("ret;");
  const auto ret = resolveInstruction(ret_ast);
  ASSERT_TRUE(ret.has_value()) << ret.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Ret>(*ret));

  const auto exit_ast = parse_instruction("exit;");
  const auto exit_instruction = resolveInstruction(exit_ast);
  ASSERT_TRUE(exit_instruction.has_value()) << exit_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Exit>(*exit_instruction));

  const auto trap_ast = parse_instruction("trap;");
  const auto trap = resolveInstruction(trap_ast);
  ASSERT_TRUE(trap.has_value()) << trap.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Trap>(*trap));

  const auto and_ast = parse_instruction("and.b32 %r0, %r1, %r2;");
  const auto and_instruction = resolveInstruction(and_ast);
  ASSERT_TRUE(and_instruction.has_value()) << and_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<And>(*and_instruction));

  const auto or_ast = parse_instruction("or.b32 %r0, %r1, %r2;");
  const auto or_instruction = resolveInstruction(or_ast);
  ASSERT_TRUE(or_instruction.has_value()) << or_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Or>(*or_instruction));

  const auto xor_ast = parse_instruction("xor.b32 %r0, %r1, %r2;");
  const auto xor_instruction = resolveInstruction(xor_ast);
  ASSERT_TRUE(xor_instruction.has_value()) << xor_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Xor>(*xor_instruction));

  const auto not_ast = parse_instruction("not.b32 %r0, %r1;");
  const auto not_instruction = resolveInstruction(not_ast);
  ASSERT_TRUE(not_instruction.has_value()) << not_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Not>(*not_instruction));

  const auto shl_ast = parse_instruction("shl.b32 %r0, %r1, %r2;");
  const auto shl_instruction = resolveInstruction(shl_ast);
  ASSERT_TRUE(shl_instruction.has_value()) << shl_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Shl>(*shl_instruction));

  const auto shr_ast = parse_instruction("shr.u32 %r0, %r1, %r2;");
  const auto shr_instruction = resolveInstruction(shr_ast);
  ASSERT_TRUE(shr_instruction.has_value()) << shr_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Shr>(*shr_instruction));

  const auto setp_ast = parse_instruction("setp.lt.u32 %p0, %r0, %r1;");
  const auto setp_instruction = resolveInstruction(setp_ast);
  ASSERT_TRUE(setp_instruction.has_value()) << setp_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Setp>(*setp_instruction));

  const auto selp_ast = parse_instruction("selp.u32 %r0, %r1, %r2, %p0;");
  const auto selp_instruction = resolveInstruction(selp_ast);
  ASSERT_TRUE(selp_instruction.has_value()) << selp_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Selp>(*selp_instruction));

  const auto cvt_ast = parse_instruction("cvt.s32.u32 %s0, %r0;");
  const auto cvt_instruction = resolveInstruction(cvt_ast);
  ASSERT_TRUE(cvt_instruction.has_value()) << cvt_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Cvt>(*cvt_instruction));

  const auto ld_ast = parse_instruction("ld.global.u32 %r0, [%rd1];");
  const auto ld_instruction = resolveInstruction(ld_ast);
  ASSERT_TRUE(ld_instruction.has_value()) << ld_instruction.error().message;
  EXPECT_TRUE(test_ir_access::holds_alternative<Ld>(*ld_instruction));
}

TEST(ResolveInstruction, RejectsUnknownOpcode) {
  const auto ast = parse_instruction("unknown.u32 %r0, %r1, %r2;");

  const auto resolved = resolveInstruction(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.opcode.syntax.range);
  EXPECT_EQ(resolved.error().message, "Unknown PTX opcode 'unknown'.");
}

TEST(ResolveInstruction, RejectsMalformedMetadataCallWithGenericLayoutError) {
  const auto resolved =
      resolveInstruction(indirect_metadata_instruction("metadata"));

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant "
            "'Direct'.");
}

TEST(ResolveCpAsyncStandalone, DefersUnambiguousControlRegisterTypes) {
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
  };
  const auto size = resolveInstruction(
      parse_instruction("cp.async.ca.shared.global [%r0], [%rd0], 4, %r1;"));
  ASSERT_TRUE(size.has_value()) << size.error().message;
  const auto& size_copy = test_ir_access::get<Cp>(*size);
  const auto& size_control =
      test_ir_access::get<Cp::AsyncCaSharedGlobalControl>(size_copy.variant)
          .source_control.value;
  const auto& size_register =
      test_ir_access::get<ResolvedRegisterRef>(size_control);
  EXPECT_FALSE(size_register.declared_type.has_value());
  EXPECT_FALSE(size_register.symbol_id.has_value());
  EXPECT_TRUE(checker::check(size_copy, context).has_value());

  const auto predicate = resolveInstruction(
      parse_instruction("cp.async.ca.shared.global [%r0], [%rd0], 4, %p0;"));
  ASSERT_TRUE(predicate.has_value()) << predicate.error().message;
  const auto& predicate_copy = test_ir_access::get<Cp>(*predicate);
  const auto& predicate_control =
      test_ir_access::get<Cp::AsyncCaSharedGlobalControl>(
          predicate_copy.variant)
          .source_control.value;
  const auto& ignore =
      test_ir_access::get<ResolvedPredicate>(predicate_control);
  EXPECT_FALSE(ignore.register_ref.declared_type.has_value());
  EXPECT_FALSE(ignore.register_ref.symbol_id.has_value());
  EXPECT_TRUE(checker::check(predicate_copy, context).has_value());
  EXPECT_FALSE(checker::check(predicate_copy,
                              checker::Context{.target = {.ptx_version = {7, 4},
                                                          .sm_version = 80}})
                   .has_value());

  const auto policy = resolveInstruction(parse_instruction(
      "cp.async.ca.shared.global.L2::cache_hint [%r0], [%rd0], 4, 0, %rd1;"));
  ASSERT_TRUE(policy.has_value()) << policy.error().message;
  const auto& policy_copy = test_ir_access::get<Cp>(*policy);
  const auto& policy_register =
      test_ir_access::get<Cp::AsyncCaSharedGlobalCacheHintControlPolicy>(
          policy_copy.variant)
          .cache_policy.value;
  EXPECT_FALSE(policy_register.declared_type.has_value());
  EXPECT_FALSE(policy_register.symbol_id.has_value());
  EXPECT_TRUE(checker::check(policy_copy, context).has_value());
  EXPECT_FALSE(checker::check(policy_copy,
                              checker::Context{.target = {.ptx_version = {7, 3},
                                                          .sm_version = 80}})
                   .has_value());
  EXPECT_FALSE(checker::check(policy_copy,
                              checker::Context{.target = {.ptx_version = {9, 3},
                                                          .sm_version = 75}})
                   .has_value());
}

TEST(ResolveCpAsyncStandalone, RequiresBindingForAmbiguousFourthRegister) {
  const auto ambiguous = resolveInstruction(parse_instruction(
      "cp.async.ca.shared.global.L2::cache_hint [%r0], [%rd0], 4, %r2;"));
  ASSERT_FALSE(ambiguous.has_value());
  EXPECT_NE(ambiguous.error().message.find("requires a declaration"),
            std::string::npos);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
