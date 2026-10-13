#include <gtest/gtest.h>

#include <array>
#include <string>

#include "ptx_parser_driver.hpp"

namespace ptx_frontend::detail {
namespace {

/** Target classification preserves written items, commas, and owned source bytes. */
TEST(GeneratedPtxParser, ClassifiesTargetDirectiveItemsAtTheirPositions) {
  for (const auto architecture : {"sm_80", "sm_999", "compute_80", "sm_90a"}) {
    const std::string source = std::string{"/*header*/ .target "} +
                               architecture +
                               ", texmode_unified, texmode_independent, debug, "
                               "map_f64_to_f32; //EOF\n";
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed.diagnostics.empty());
    EXPECT_EQ(parsed->sourceText(), source);
    const auto& directive = std::get<syntax_cst::CstModuleDirective>(
        parsed->module()->items.front());
    ASSERT_EQ(directive.arguments.size(), 5u);
    ASSERT_EQ(directive.separators.size(), 4u);
    EXPECT_EQ(parsed->token(directive.arguments.front()).text, architecture);
    EXPECT_EQ(directive.token_range.first, directive.keyword);
    ASSERT_TRUE(directive.terminator.has_value());
    EXPECT_EQ(directive.token_range.last, *directive.terminator + 1);
    for (std::size_t i = 0; i < directive.separators.size(); ++i) {
      EXPECT_EQ(parsed->token(directive.separators[i]).kind, TokenKind::Comma);
      EXPECT_EQ(directive.separators[i] + 1, directive.arguments[i + 1]);
    }
  }
}

/** Rejected consumed items recover at their successor without losing neighbors. */
TEST(GeneratedPtxParser, RecoversRejectedTargetItemsBeforeAdjacentDirectives) {
  for (const auto target : {"bogus", "debug", "sm_80, bogus", "sm_80, sm_90"}) {
    for (const auto suffix : {"\n", ";\n", ", debug\n"}) {
      const std::string source =
          ".version 9.3\n.target " + std::string{target} + suffix +
          ".address_size 64\n.entry survivor() { ret; } //EOF\n";
      SCOPED_TRACE(source);
      PtxCstParser parser(source);
      const auto parsed = parser.parseModule();
      ASSERT_TRUE(parsed.has_value());
      ASSERT_EQ(parsed.diagnostics.size(), 1u);
      const auto offending = std::string_view{target}.find_last_of(' ');
      const auto spelling = std::string_view{target}.substr(
          offending == std::string_view::npos ? 0 : offending + 1);
      const auto column = 9 + std::string_view{target}.find(spelling);
      EXPECT_EQ(
          parsed.diagnostics.front().range,
          (SourceRange{{2, static_cast<int32_t>(column)},
                       {2, static_cast<int32_t>(column + spelling.size())}}));
      EXPECT_EQ(parsed.diagnostics.front().message,
                std::string_view{target}.find(',') == std::string_view::npos
                    ? "expected target architecture spelling"
                    : "expected modeled target option");
      EXPECT_FALSE(parsed.diagnostics.front().expected_kind.has_value());
      EXPECT_EQ(parsed->sourceText(), source);
      ASSERT_EQ(parsed->tokens.back().kind, TokenKind::Eof);
      const auto& items = parsed->module()->items;
      ASSERT_GE(items.size(), 4u);
      const auto& address =
          std::get<syntax_cst::CstModuleDirective>(items[items.size() - 2]);
      EXPECT_EQ(parsed->token(address.keyword).kind, TokenKind::DotAddressSize);
      const auto& survivor = std::get<syntax_cst::CstFunction>(items.back());
      EXPECT_EQ(parsed->token(survivor.name).text, "survivor");
      EXPECT_EQ(survivor.body.size(), 1u);
      const auto exhausted = parser.parseModule();
      ASSERT_FALSE(exhausted.has_value());
      ASSERT_EQ(exhausted.diagnostics.size(), 1u);
      EXPECT_EQ(exhausted.diagnostics.front().range,
                parsed->tokens.back().range);
    }
  }
}

/** Function-target lists accept names that are invalid in a module target header. */
TEST(GeneratedPtxParser,
     TargetDirectiveClassificationDoesNotAffectCallTargets) {
  constexpr std::string_view source =
      ".func f() { list: .calltargets bogus, debug, sm_80; ret; }";
  const auto parsed = PtxCstParser(source).parseModule();
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed.diagnostics.empty());
  EXPECT_EQ(parsed->sourceText(), source);
  const auto& function =
      std::get<syntax_cst::CstFunction>(parsed->module()->items.front());
  const auto& targets =
      std::get<syntax_cst::CstCallTargets>(function.body.front());
  ASSERT_EQ(targets.targets.size(), 3u);
  EXPECT_EQ(parsed->token(targets.targets[0]).text, "bogus");
  EXPECT_EQ(parsed->token(targets.targets[1]).text, "debug");
  EXPECT_EQ(parsed->token(targets.targets[2]).text, "sm_80");
}

/** Private function handles must not determine the largest semantic slot. */
TEST(GeneratedPtxParser, CompactModuleItemTransfersExclusiveFunctionOwnership) {
  static_assert(sizeof(ModuleItemValue) <= sizeof(syntax_cst::CstOperand));
  static_assert(sizeof(FunctionValue) == sizeof(void*));

  // The same owner moves through function symbols and the module-item variant.
  auto function = std::make_unique<syntax_cst::CstFunction>();
  function->name = 3;
  function->token_range = {1, 8};
  function->body.emplace_back(syntax_cst::CstInstruction{});
  const auto* identity = function.get();
  ModuleItemValue item = std::move(function);
  EXPECT_FALSE(function);
  EXPECT_EQ(std::get<FunctionValue>(item).get(), identity);

  // Materialization moves the payload into public storage, without any borrow.
  const auto materialized = materializeModuleItem(std::move(item));
  const auto& result = std::get<syntax_cst::CstFunction>(materialized);
  EXPECT_EQ(result.name, 3u);
  EXPECT_EQ(result.token_range.first, 1u);
  EXPECT_EQ(result.token_range.last, 8u);
  ASSERT_EQ(result.body.size(), 1u);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(result.body[0]));
}

/** Public nonfunction items retain their value representation and token ranges. */
TEST(GeneratedPtxParser, CompactModuleItemPreservesNonfunctionAlternatives) {
  syntax_cst::CstModuleDirective directive;
  directive.keyword = 4;
  directive.token_range = {4, 7};
  const auto materialized = materializeModuleItem(ModuleItemValue{directive});
  const auto& result = std::get<syntax_cst::CstModuleDirective>(materialized);
  EXPECT_EQ(result.keyword, 4u);
  EXPECT_EQ(result.token_range.first, 4u);
  EXPECT_EQ(result.token_range.last, 7u);
}

/** Compare the exact recovery contract, including absent and zero-width spans. */
void expectRecoveryIdentity(const syntax_cst::CstRecoveryNode& actual,
                            const syntax_cst::CstRecoveryNode& expected) {
  EXPECT_EQ(actual.kind, expected.kind);
  EXPECT_EQ(actual.expected_kind, expected.expected_kind);
  EXPECT_EQ(actual.range, expected.range);
  ASSERT_EQ(actual.token_range.has_value(), expected.token_range.has_value());
  if (actual.token_range) {
    EXPECT_EQ(actual.token_range->first, expected.token_range->first);
    EXPECT_EQ(actual.token_range->last, expected.token_range->last);
  }
}

/** Traverse nested bodies so depth failures cannot hide a lost recovery neighbor. */
void expectBodyIdentity(
    const std::vector<syntax_cst::CstFunctionBodyItem>& actual,
    const std::vector<syntax_cst::CstFunctionBodyItem>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    ASSERT_EQ(actual[i].index(), expected[i].index());
    if (const auto* recovery =
            std::get_if<syntax_cst::CstRecoveryNode>(&actual[i]))
      expectRecoveryIdentity(
          *recovery, std::get<syntax_cst::CstRecoveryNode>(expected[i]));
    else if (const auto* block =
                 std::get_if<std::unique_ptr<syntax_cst::CstBlock>>(
                     &actual[i])) {
      const auto& reference =
          *std::get<std::unique_ptr<syntax_cst::CstBlock>>(expected[i]);
      EXPECT_EQ((*block)->left_brace, reference.left_brace);
      EXPECT_EQ((*block)->right_brace, reference.right_brace);
      EXPECT_EQ((*block)->token_range.first, reference.token_range.first);
      EXPECT_EQ((*block)->token_range.last, reference.token_range.last);
      expectBodyIdentity((*block)->body, reference.body);
    } else if (const auto* instruction =
                   std::get_if<syntax_cst::CstInstruction>(&actual[i])) {
      const auto& reference = std::get<syntax_cst::CstInstruction>(expected[i]);
      EXPECT_EQ(instruction->opcode, reference.opcode);
      EXPECT_EQ(instruction->semicolon, reference.semicolon);
      EXPECT_EQ(instruction->token_range.first, reference.token_range.first);
      EXPECT_EQ(instruction->token_range.last, reference.token_range.last);
    }
  }
}

/** Compare module neighbors, all diagnostics and every original token/trivia span. */
void expectModuleIdentity(std::string_view source) {
  const auto reference = PtxCstParser(source).parseModule();
  const auto actual = parseGenerated(source, EntryMode::Module);
  ASSERT_TRUE(reference.has_value());
  ASSERT_TRUE(actual.has_value());
  EXPECT_EQ(actual->sourceText(), source);
  ASSERT_EQ(actual.diagnostics.size(), reference.diagnostics.size());
  for (std::size_t i = 0; i < actual.diagnostics.size(); ++i) {
    EXPECT_EQ(actual.diagnostics[i].message, reference.diagnostics[i].message);
    EXPECT_EQ(actual.diagnostics[i].range, reference.diagnostics[i].range);
    EXPECT_EQ(actual.diagnostics[i].expected_kind,
              reference.diagnostics[i].expected_kind);
  }
  ASSERT_EQ(actual->tokens.size(), reference->tokens.size());
  for (std::size_t i = 0; i < actual->tokens.size(); ++i) {
    const auto& token = actual->tokens[i];
    const auto& original = reference->tokens[i];
    EXPECT_EQ(token.kind, original.kind);
    EXPECT_EQ(token.text, original.text);
    EXPECT_EQ(token.range, original.range);
    ASSERT_EQ(token.leading_trivia.size(), original.leading_trivia.size());
    for (std::size_t j = 0; j < token.leading_trivia.size(); ++j) {
      EXPECT_EQ(token.leading_trivia[j].kind, original.leading_trivia[j].kind);
      EXPECT_EQ(token.leading_trivia[j].text, original.leading_trivia[j].text);
      EXPECT_EQ(token.leading_trivia[j].range,
                original.leading_trivia[j].range);
    }
  }
  const auto& items = actual->module()->items;
  const auto& originals = reference->module()->items;
  ASSERT_EQ(items.size(), originals.size());
  for (std::size_t i = 0; i < items.size(); ++i) {
    ASSERT_EQ(items[i].index(), originals[i].index());
    if (const auto* function =
            std::get_if<syntax_cst::CstFunction>(&items[i])) {
      const auto& original = std::get<syntax_cst::CstFunction>(originals[i]);
      EXPECT_EQ(function->name, original.name);
      EXPECT_EQ(function->left_brace, original.left_brace);
      EXPECT_EQ(function->right_brace, original.right_brace);
      EXPECT_EQ(function->token_range.first, original.token_range.first);
      EXPECT_EQ(function->token_range.last, original.token_range.last);
      expectBodyIdentity(function->body, original.body);
    } else if (const auto* recovery =
                   std::get_if<syntax_cst::CstRecoveryNode>(&items[i]))
      expectRecoveryIdentity(
          *recovery, std::get<syntax_cst::CstRecoveryNode>(originals[i]));
  }
}

/** Synthetic closes are confined to item boundaries and retain each real anchor. */
TEST(GeneratedPtxParser, RecoversMissingBodyBracesAtEligibleBoundaries) {
  constexpr std::array sources{
      ".entry a(){ .visible .entry b(){ret;}",
      ".entry a(){ { { ret; .entry b(){ret;}",
      ".entry a(){ { { ret; .version 9.3 .entry b(){ret;}",
      ".entry a(){ { { ret;",
      ".entry a(){ { {",
      ".entry a(){mov.u32 %r,1 .entry b(){ret;}",
      ".entry a(){mov.u32 %r,1 .reg .u32 %s; ret;}",
      ".entry a(){ld.weak.global.u32 %r,[address]; .reg .u32 %s; .pragma "
      "\"body\"; .loc 1 2 3; {ret;} ret;}",
      ".section .debug_info { { .entry .global .pragma } .b8 0; } .entry "
      "a(){ret;}"};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    expectModuleIdentity(source);
  }
}

/** Function braces do not spend the 256-level nested block budget. */
TEST(GeneratedPtxParser, PreservesNestedBlockDepthBoundaryAndRecoveryCursor) {
  for (const std::size_t count : {256u, 257u}) {
    const std::string source = ".entry a(){" + std::string(count, '{') +
                               "ret;" + std::string(count, '}') +
                               "} .entry survivor(){ret;}";
    SCOPED_TRACE(count);
    expectModuleIdentity(source);
    const auto result = parseGenerated(source, EntryMode::Module);
    ASSERT_TRUE(result.has_value());
    if (count == 256)
      EXPECT_TRUE(result.diagnostics.empty());
    else {
      ASSERT_FALSE(result.diagnostics.empty());
      EXPECT_EQ(result.diagnostics.front().message,
                "nested block depth exceeds parser limit");
    }
    ASSERT_TRUE(std::holds_alternative<syntax_cst::CstFunction>(
        result->module()->items.back()));
    EXPECT_EQ(result
                  ->token(std::get<syntax_cst::CstFunction>(
                              result->module()->items.back())
                              .name)
                  .text,
              "survivor");
  }
}

/** Unsupported lookahead cannot suppress an already-completed predecessor. */
TEST(GeneratedPtxParser, RetainsCompletedItemsBeforeUnsupportedLookahead) {
  for (const auto source :
       {".target sm_80 .unknown 64; .entry survivor(){ret;}",
        ".func f(){label: .unknown value; ret;}", ".version 9.3 .unknown 0;"}) {
    SCOPED_TRACE(source);
    expectModuleIdentity(source);
  }
}

/** Empty parameter lists must not make a nonempty list's comma optional. */
TEST(GeneratedPtxParser, RejectsLeadingParameterCommas) {
  for (const auto source :
       {".entry k(, .param .u32 x) { ret; }", ".func (, .reg .u32 r) f();",
        ".func f(, .param .u32 x);",
        ".func f(){ p: .callprototype (, .reg .u32 r) _; ret; }",
        ".func f(){ p: .callprototype _ (, .param .u32 x); ret; }"}) {
    SCOPED_TRACE(source);
    expectModuleIdentity(source);
    const auto result = parseGenerated(source, EntryMode::Module);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().message,
              "expected '.reg' or '.param' parameter");
  }
}

/** Prefix rejection and missing metadata terminators preserve ordered neighbors. */
TEST(GeneratedPtxParser, PreservesMalformedModulePrefixRecovery) {
  for (const auto source :
       {".entry k(){.loc 1 2 3, bogus f, inlined_at 1 2 3; ret;}",
        ".entry k(){.loc 1 2 3, function_name f, bogus 1 2 3; ret;}",
        ".entry k(){.loc 1 2 3, function_name 1, inlined_at 1 2 3; ret;}",
        ".entry k(){.loc 1 2 3, function_name f+1.0, inlined_at 1 2 3; ret;}",
        ".entry k(){.loc 1 2 3, function_name f, inlined_at 1 2 -1; ret;}",
        ".entry k(){.loc 1 2; ret;}",
        ".entry k(){.loc 1 2 3, function_name f, inlined_at; ret;}",
        ".global .attribute(.unknown(1,2)) .u32 x; .entry good(){ret;}",
        ".global .attribute(.unified(1,2,3)) .u32 x; .entry good(){ret;}",
        ".entry k(){t: .calltargets f .reg .u32 r; ret;}",
        ".entry k(){t: .branchtargets a .reg .u32 r; ret;}",
        ".entry k(){p: .callprototype _ .abi_preserve; ret;}"}) {
    SCOPED_TRACE(source);
    expectModuleIdentity(source);
    EXPECT_FALSE(parseGenerated(source, EntryMode::Module).diagnostics.empty());
  }
}

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
