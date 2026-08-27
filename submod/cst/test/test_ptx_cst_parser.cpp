#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {

using syntax_cst::CstAddress;
using syntax_cst::CstBranchTarget;
using syntax_cst::CstBranchTargetSet;
using syntax_cst::CstBlock;
using syntax_cst::CstCallParameterList;
using syntax_cst::CstCallTarget;
using syntax_cst::CstCallTargetSet;
using syntax_cst::CstCallPrototype;
using syntax_cst::CstCallTargets;
using syntax_cst::CstBranchTargets;
using syntax_cst::CstRecoveryKind;
using syntax_cst::CstRecoveryNode;
using syntax_cst::CstTokenRange;
using syntax_cst::CstVectorMember;
using syntax_cst::CstVectorPack;

TEST(PtxCstParser, RoundTripsInstructionWithAllTriviaAndPunctuation) {
  constexpr std::string_view source =
      "  // lead\n@!%p add /* type */ .u32 "
      "[%rd1 /* op */ + 16], {%r1, -2} /* tail */ ;\n// eof";
  PtxCstParser parser(source);

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_TRUE(result.diagnostics.empty());

  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  EXPECT_EQ(result->module(), nullptr);
  EXPECT_EQ(result->sourceText(), source);
  EXPECT_EQ(result->token(instruction->opcode).text, "add");
  ASSERT_TRUE(instruction->predicate.has_value());
  EXPECT_TRUE(instruction->predicate->exclamation_token.has_value());
  ASSERT_EQ(instruction->modifiers.size(), 1u);
  EXPECT_EQ(result->token(instruction->modifiers[0]).text, ".u32");
  ASSERT_EQ(instruction->operands.size(), 2u);
  EXPECT_TRUE(instruction->operands[0].trailing_comma.has_value());
  EXPECT_EQ(result->token(*instruction->operands[0].trailing_comma).kind,
            TokenKind::Comma);
  EXPECT_EQ(result->token(instruction->semicolon).kind, TokenKind::Semicolon);

  const auto& eof = result->tokens.back();
  EXPECT_EQ(eof.kind, TokenKind::Eof);
  ASSERT_EQ(eof.leading_trivia.size(), 2u);
  EXPECT_EQ(eof.leading_trivia[1].kind, TriviaKind::LineComment);
}

TEST(PtxCstParser, RoundTripsUnmodifiedModuleTokenBufferByteForByte) {
  constexpr std::string_view source = R"ptx(// leading comment
.version 9.3
.file 0 "round_trip.ptx"
.section .debug_info { /* raw payload */ .b8 0; };
.pragma "module";
.entry kernel() .pragma "header"; .maxnreg 32 {
  .pragma "body";
  { add.u32 %r0, %r1, %r2; }
}
// trailing comment
   )ptx";
  PtxCstParser parser(source);

  const auto first = parser.parseModule();

  ASSERT_TRUE(first.has_value()) << first.diagnostics.front().message;
  EXPECT_TRUE(first.diagnostics.empty());
  EXPECT_EQ(first->sourceText(), source);

  PtxCstParser reparsing(first->sourceText());
  const auto second = reparsing.parseModule();

  ASSERT_TRUE(second.has_value()) << second.diagnostics.front().message;
  EXPECT_TRUE(second.diagnostics.empty());
  EXPECT_EQ(second->sourceText(), source);

  const auto first_eof = [](const auto& tokens) {
    for (std::size_t index = 0; index < tokens.size(); ++index) {
      if (tokens[index].kind == TokenKind::Eof)
        return index;
    }
    return tokens.size();
  };
  const std::size_t first_end = first_eof(first->tokens);
  const std::size_t second_end = first_eof(second->tokens);
  ASSERT_TRUE(first_end < first->tokens.size());
  ASSERT_TRUE(second_end < second->tokens.size());
  ASSERT_EQ(first_end, second_end);

  for (std::size_t index = 0; index <= first_end; ++index) {
    const auto& left = first->tokens[index];
    const auto& right = second->tokens[index];
    EXPECT_EQ(left.kind, right.kind);
    EXPECT_EQ(left.text, right.text);
    ASSERT_EQ(left.leading_trivia.size(), right.leading_trivia.size());
    for (std::size_t trivia = 0; trivia < left.leading_trivia.size(); ++trivia) {
      EXPECT_EQ(left.leading_trivia[trivia].kind,
                right.leading_trivia[trivia].kind);
      EXPECT_EQ(left.leading_trivia[trivia].text, right.leading_trivia[trivia].text);
    }
  }
  const auto& final_trivia = first->tokens[first_end].leading_trivia;
  ASSERT_EQ(final_trivia.size(), 3u);
  EXPECT_EQ(final_trivia[1].kind, TriviaKind::LineComment);
  EXPECT_EQ(final_trivia.back().text, "\n   ");
}

TEST(PtxCstParser, AcceptsWeakAsAnInstructionModifier) {
  PtxCstParser parser("ld.weak.u32 %r0, [%rd0];");

  const auto result = parser.parseInstruction();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->modifiers.size(), 2u);
  EXPECT_EQ(result->token(instruction->modifiers[0]).kind, TokenKind::DotWeak);
  EXPECT_EQ(result->token(instruction->modifiers[0]).text, ".weak");
}

TEST(PtxCstParser, RetainsStructuredOperandDelimiterTokens) {
  PtxCstParser parser("mov.b32 [%rd1-4], %r2.x, {%r3, 1};");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->operands.size(), 3u);

  const auto& address = std::get<CstAddress>(instruction->operands[0].operand);
  ASSERT_TRUE(address.left_bracket.has_value());
  ASSERT_TRUE(address.right_bracket.has_value());
  ASSERT_TRUE(address.offset.has_value());
  EXPECT_EQ(result->token(*address.left_bracket).kind, TokenKind::LBracket);
  EXPECT_EQ(result->token(address.offset->operator_token).kind,
            TokenKind::Minus);
  EXPECT_EQ(result->token(*address.right_bracket).kind, TokenKind::RBracket);

  const auto& member =
      std::get<CstVectorMember>(instruction->operands[1].operand);
  EXPECT_EQ(result->token(member.selector).text, ".x");

  const auto& pack = std::get<CstVectorPack>(instruction->operands[2].operand);
  EXPECT_EQ(result->token(pack.left_brace).kind, TokenKind::LBrace);
  EXPECT_EQ(result->token(pack.right_brace).kind, TokenKind::RBrace);
  ASSERT_EQ(pack.commas.size(), 1u);
}

TEST(PtxCstParser, RetainsDedicatedCallOperandStructure) {
  constexpr std::string_view source =
      "@%p call.uni (%result), %callee, (%arg, -4), targets;";
  PtxCstParser parser(source);

  auto result = parser.parseInstruction();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->operands.size(), 4u);

  const auto& returns =
      std::get<CstCallParameterList>(instruction->operands[0].operand);
  EXPECT_EQ(returns.kind, syntax_cst::CstCallParameterListKind::Return);
  ASSERT_EQ(returns.parameters.size(), 1u);
  EXPECT_EQ(result->token(returns.left_paren).kind, TokenKind::LParen);
  EXPECT_EQ(result->token(returns.right_paren).kind, TokenKind::RParen);

  const auto& target =
      std::get<CstCallTarget>(instruction->operands[1].operand);
  EXPECT_EQ(result->token(target.name.token).text, "%callee");

  const auto& inputs =
      std::get<CstCallParameterList>(instruction->operands[2].operand);
  EXPECT_EQ(inputs.kind, syntax_cst::CstCallParameterListKind::Input);
  ASSERT_EQ(inputs.parameters.size(), 2u);
  ASSERT_EQ(inputs.commas.size(), 1u);

  const auto& target_set =
      std::get<CstCallTargetSet>(instruction->operands[3].operand);
  EXPECT_EQ(result->token(target_set.name.token).text, "targets");
}

TEST(PtxCstParser, RetainsDedicatedDirectBranchTarget) {
  PtxCstParser parser("@%p bra.uni done;");

  auto result = parser.parseInstruction();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->operands.size(), 1u);
  const auto& target =
      std::get<CstBranchTarget>(instruction->operands[0].operand);
  EXPECT_EQ(result->token(target.name.token).text, "done");
}

TEST(PtxCstParser, RetainsIndexedBranchTargetList) {
  constexpr std::string_view source = "brx.idx.uni %r0, targets;";
  PtxCstParser parser(source);

  const auto result = parser.parseInstruction();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->modifiers.size(), 2u);
  ASSERT_EQ(instruction->operands.size(), 2u);
  EXPECT_EQ(result->token(std::get<syntax_cst::CstIdentifier>(
                              instruction->operands[0].operand)
                              .token)
                .text,
            "%r0");
  const auto& target_set =
      std::get<CstBranchTargetSet>(instruction->operands[1].operand);
  EXPECT_EQ(result->token(target_set.name.token).text, "targets");
  ASSERT_TRUE(instruction->operands[0].trailing_comma.has_value());
}

TEST(PtxCstParser, RejectsMalformedCallAndBranchLayouts) {
  for (const auto [source, message] :
       std::initializer_list<std::pair<std::string_view, std::string_view>>{
           {"call (%r0, %r1), helper, ();",
            "a call return parameter list must contain exactly one name"},
           {"call (%r0), helper;",
            "a call with a return parameter requires an input parameter list"},
           {"call helper, (%r0,);",
            "call argument list cannot end with a trailing comma"},
           {"bra first, second;",
            "direct branch accepts exactly one label target"},
           {"brx.idx %r0, targets, extra;",
            "brx.idx accepts exactly an index and target list"}}) {
    PtxCstParser parser(source);
    const auto result = parser.parseInstruction();
    ASSERT_FALSE(result.has_value()) << source;
    EXPECT_EQ(result.diagnostics.front().message, message) << source;
  }
}

TEST(PtxCstParser, ReportsSingleDiagnosticWithoutAValue) {
  PtxCstParser parser("add.u32 %r1, %r2, %r3");

  const auto result = parser.parseInstruction();

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics.front().message, "expected ';'");
  EXPECT_EQ(result.diagnostics.front().range,
            (SourceRange{SourcePos{1, 22}, SourcePos{1, 22}}));
}

TEST(ParseResult, RetainsAValueAndOrderedDiagnostics) {
  const ResultWithDiagnostics<int, CstParseDiagnostic> result{
      .value = 7,
      .diagnostics = {
          {{{1, 1}, {1, 2}}, "first"},
          {{{2, 1}, {2, 2}}, "second"},
      },
  };

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 7);
  ASSERT_EQ(result.diagnostics.size(), 2u);
  EXPECT_EQ(result.diagnostics[0].message, "first");
  EXPECT_EQ(result.diagnostics[1].message, "second");
}

TEST(PtxCstParser, RetainsFunctionLocalCallPrototypeStructure) {
  constexpr std::string_view source = R"ptx(
.func dispatch() {
  no_args: .callprototype _;
  inputs: .callprototype _ (.param .u32 _);
  returns: .callprototype (.reg .u32 result) _;
  full: .callprototype (.param .u32 result) _ (.param .b8 arg[12]) .noreturn .abi_preserve 10 .abi_preserve_control 2;
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function = std::get<syntax_cst::CstFunction>(
      result->module()->items.front());
  ASSERT_EQ(function.body.size(), 4u);
  for (const auto& item : function.body)
    EXPECT_TRUE(std::holds_alternative<CstCallPrototype>(item));

  const auto& full = std::get<CstCallPrototype>(function.body.back());
  EXPECT_EQ(result->token(full.label).text, "full");
  EXPECT_EQ(result->token(full.colon).kind, TokenKind::Colon);
  EXPECT_EQ(result->token(full.directive).kind, TokenKind::DotCallPrototype);
  ASSERT_TRUE(full.return_parameters.has_value());
  ASSERT_TRUE(full.parameters.has_value());
  EXPECT_EQ(full.return_parameters->parameters.size(), 1u);
  EXPECT_EQ(full.parameters->parameters.size(), 1u);
  ASSERT_TRUE(full.noreturn_directive.has_value());
  ASSERT_TRUE(full.abi_preserve.has_value());
  ASSERT_TRUE(full.abi_preserve_control.has_value());
  EXPECT_EQ(result->token(full.abi_preserve->directive).text,
            ".abi_preserve");
  EXPECT_EQ(result->token(full.abi_preserve->count).text, "10");
  EXPECT_EQ(result->token(full.abi_preserve_control->count).text, "2");
}

TEST(PtxCstParser, RejectsMalformedAndNonlocalCallPrototypeGrammar) {
  for (const auto [source, message] :
       std::initializer_list<std::pair<std::string_view, std::string_view>>{
           {".func f() { p: .callprototype (.reg .u32 a, .reg .u32 b) _; }",
            ".callprototype return parameter list must contain exactly one parameter"},
           {".func f() { p: .callprototype value; }",
            "expected '_' in .callprototype"},
           {".func f() { .callprototype _; }",
            "'.callprototype' requires a preceding function-local label"},
           {"outside: .callprototype _;",
            "'.callprototype' is only valid inside a function body"},
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value()) << source;
    ASSERT_FALSE(result.diagnostics.empty()) << source;
    EXPECT_EQ(result.diagnostics.front().message, message) << source;
  }
}

TEST(PtxCstParser, RetainsFunctionLocalCallTargetsStructure) {
  constexpr std::string_view source = R"ptx(
.func caller() {
  one: .calltargets first;
  many: .calltargets first, second, third;
  duplicate: .calltargets first, first;
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function = std::get<syntax_cst::CstFunction>(
      result->module()->items.front());
  ASSERT_EQ(function.body.size(), 3u);
  for (const auto& item : function.body)
    EXPECT_TRUE(std::holds_alternative<CstCallTargets>(item));

  const auto& many = std::get<CstCallTargets>(function.body[1]);
  EXPECT_EQ(result->token(many.label).text, "many");
  EXPECT_EQ(result->token(many.colon).kind, TokenKind::Colon);
  EXPECT_EQ(result->token(many.directive).kind, TokenKind::DotCallTargets);
  ASSERT_EQ(many.targets.size(), 3u);
  ASSERT_EQ(many.commas.size(), 2u);
  EXPECT_EQ(result->token(many.targets[0]).text, "first");
  EXPECT_EQ(result->token(many.targets[2]).text, "third");

  const auto& duplicate = std::get<CstCallTargets>(function.body[2]);
  ASSERT_EQ(duplicate.targets.size(), 2u);
  EXPECT_EQ(result->token(duplicate.targets[0]).text, "first");
  EXPECT_EQ(result->token(duplicate.targets[1]).text, "first");
}

TEST(PtxCstParser, RejectsMalformedAndNonlocalCallTargetsGrammar) {
  for (const auto [source, message] :
       std::initializer_list<std::pair<std::string_view, std::string_view>>{
           {".func f() { list: .calltargets; }",
            ".calltargets requires at least one function target"},
           {".func f() { list: .calltargets first,; }",
            "call target list cannot end with a trailing comma"},
           {".func f() { .calltargets first; }",
            "'.calltargets' requires a preceding function-local label"},
           {"outside: .calltargets first;",
            "'.calltargets' is only valid inside a function body"},
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value()) << source;
    ASSERT_FALSE(result.diagnostics.empty()) << source;
    EXPECT_EQ(result.diagnostics.front().message, message) << source;
  }
}

TEST(PtxCstParser, RetainsFunctionLocalBranchTargetsStructure) {
  constexpr std::string_view source = R"ptx(
.func dispatch() {
  table: .branchtargets L1, N<5>, L1;
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function = std::get<syntax_cst::CstFunction>(
      result->module()->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& targets = std::get<CstBranchTargets>(function.body.front());
  EXPECT_EQ(result->token(targets.label).text, "table");
  EXPECT_EQ(result->token(targets.colon).kind, TokenKind::Colon);
  EXPECT_EQ(result->token(targets.directive).kind, TokenKind::DotBranchTargets);
  ASSERT_EQ(targets.targets.size(), 3u);
  ASSERT_EQ(targets.commas.size(), 2u);
  EXPECT_EQ(result->token(targets.targets[0].name).text, "L1");
  EXPECT_FALSE(targets.targets[0].count.has_value());
  EXPECT_EQ(result->token(targets.targets[1].name).text, "N");
  ASSERT_TRUE(targets.targets[1].left_angle.has_value());
  ASSERT_TRUE(targets.targets[1].count.has_value());
  ASSERT_TRUE(targets.targets[1].right_angle.has_value());
  EXPECT_EQ(result->token(*targets.targets[1].count).text, "5");
  EXPECT_EQ(result->token(targets.targets[2].name).text, "L1");
}

TEST(PtxCstParser, RejectsMalformedAndNonlocalBranchTargetsGrammar) {
  for (const auto [source, message] :
       std::initializer_list<std::pair<std::string_view, std::string_view>>{
           {".func f() { table: .branchtargets; }",
            ".branchtargets requires at least one label target"},
           {".func f() { table: .branchtargets L1,; }",
            "branch target list cannot end with a trailing comma"},
           {".func f() { table: .branchtargets N<>; }",
            "expected branch target count"},
           {".func f() { table: .branchtargets N<5; }",
            "expected '>' after branch target count"},
           {".func f() { .branchtargets L1; }",
            "'.branchtargets' requires a preceding function-local label"},
           {"outside: .branchtargets L1;",
            "'.branchtargets' is only valid inside a function body"},
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value()) << source;
    ASSERT_FALSE(result.diagnostics.empty()) << source;
    EXPECT_EQ(result.diagnostics.front().message, message) << source;
  }
}

TEST(PtxCstParser, RejectsTrailingSignificantInput) {
  PtxCstParser parser("add.u32 %r1, %r2, %r3; sub.u32 %r4, %r5, %r6;");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.diagnostics.front().message, "expected end of input");
}

TEST(CstFile, DistinguishesInstructionFragmentAndModuleRoots) {
  syntax_cst::CstFile module_file{
      .tokens = {},
      .root = syntax_cst::CstModule{.items = {}, .token_range = {}},
  };

  EXPECT_EQ(module_file.instruction(), nullptr);
  ASSERT_NE(module_file.module(), nullptr);
  EXPECT_TRUE(module_file.module()->items.empty());
}

TEST(PtxCstParser, ParsesAndRoundTripsMinimalModule) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80, debug
.address_size 64

.visible .entry kernel() {
  add.u32 %r0, %r1, %r2;
  ret;
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  ASSERT_NE(result->module(), nullptr);
  EXPECT_EQ(result->instruction(), nullptr);
  const auto& items = result->module()->items;
  ASSERT_EQ(items.size(), 4u);

  const auto& version = std::get<syntax_cst::CstModuleDirective>(items[0]);
  EXPECT_EQ(result->token(version.keyword).kind, TokenKind::DotVersion);
  ASSERT_EQ(version.arguments.size(), 1u);
  EXPECT_EQ(result->token(version.arguments[0]).text, "8.0");

  const auto& target = std::get<syntax_cst::CstModuleDirective>(items[1]);
  ASSERT_EQ(target.arguments.size(), 2u);
  ASSERT_EQ(target.separators.size(), 1u);
  EXPECT_EQ(result->token(target.arguments[0]).text, "sm_80");
  EXPECT_EQ(result->token(target.arguments[1]).text, "debug");
  EXPECT_EQ(result->token(target.separators[0]).kind, TokenKind::Comma);

  const auto& function = std::get<syntax_cst::CstFunction>(items[3]);
  ASSERT_EQ(function.qualifiers.size(), 1u);
  EXPECT_EQ(result->token(function.qualifiers[0]).kind, TokenKind::DotVisible);
  EXPECT_EQ(result->token(function.directive).kind, TokenKind::DotEntry);
  EXPECT_EQ(result->token(function.name).text, "kernel");
  ASSERT_EQ(function.body.size(), 2u);
  EXPECT_EQ(
      result
          ->token(std::get<syntax_cst::CstInstruction>(function.body[0]).opcode)
          .text,
      "add");
  EXPECT_EQ(
      result
          ->token(std::get<syntax_cst::CstInstruction>(function.body[1]).opcode)
          .text,
      "ret");
}

TEST(PtxCstParser, RetainsOutermostFileDirectivePayload) {
  constexpr std::string_view source = R"ptx(.file 0x1U "source.ptx"
.file 1 "large.ptx", 0, 18446744073709551615U;
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& items = result->module()->items;
  ASSERT_EQ(items.size(), 2u);
  const auto& short_form =
      std::get<syntax_cst::CstModuleDirective>(items[0]);
  EXPECT_EQ(result->token(short_form.keyword).kind, TokenKind::DotFile);
  ASSERT_EQ(short_form.arguments.size(), 2u);
  EXPECT_EQ(result->token(short_form.arguments[0]).kind, TokenKind::Hex);
  EXPECT_EQ(result->token(short_form.arguments[0]).text, "0x1U");
  EXPECT_TRUE(short_form.separators.empty());
  EXPECT_FALSE(short_form.terminator.has_value());
  EXPECT_EQ(result->token(short_form.arguments[1]).text, "\"source.ptx\"");

  const auto& full_form =
      std::get<syntax_cst::CstModuleDirective>(items[1]);
  ASSERT_EQ(full_form.arguments.size(), 4u);
  ASSERT_EQ(full_form.separators.size(), 2u);
  ASSERT_TRUE(full_form.terminator.has_value());
  EXPECT_EQ(result->token(full_form.arguments[2]).text, "0");
  EXPECT_EQ(result->token(full_form.arguments[3]).text,
            "18446744073709551615U");
  EXPECT_EQ(result->sourceRange(full_form.token_range).start.line, 2u);
}

TEST(PtxCstParser, RejectsMalformedOrNonlocalFileDirectives) {
  for (const std::string_view source : {
           ".file 0 \"source.ptx\", 0",
           ".file 0 \"source.ptx\", , 0",
           ".file 0 source.ptx",
           ".entry kernel() { .file 0 \"source.ptx\" }",
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_TRUE(result.has_value()) << source;
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxCstParser, RetainsOutermostSectionPayload) {
  constexpr std::string_view source = R"ptx(.section .debug_info {
Lbegin:
  .b8 -128, 0, 255
  .b16 -32768, 65535
  .b32 -2147483648, 4294967295U
  .b64 -9223372036854775808, 18446744073709551615U
  .b32 .debug_abbrev
  .b64 Lbegin
  .b32 .debug_str+0x4
  .b64 Lbegin+12
  .b32 Lend-Lbegin
  .b64 Lbegin-Lend
Lend:
};
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& section = std::get<syntax_cst::CstSectionDirective>(
      result->module()->items.front());
  EXPECT_EQ(result->token(section.directive).kind, TokenKind::DotSection);
  EXPECT_EQ(result->token(section.name).text, ".debug_info");
  EXPECT_EQ(result->token(section.left_brace).kind, TokenKind::LBrace);
  EXPECT_EQ(result->token(section.right_brace).kind, TokenKind::RBrace);
  ASSERT_TRUE(section.terminator.has_value());
  ASSERT_EQ(section.payload.size(), 46u);
  EXPECT_EQ(result->token(section.payload.front()).text, "Lbegin");
  EXPECT_EQ(result->token(section.payload.at(1)).kind, TokenKind::Colon);
  EXPECT_EQ(result->token(section.payload.at(29)).text, ".debug_str");
  EXPECT_EQ(result->token(section.payload.at(30)).kind, TokenKind::Plus);
  EXPECT_EQ(result->token(section.payload.at(40)).text, ".b64");
  EXPECT_EQ(result->sourceRange(section.token_range).start.line, 1u);
}

TEST(PtxCstParser, RejectsMalformedOrNonlocalSectionDirectives) {
  for (const std::string_view source : {
           ".section",
           ".section .debug_info",
           ".section .debug_info { .b8 0",
           ".entry kernel() { .section .debug_info { .b8 0 } }",
           ".entry kernel() { { .section .debug_info { .b8 0 } } }",
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_TRUE(result.has_value()) << source;
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxCstParser, RetainsPragmasAtAllSupportedScopes) {
  constexpr std::string_view source = R"ptx(.pragma "module", "opaque";
.entry kernel() .pragma "nounroll"; {
  .pragma "frequency 32";
  {
    .pragma "nested", "opaque";
  }
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& module_pragma = std::get<syntax_cst::CstPragma>(
      result->module()->items.front());
  EXPECT_EQ(result->token(module_pragma.directive).kind, TokenKind::DotPragma);
  ASSERT_EQ(module_pragma.strings.size(), 2u);
  ASSERT_EQ(module_pragma.commas.size(), 1u);
  EXPECT_EQ(result->token(module_pragma.strings[1]).text, "\"opaque\"");

  const auto& function = std::get<syntax_cst::CstFunction>(
      result->module()->items[1]);
  ASSERT_EQ(function.pragmas.size(), 1u);
  EXPECT_EQ(result->token(function.pragmas[0].strings[0]).text,
            "\"nounroll\"");
  EXPECT_EQ(result->sourceRange(function.pragmas[0].token_range).start.line,
            2u);
  ASSERT_EQ(function.body.size(), 2u);
  const auto& body_pragma =
      std::get<syntax_cst::CstPragma>(function.body.front());
  EXPECT_EQ(result->sourceRange(body_pragma.token_range).start.line, 3u);
  const auto& block = *std::get<std::unique_ptr<syntax_cst::CstBlock>>(
      function.body[1]);
  const auto& nested_pragma =
      std::get<syntax_cst::CstPragma>(block.body.front());
  ASSERT_EQ(nested_pragma.strings.size(), 2u);
  EXPECT_EQ(result->token(nested_pragma.terminator).kind, TokenKind::Semicolon);
}

TEST(PtxCstParser, RejectsMalformedOrInvalidHeaderPragmas) {
  for (const std::string_view source : {
           ".pragma ;",
           ".pragma \"one\",;",
           ".pragma one;",
           ".pragma \"one\"",
           ".entry kernel() .pragma \"one\" {}",
           ".func device() .pragma \"one\"; {}",
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_TRUE(result.has_value()) << source;
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxCstParser, RetainsEntryKernelResourceDirectives) {
  constexpr std::string_view source = R"ptx(.entry kernel() .pragma "before";
    .maxnreg 32 .maxntid 16, 8, 4 .pragma "after";
    .minnctapersm 2 { }
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.pragmas.size(), 2u);
  ASSERT_EQ(function.resources.size(), 3u);
  const auto& maxnreg = function.resources[0];
  EXPECT_EQ(result->token(maxnreg.directive).kind, TokenKind::DotMaxnreg);
  ASSERT_EQ(maxnreg.values.size(), 1u);
  EXPECT_EQ(result->token(maxnreg.values[0]).text, "32");
  EXPECT_TRUE(maxnreg.commas.empty());
  const auto& maxntid = function.resources[1];
  EXPECT_EQ(result->token(maxntid.directive).kind, TokenKind::DotMaxntid);
  ASSERT_EQ(maxntid.values.size(), 3u);
  EXPECT_EQ(result->token(maxntid.values[2]).text, "4");
  ASSERT_EQ(maxntid.commas.size(), 2u);
  EXPECT_EQ(result->sourceRange(maxntid.token_range).start.line, 2u);
  EXPECT_EQ(result->token(function.resources[2].directive).kind,
            TokenKind::DotMinnctapersm);
}

TEST(PtxCstParser, ParsesRequiredThreadCountDimensions) {
  PtxCstParser parser(".entry kernel() .reqntid 16, 8 { }");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.resources.size(), 1u);
  EXPECT_EQ(result->token(function.resources[0].directive).kind,
            TokenKind::DotReqntid);
  EXPECT_EQ(function.resources[0].values.size(), 2u);
}

TEST(PtxCstParser, RejectsMalformedOrMisplacedKernelResourceDirectives) {
  for (const std::string_view source : {
           ".maxnreg 32",
           ".func device() .maxnreg 32 { }",
           ".entry kernel() { .maxnreg 32 }",
           ".entry kernel() { { .reqntid 32 } }",
           ".entry kernel() .maxnreg { }",
           ".entry kernel() .maxnreg 1, 2 { }",
           ".entry kernel() .maxntid 1, { }",
           ".entry kernel() .maxntid 1, 2, 3, 4 { }",
           ".entry kernel() .reqntid 1.0 { }",
           ".entry kernel() .minnctapersm 2; { }",
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_TRUE(result.has_value()) << source;
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxCstParser, RetainsNestedLocDirectiveStructure) {
  constexpr std::string_view source = R"ptx(.entry kernel() {
  .loc 0x2U 4237 0
  {
    .loc 1 9 3, function_name info_string0, inlined_at 1 21 3
    .loc 1 15 3, function_name .debug_str+16, inlined_at 0x1U 10 5;
    add.u32 %r0, %r1, %r2;
  }
}
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.body.size(), 2u);
  const auto& basic = std::get<syntax_cst::CstLocDirective>(function.body[0]);
  EXPECT_EQ(result->token(basic.directive).kind, TokenKind::DotLoc);
  EXPECT_EQ(result->token(basic.file_index).text, "0x2U");
  EXPECT_FALSE(basic.inline_context.has_value());
  EXPECT_FALSE(basic.terminator.has_value());
  EXPECT_EQ(result->sourceRange(basic.token_range).start.line, 2u);

  const auto& block = *std::get<std::unique_ptr<syntax_cst::CstBlock>>(
      function.body[1]);
  ASSERT_EQ(block.body.size(), 3u);
  const auto& named = std::get<syntax_cst::CstLocDirective>(block.body[0]);
  ASSERT_TRUE(named.inline_context.has_value());
  EXPECT_EQ(result->token(named.inline_context->function_name_label).text,
            "info_string0");

  const auto& dotted =
      std::get<syntax_cst::CstLocDirective>(block.body[1]);
  ASSERT_TRUE(dotted.inline_context.has_value());
  const auto& context = *dotted.inline_context;
  EXPECT_EQ(result->token(context.function_name_keyword).text,
            "function_name");
  EXPECT_EQ(result->token(context.function_name_label).text, ".debug_str");
  ASSERT_TRUE(context.plus.has_value());
  ASSERT_TRUE(context.function_name_offset.has_value());
  EXPECT_EQ(result->token(*context.function_name_offset).text, "16");
  EXPECT_EQ(result->token(context.inlined_at_keyword).text, "inlined_at");
  EXPECT_EQ(result->token(context.file_index).text, "0x1U");
  EXPECT_EQ(result->token(context.line_number).text, "10");
  EXPECT_TRUE(dotted.terminator.has_value());
}

TEST(PtxCstParser, RejectsMalformedOrModuleScopeLocDirectives) {
  for (const std::string_view source : {
           ".entry f() { .loc 1 2 }",
           ".entry f() { .loc 1 2 3, function_name name }",
           ".entry f() { .loc 1 2 3, inlined_at 1 2 3 }",
           ".entry f() { .loc 1 2 3, function_name name + 1.5, inlined_at 1 2 3 }",
           ".loc 1 2 3",
       }) {
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    EXPECT_TRUE(result.has_value()) << source;
    EXPECT_FALSE(result.diagnostics.empty()) << source;
  }
}

TEST(PtxCstParser, FindsFuncNameAfterReturnParameterList) {
  constexpr std::string_view source =
      ".func (.param .b32 result) helper(.param .b32 input) { ret; }";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_NE(result->module(), nullptr);
  ASSERT_EQ(result->module()->items.size(), 1u);
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items[0]);
  EXPECT_EQ(result->token(function.directive).kind, TokenKind::DotFunc);
  EXPECT_EQ(result->token(function.name).text, "helper");
  ASSERT_TRUE(function.return_parameters.has_value());
  ASSERT_EQ(function.return_parameters->parameters.size(), 1u);
  EXPECT_EQ(result->token(function.return_parameters->parameters[0].name).text,
            "result");
  ASSERT_TRUE(function.parameters.has_value());
  ASSERT_EQ(function.parameters->parameters.size(), 1u);
  EXPECT_EQ(result->token(function.parameters->parameters[0].name).text,
            "input");
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, ParsesParameterAttributesArraysAndPrototype) {
  constexpr std::string_view source =
      ".extern .func sink(.param .align 8 .b8 blob[], "
      ".param .u64 .ptr .global .align 16 ptr) .noreturn;";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items[0]);
  EXPECT_FALSE(function.left_brace.has_value());
  EXPECT_FALSE(function.right_brace.has_value());
  EXPECT_TRUE(function.terminator.has_value());
  EXPECT_TRUE(function.noreturn_directive.has_value());
  ASSERT_TRUE(function.parameters.has_value());
  ASSERT_EQ(function.parameters->parameters.size(), 2u);

  const auto& blob = function.parameters->parameters[0];
  EXPECT_EQ(result->token(blob.state_space).kind, TokenKind::DotParam);
  ASSERT_TRUE(blob.alignment.has_value());
  EXPECT_EQ(result->token(*blob.alignment).text, "8");
  EXPECT_EQ(result->token(blob.type).text, ".b8");
  EXPECT_TRUE(blob.left_bracket.has_value());
  EXPECT_FALSE(blob.array_size.has_value());

  const auto& pointer = function.parameters->parameters[1];
  EXPECT_TRUE(pointer.pointer_directive.has_value());
  ASSERT_TRUE(pointer.pointer_space.has_value());
  EXPECT_EQ(result->token(*pointer.pointer_space).kind, TokenKind::DotGlobal);
  ASSERT_TRUE(pointer.pointer_alignment.has_value());
  EXPECT_EQ(result->token(*pointer.pointer_alignment).text, "16");
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, RejectsUnsupportedFunctionHeaderTokens) {
  for (const std::string_view source : {
           ".func helper() unexpected;",
       }) {
    PtxCstParser parser(source);

    const auto result = parser.parseModule();

    ASSERT_TRUE(result.has_value()) << source;
    ASSERT_FALSE(result.diagnostics.empty()) << source;
    EXPECT_TRUE(
        result.diagnostics.front().message.starts_with(
            "unsupported function header token"))
        << source;
  }
}

TEST(PtxCstParser, ParsesRegisterDeclarationsAndLabels) {
  constexpr std::string_view source =
      ".entry kernel() { .reg .align 16 .u32 %r<3>, %tmp; loop: ret; }";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items[0]);
  ASSERT_EQ(function.body.size(), 3u);

  const auto& declaration =
      std::get<syntax_cst::CstVariableDeclaration>(function.body[0]);
  ASSERT_TRUE(declaration.alignment.has_value());
  EXPECT_EQ(result->token(*declaration.alignment).text, "16");
  EXPECT_EQ(result->token(declaration.type).text, ".u32");
  ASSERT_EQ(declaration.declarators.size(), 2u);
  EXPECT_EQ(result->token(declaration.declarators[0].name).text, "%r");
  ASSERT_TRUE(declaration.declarators[0].parameterized_count.has_value());
  EXPECT_EQ(result->token(*declaration.declarators[0].parameterized_count).text,
            "3");
  EXPECT_EQ(result->token(declaration.declarators[1].name).text, "%tmp");
  ASSERT_EQ(declaration.commas.size(), 1u);

  const auto& label = std::get<syntax_cst::CstLabel>(function.body[1]);
  EXPECT_EQ(result->token(label.name).text, "loop");
  EXPECT_EQ(result->token(label.colon).kind, TokenKind::Colon);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(function.body[2]));
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, RetainsNestedFunctionBlocks) {
  constexpr std::string_view source = R"ptx(.entry kernel() {
  {
    outer:
    { add.u32 %r0, %r1, %r2; }
  }
})ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  EXPECT_EQ(result->sourceText(), source);
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.body.size(), 1u);
  const auto& outer = *std::get<std::unique_ptr<CstBlock>>(function.body[0]);
  EXPECT_EQ(result->token(outer.left_brace).kind, TokenKind::LBrace);
  ASSERT_TRUE(outer.right_brace.has_value());
  EXPECT_EQ(result->token(*outer.right_brace).kind, TokenKind::RBrace);
  EXPECT_EQ(result->sourceRange(outer.token_range).start.line, 2u);
  ASSERT_EQ(outer.body.size(), 2u);
  const auto& inner = *std::get<std::unique_ptr<CstBlock>>(outer.body[1]);
  EXPECT_EQ(result->token(inner.left_brace).range.start.line, 4u);
  ASSERT_EQ(inner.body.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<syntax_cst::CstInstruction>(inner.body[0]));
}

TEST(PtxCstParser, RecoveryNodesPreserveSourceAndRecoveryInvariants) {
  constexpr std::string_view source =
      ".entry kernel() { add.u32 %r0, %r1, %r2; }";
  PtxCstParser parser(source);

  auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  auto file = std::move(*result);
  const auto original_source = file.sourceText();
  const auto token_count = file.tokens.size();
  auto& module = std::get<syntax_cst::CstModule>(file.root);
  auto& function = std::get<syntax_cst::CstFunction>(module.items.front());
  const auto& instruction =
      std::get<syntax_cst::CstInstruction>(function.body.front());
  const CstTokenRange first_token{instruction.token_range.first,
                                  instruction.token_range.first + 1};
  const auto first_range = file.sourceRange(first_token);
  const CstTokenRange second_token{instruction.token_range.first + 1,
                                   instruction.token_range.first + 2};
  const auto second_range = file.sourceRange(second_token);
  const auto eof_range = file.tokens.back().range;

  function.body.emplace_back(CstRecoveryNode{
      .kind = CstRecoveryKind::Skipped,
      .expected_kind = std::nullopt,
      .token_range = first_token,
      .range = first_range,
  });
  function.body.emplace_back(CstRecoveryNode{
      .kind = CstRecoveryKind::Error,
      .expected_kind = std::nullopt,
      .token_range = second_token,
      .range = second_range,
  });

  const auto& skipped =
      std::get<CstRecoveryNode>(function.body[function.body.size() - 2]);
  EXPECT_EQ(skipped.kind, CstRecoveryKind::Skipped);
  EXPECT_FALSE(skipped.expected_kind.has_value());
  ASSERT_TRUE(skipped.token_range.has_value());
  EXPECT_TRUE(skipped.token_range->first < skipped.token_range->last);
  EXPECT_EQ(file.sourceRange(*skipped.token_range), skipped.range);

  const auto& spanned_error =
      std::get<CstRecoveryNode>(function.body.back());
  EXPECT_EQ(spanned_error.kind, CstRecoveryKind::Error);
  EXPECT_FALSE(spanned_error.expected_kind.has_value());
  ASSERT_TRUE(spanned_error.token_range.has_value());
  EXPECT_TRUE(spanned_error.token_range->first <
              spanned_error.token_range->last);
  EXPECT_EQ(file.sourceRange(*spanned_error.token_range), spanned_error.range);

  module.items.emplace_back(CstRecoveryNode{
      .kind = CstRecoveryKind::Inserted,
      .expected_kind = TokenKind::Semicolon,
      .token_range = std::nullopt,
      .range = SourceRange{first_range.start, first_range.start},
  });
  module.items.emplace_back(CstRecoveryNode{
      .kind = CstRecoveryKind::Error,
      .expected_kind = std::nullopt,
      .token_range = std::nullopt,
      .range = eof_range,
  });

  const auto& inserted =
      std::get<CstRecoveryNode>(module.items[module.items.size() - 2]);
  EXPECT_EQ(inserted.kind, CstRecoveryKind::Inserted);
  ASSERT_TRUE(inserted.expected_kind.has_value());
  EXPECT_EQ(*inserted.expected_kind, TokenKind::Semicolon);
  EXPECT_FALSE(inserted.token_range.has_value());
  EXPECT_EQ(inserted.range.start, inserted.range.end);

  const auto& eof_error = std::get<CstRecoveryNode>(module.items.back());
  EXPECT_EQ(eof_error.kind, CstRecoveryKind::Error);
  EXPECT_FALSE(eof_error.expected_kind.has_value());
  EXPECT_FALSE(eof_error.token_range.has_value());
  EXPECT_EQ(eof_error.range, eof_range);
  EXPECT_EQ(eof_error.range.start, eof_error.range.end);
  EXPECT_EQ(file.tokens.size(), token_count);
  EXPECT_EQ(file.sourceText(), original_source);
}

TEST(PtxCstParser, RecoversMissingBodySemicolonBeforeNextInstruction) {
  constexpr std::string_view source = R"ptx(.entry kernel() {
  add.u32 %r0, %r1, %r2
  sub.u32 %r3, %r4, %r5;
})ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics.front().message, "expected ';'");
  EXPECT_EQ(result->sourceText(), source);
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.body.size(), 3u);
  const auto& skipped = std::get<CstRecoveryNode>(function.body[0]);
  EXPECT_EQ(skipped.kind, CstRecoveryKind::Skipped);
  ASSERT_TRUE(skipped.token_range.has_value());
  const auto& inserted = std::get<CstRecoveryNode>(function.body[1]);
  EXPECT_EQ(inserted.kind, CstRecoveryKind::Inserted);
  EXPECT_EQ(inserted.expected_kind, TokenKind::Semicolon);
  EXPECT_FALSE(inserted.token_range.has_value());
  EXPECT_EQ(inserted.range.start, inserted.range.end);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(function.body[2]));
}

TEST(PtxCstParser, RecoversNestedBlockAndModuleItemsInSourceOrder) {
  constexpr std::string_view source = R"ptx(.version nope;
.target;
.entry kernel() { { add.u32 %r0, %r1, %r2 sub.u32 %r3, %r4, %r5; } }
)ptx";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 3u);
  EXPECT_EQ(result.diagnostics[0].message, "expected PTX version");
  EXPECT_EQ(result.diagnostics[1].message, "expected target architecture");
  EXPECT_EQ(result.diagnostics[2].message, "expected ';'");
  EXPECT_EQ(result->sourceText(), source);
  const auto& module = *result->module();
  ASSERT_EQ(module.items.size(), 6u);
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(module.items[0]));
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(module.items[1]));
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(module.items[2]));
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(module.items[3]));
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(module.items[4]));
  const auto& function = std::get<syntax_cst::CstFunction>(module.items[5]);
  const auto& block = *std::get<std::unique_ptr<CstBlock>>(function.body[0]);
  ASSERT_EQ(block.body.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(block.body[0]));
  EXPECT_TRUE(std::holds_alternative<CstRecoveryNode>(block.body[1]));
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(block.body[2]));
  EXPECT_EQ(std::get<CstRecoveryNode>(module.items[1]).kind,
            CstRecoveryKind::Error);
}

TEST(PtxCstParser, RecoversMissingFunctionBraceBeforeNextFunctionAndAtEof) {
  constexpr std::string_view next_function_source = R"ptx(.entry first() {
  add.u32 %r0, %r1, %r2;
.version 9.3
.entry second() { sub.u32 %r3, %r4, %r5; }
)ptx";
  PtxCstParser next_function_parser(next_function_source);
  const auto next_function = next_function_parser.parseModule();

  ASSERT_TRUE(next_function.has_value());
  ASSERT_EQ(next_function.diagnostics.size(), 1u);
  EXPECT_EQ(next_function.diagnostics.front().message,
            "expected '}' at end of function body");
  ASSERT_EQ(next_function->module()->items.size(), 3u);
  const auto& first = std::get<syntax_cst::CstFunction>(
      next_function->module()->items.front());
  EXPECT_FALSE(first.right_brace.has_value());
  EXPECT_EQ(std::get<CstRecoveryNode>(first.body.back()).expected_kind,
            TokenKind::RBrace);
  EXPECT_TRUE(std::holds_alternative<syntax_cst::CstModuleDirective>(
      next_function->module()->items[1]));
  EXPECT_TRUE(std::holds_alternative<syntax_cst::CstFunction>(
      next_function->module()->items[2]));
  EXPECT_EQ(next_function->sourceText(), next_function_source);

  constexpr std::string_view eof_source = ".entry lone() {";
  PtxCstParser eof_parser(eof_source);
  const auto eof = eof_parser.parseModule();

  ASSERT_TRUE(eof.has_value());
  ASSERT_EQ(eof.diagnostics.size(), 1u);
  EXPECT_EQ(eof.diagnostics.front().message,
            "expected '}' at end of function body");
  EXPECT_EQ(eof->sourceText(), eof_source);
}

TEST(PtxCstParser, RecoversStrayModuleTokenAndEofWithoutSyntheticTokens) {
  constexpr std::string_view source = "} .version";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 2u);
  EXPECT_EQ(result.diagnostics[0].message,
            "expected module directive, variable declaration, or function");
  EXPECT_EQ(result.diagnostics[1].message, "expected PTX version");
  EXPECT_EQ(result->sourceText(), source);
  const auto& module = *result->module();
  ASSERT_EQ(module.items.size(), 3u);
  EXPECT_EQ(std::get<CstRecoveryNode>(module.items.back()).kind,
            CstRecoveryKind::Error);
  EXPECT_FALSE(std::get<CstRecoveryNode>(module.items.back())
                   .token_range.has_value());
  EXPECT_EQ(result->tokens.back().kind, TokenKind::Eof);
}

TEST(PtxCstParser, RejectsNestedBlockMissingRightBrace) {
  PtxCstParser parser(".entry kernel() { { add.u32 %r0, %r1, %r2;");

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result.diagnostics.size(), 2u);
  EXPECT_EQ(result.diagnostics[0].message,
            "expected '}' at end of nested block");
  EXPECT_EQ(result.diagnostics[1].message,
            "expected '}' at end of function body");
  EXPECT_EQ(result->sourceText(), ".entry kernel() { { add.u32 %r0, %r1, %r2;");
  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items.front());
  ASSERT_EQ(function.body.size(), 2u);
  const auto& block = *std::get<std::unique_ptr<CstBlock>>(function.body[0]);
  EXPECT_FALSE(block.right_brace.has_value());
  ASSERT_EQ(block.body.size(), 2u);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstInstruction>(block.body[0]));
  const auto& inserted = std::get<CstRecoveryNode>(block.body[1]);
  EXPECT_EQ(inserted.kind, CstRecoveryKind::Inserted);
  EXPECT_EQ(inserted.expected_kind, TokenKind::RBrace);
}

TEST(PtxCstParser, DiagnosesExcessiveNestedBlockDepthWithoutCrashing) {
  constexpr size_t nested_blocks = 257;
  std::string source = ".entry kernel() {";
  source.append(nested_blocks, '{');
  source.append(nested_blocks, '}');
  source.push_back('}');
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->sourceText(), source);
  bool diagnosed_depth = false;
  for (const auto& diagnostic : result.diagnostics) {
    diagnosed_depth |=
        diagnostic.message == "nested block depth exceeds parser limit";
  }
  EXPECT_TRUE(diagnosed_depth);
}

TEST(PtxCstParser, ParsesModuleAndFunctionVariableDeclarations) {
  constexpr std::string_view source =
      ".visible .global .align 16 .v4 .f32 values[2][3];\n"
      ".entry kernel() {\n"
      "  .shared .v2 .u16 tile[8];\n"
      "  .local .u32 scratch[19][19];\n"
      "  .param .align 8 .b8 argument[12];\n"
      "  ret;\n"
      "}";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->module()->items.size(), 2u);
  const auto& global =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[0]);
  ASSERT_EQ(global.qualifiers.size(), 1u);
  EXPECT_EQ(result->token(global.qualifiers[0]).kind, TokenKind::DotVisible);
  EXPECT_EQ(result->token(global.state_space).kind, TokenKind::DotGlobal);
  ASSERT_TRUE(global.vector_type.has_value());
  EXPECT_EQ(result->token(*global.vector_type).text, ".v4");
  ASSERT_EQ(global.declarators[0].array_dimensions.size(), 2u);
  ASSERT_TRUE(global.declarators[0].array_dimensions[1].size.has_value());
  const auto& dimension_literal = std::get<syntax_cst::CstConstantLiteral>(
      global.declarators[0].array_dimensions[1].size->node);
  EXPECT_EQ(result->token(dimension_literal.literal).text, "3");

  const auto& function =
      std::get<syntax_cst::CstFunction>(result->module()->items[1]);
  ASSERT_EQ(function.body.size(), 4u);
  const auto& shared =
      std::get<syntax_cst::CstVariableDeclaration>(function.body[0]);
  EXPECT_EQ(result->token(shared.state_space).kind, TokenKind::DotShared);
  const auto& local =
      std::get<syntax_cst::CstVariableDeclaration>(function.body[1]);
  EXPECT_EQ(result->token(local.state_space).kind, TokenKind::DotLocal);
  ASSERT_EQ(local.declarators[0].array_dimensions.size(), 2u);
  const auto& parameter =
      std::get<syntax_cst::CstVariableDeclaration>(function.body[2]);
  EXPECT_EQ(result->token(parameter.state_space).kind, TokenKind::DotParam);
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, RetainsStructuredConstantExpressionsAndInitializers) {
  constexpr std::string_view source =
      ".global .u32 table[(1 + 2) << 3] = {1, 2 * 3, WARP_SZ};\n"
      ".const .u64 pointer = generic(base) + 8;\n"
      ".global .u8 masked = 0xff(base + 4);\n"
      ".global .u32 first = 1, second = 2;";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  ASSERT_EQ(result->module()->items.size(), 4u);
  const auto& table =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[0]);
  const auto& declarator = table.declarators[0];
  ASSERT_EQ(declarator.array_dimensions.size(), 1u);
  ASSERT_TRUE(declarator.array_dimensions[0].size.has_value());
  const auto& shift = std::get<syntax_cst::CstConstantBinary>(
      declarator.array_dimensions[0].size->node);
  EXPECT_EQ(result->token(shift.operator_token).kind, TokenKind::ShiftLeft);
  ASSERT_TRUE(std::holds_alternative<syntax_cst::CstConstantParenthesized>(
      shift.left->node));

  ASSERT_TRUE(declarator.equals.has_value());
  ASSERT_TRUE(declarator.initializer.has_value());
  const auto& list =
      std::get<syntax_cst::CstInitializerList>(declarator.initializer->value);
  ASSERT_EQ(list.elements.size(), 3u);
  ASSERT_EQ(list.commas.size(), 2u);
  const auto& product = std::get<syntax_cst::CstConstantBinary>(
      std::get<syntax_cst::CstConstantExpression>(list.elements[1].value).node);
  EXPECT_EQ(result->token(product.operator_token).kind, TokenKind::Star);
  const auto& warp_size = std::get<syntax_cst::CstConstantLiteral>(
      std::get<syntax_cst::CstConstantExpression>(list.elements[2].value).node);
  EXPECT_EQ(result->token(warp_size.literal).kind, TokenKind::WarpSz);

  const auto& pointer =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[1]);
  const auto& pointer_expression = std::get<syntax_cst::CstConstantExpression>(
      pointer.declarators[0].initializer->value);
  const auto& add =
      std::get<syntax_cst::CstConstantBinary>(pointer_expression.node);
  EXPECT_EQ(result->token(add.operator_token).kind, TokenKind::Plus);
  ASSERT_TRUE(
      std::holds_alternative<syntax_cst::CstConstantCall>(add.left->node));

  const auto& masked =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[2]);
  const auto& masked_expression = std::get<syntax_cst::CstConstantExpression>(
      masked.declarators[0].initializer->value);
  const auto& mask_call =
      std::get<syntax_cst::CstConstantCall>(masked_expression.node);
  const auto& mask_literal =
      std::get<syntax_cst::CstConstantLiteral>(mask_call.callee->node);
  EXPECT_EQ(result->token(mask_literal.literal).text, "0xff");

  const auto& multiple =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[3]);
  ASSERT_EQ(multiple.declarators.size(), 2u);
  ASSERT_EQ(multiple.commas.size(), 1u);
  EXPECT_EQ(result->token(multiple.declarators[1].name).text, "second");
  EXPECT_TRUE(multiple.declarators[1].initializer.has_value());
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, RetainsNestedAndUnsizedArrayInitializer) {
  constexpr std::string_view source =
      ".global .s32 offsets[][2] = {{-1, 0}, {0, -1}};";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.diagnostics.front().message;
  const auto& declaration =
      std::get<syntax_cst::CstVariableDeclaration>(result->module()->items[0]);
  const auto& declarator = declaration.declarators[0];
  ASSERT_EQ(declarator.array_dimensions.size(), 2u);
  EXPECT_FALSE(declarator.array_dimensions[0].size.has_value());
  EXPECT_TRUE(declarator.array_dimensions[1].size.has_value());
  const auto& outer =
      std::get<syntax_cst::CstInitializerList>(declarator.initializer->value);
  ASSERT_EQ(outer.elements.size(), 2u);
  for (const auto& row : outer.elements) {
    const auto& inner = std::get<syntax_cst::CstInitializerList>(row.value);
    EXPECT_EQ(inner.elements.size(), 2u);
  }
  const auto& first_value = std::get<syntax_cst::CstConstantExpression>(
      std::get<syntax_cst::CstInitializerList>(outer.elements[0].value)
          .elements[0]
          .value);
  EXPECT_TRUE(
      std::holds_alternative<syntax_cst::CstConstantUnary>(first_value.node));
  EXPECT_EQ(result->sourceText(), source);
}

TEST(PtxCstParser, RejectsInitializersInUnsupportedDeclarations) {
  for (const auto source_and_message : {
           std::pair{std::string_view{".reg .u32 value = 1;"},
                     std::string_view{
                         "variable initializer requires '.global' or '.const' "
                         "state space"}},
           std::pair{
               std::string_view{".extern .global .u32 value = 1;"},
               std::string_view{"external variable declaration cannot have an "
                                "initializer"}},
           std::pair{
               std::string_view{".global .u32 values<2>[4];"},
               std::string_view{"parameterized variable names cannot declare "
                                "arrays"}},
           std::pair{
               std::string_view{".reg .u32 values<2> = 1;"},
               std::string_view{"parameterized variable names cannot have an "
                                "initializer"}},
       }) {
    PtxCstParser parser(source_and_message.first);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value()) << source_and_message.first;
    ASSERT_FALSE(result.diagnostics.empty()) << source_and_message.first;
    EXPECT_EQ(result.diagnostics.front().message, source_and_message.second);
  }
}

}  // namespace
}  // namespace ptx_frontend
