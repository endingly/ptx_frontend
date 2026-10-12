#include <gtest/gtest.h>

#include <array>
#include <string>

#include "ptx_parser_driver.hpp"

namespace ptx_frontend::detail {
namespace {

/** Compare generated standalone forms with the established facade shapes. */
TEST(GeneratedPtxParser, PreservesStandaloneInstructionForms) {
  constexpr std::array sources{
      "ret;",
      "@!%p add.u32 %r0, %r1, 3;",
      "mov.u32 %r0, -7, +8, !1, !-2, ~1, +symbol;",
      "mov.u32 %r0, (1+2)*3-4, (.u64)(1?2:3), 0xff(3)(4);",
      "mov.u64 %rd, [1*2+3], [(1+2)*3-4], [base+1+2];",
      "mov.u64 %rd, base+1+2, A[idx-1], A[(1+2)*3];",
      "mov.u32 %r0, %r1.x, -%r1.y, !%p, %r1|%p;",
      "mov.u32 {%r0, %r1.x, -2, 1+2}|%p, {%r2, %r3};",
      "cp.async.bulk.tensor.2d [map+64, {%r0, -2}];",
      "foo [a,b,c], [a,b,{%r0,1+2}], [base].unified;",
      "call target;",
      "call target, targets;",
      "call target, ();",
      "call (retval), target, (arg, 1+2), prototype;",
      "call target, (1?2:3, (.s64)-1);",
      "bra label;",
      "brx.idx %r, labels;",
      "foo call, bra.x, brx+1, [call+2], -bra, !brx;",
      " /*lead*/ mov.u64 %rd, /*expr*/ (generic(call)+0xff(2)); //eof\n"};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    const auto original = PtxCstParser(source).parseInstruction();
    const auto generated = parseGenerated(source, EntryMode::Instruction);
    ASSERT_TRUE(original.has_value()) << original.diagnostics.front().message;
    ASSERT_TRUE(generated.has_value()) << generated.diagnostics.front().message;
    EXPECT_TRUE(generated.diagnostics.empty());
    EXPECT_EQ(generated->sourceText(), source);
    const auto& expected = *original->instruction();
    const auto& actual = *generated->instruction();
    EXPECT_EQ(actual.token_range.first, expected.token_range.first);
    EXPECT_EQ(actual.token_range.last, expected.token_range.last);
    EXPECT_EQ(actual.opcode, expected.opcode);
    EXPECT_EQ(actual.modifiers, expected.modifiers);
    EXPECT_EQ(actual.semicolon, expected.semicolon);
    ASSERT_EQ(actual.operands.size(), expected.operands.size());
    for (std::size_t i = 0; i < actual.operands.size(); ++i) {
      EXPECT_EQ(actual.operands[i].operand.index(),
                expected.operands[i].operand.index());
      EXPECT_EQ(actual.operands[i].trailing_comma,
                expected.operands[i].trailing_comma);
    }
    for (std::size_t i = 0; i < actual.token_range.last; ++i) {
      EXPECT_EQ(generated->tokens[i].kind, original->tokens[i].kind);
      EXPECT_EQ(generated->tokens[i].text, original->tokens[i].text);
      EXPECT_EQ(generated->tokens[i].range, original->tokens[i].range);
      EXPECT_EQ(generated->tokens[i].leading_trivia.size(),
                original->tokens[i].leading_trivia.size());
    }
  }
}

/** Action failures recover within the generated module grammar and retain neighbors. */
TEST(GeneratedPtxParser, RecoversActionFailuresWithoutLosingModule) {
  const std::array expressions{std::string(128, '+') + "1",
                               std::string{"(1+%r)"}, std::string{"1+"},
                               std::string(128, '('), std::string(128, '+')};
  for (const auto& expression : expressions) {
    const std::string source =
        ".entry first() {\nmov.u32 %r0, " + expression +
        ";\nmov.u32 %r1, 7;\n}\n.entry second() { ret; } //eof\n";
    SCOPED_TRACE(expression);
    const auto generated = parseGenerated(source, EntryMode::Module);
    const auto original = PtxCstParser(source).parseModule();
    ASSERT_TRUE(generated.has_value());
    ASSERT_EQ(generated.diagnostics.size(), 1u);
    ASSERT_EQ(original.diagnostics.size(), 1u);
    EXPECT_EQ(generated.diagnostics.front().message,
              original.diagnostics.front().message);
    EXPECT_EQ(generated.diagnostics.front().range,
              original.diagnostics.front().range);
    EXPECT_EQ(generated->sourceText(), source);
    ASSERT_EQ(generated->module()->items.size(), 2u);
    const auto& first =
        std::get<syntax_cst::CstFunction>(generated->module()->items[0]);
    const auto& expected =
        std::get<syntax_cst::CstFunction>(original->module()->items[0]);
    ASSERT_EQ(first.body.size(), expected.body.size());
    for (std::size_t i = 0; i < first.body.size(); ++i) {
      ASSERT_EQ(first.body[i].index(), expected.body[i].index());
      if (const auto* recovery =
              std::get_if<syntax_cst::CstRecoveryNode>(&first.body[i])) {
        const auto& reference =
            std::get<syntax_cst::CstRecoveryNode>(expected.body[i]);
        EXPECT_EQ(recovery->kind, reference.kind);
        EXPECT_EQ(recovery->expected_kind, reference.expected_kind);
        EXPECT_EQ(recovery->range, reference.range);
        ASSERT_EQ(recovery->token_range.has_value(),
                  reference.token_range.has_value());
        if (recovery->token_range) {
          EXPECT_EQ(recovery->token_range->first, reference.token_range->first);
          EXPECT_EQ(recovery->token_range->last, reference.token_range->last);
        }
      }
    }
    ASSERT_FALSE(first.body.empty());
    ASSERT_TRUE(
        std::holds_alternative<syntax_cst::CstInstruction>(first.body.back()));
    EXPECT_EQ(
        generated
            ->token(
                std::get<syntax_cst::CstInstruction>(first.body.back()).opcode)
            .text,
        "mov");
  }
}

/** Keep specialized operand failures identical to the established diagnostics. */
TEST(GeneratedPtxParser, PreservesMalformedOperandDiagnostics) {
  constexpr std::array sources{
      "call;",           "call ();",       "call (r,x), t, ();",
      "call (r) t, ();", "call (r), t;",   "call (r), t, set;",
      "call t, (1,);",   "call t, (,);",   "call t, (r x);",
      "call t, (r),;",   "bra a,b;",       "brx.idx;",
      "brx.idx r,;",     "brx.idx r,s,t;", "foo {};",
      "foo {r,};",       "foo {r x};",     "foo [a;",
      "foo [a,];",       "foo !1.0;",      "foo !;",
      "foo -;",          "foo r|;",        "foo -r.;"};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    const auto reference = PtxCstParser(source).parseInstruction();
    const auto candidate = parseGenerated(source, EntryMode::Instruction);
    ASSERT_FALSE(reference.has_value());
    ASSERT_FALSE(candidate.has_value());
    ASSERT_EQ(reference.diagnostics.size(), candidate.diagnostics.size());
    for (std::size_t i = 0; i < reference.diagnostics.size(); ++i) {
      EXPECT_EQ(reference.diagnostics[i].message,
                candidate.diagnostics[i].message);
      EXPECT_EQ(reference.diagnostics[i].range, candidate.diagnostics[i].range);
      EXPECT_EQ(reference.diagnostics[i].expected_kind,
                candidate.diagnostics[i].expected_kind);
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::detail
