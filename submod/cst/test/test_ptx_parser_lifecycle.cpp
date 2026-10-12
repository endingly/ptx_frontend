#include <gtest/gtest.h>

#include <array>
#include <string>
#include <type_traits>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {

static_assert(!std::is_copy_constructible_v<PtxCstParser>);
static_assert(!std::is_copy_assignable_v<PtxCstParser>);
static_assert(!std::is_move_constructible_v<PtxCstParser>);
static_assert(!std::is_move_assignable_v<PtxCstParser>);

/** Require repeatable EOF failures after a result transfers scanner-owned tokens. */
void expectExhausted(PtxCstParser& parser, SourceRange eof) {
  for (int iteration = 0; iteration < 2; ++iteration) {
    const auto instruction = parser.parseInstruction();
    ASSERT_FALSE(instruction.has_value());
    ASSERT_EQ(instruction.diagnostics.size(), 1u);
    EXPECT_EQ(instruction.diagnostics.front().message,
              "expected instruction opcode");
    EXPECT_EQ(instruction.diagnostics.front().expected_kind, TokenKind::Ident);
    EXPECT_EQ(instruction.diagnostics.front().range, eof);
    const auto module = parser.parseModule();
    ASSERT_FALSE(module.has_value());
    ASSERT_EQ(module.diagnostics.size(), 1u);
    EXPECT_EQ(module.diagnostics.front().message, "expected module item");
    EXPECT_FALSE(module.diagnostics.front().expected_kind.has_value());
    EXPECT_EQ(module.diagnostics.front().range, eof);
  }
}

/** Copy caller input once and transfer independently owned tokens to the result. */
TEST(PtxParserLifecycle, OwnsInputAndReturnedFile) {
  std::string input = " /*before*/ ret; //EOF\n";
  const auto expected = input;
  PtxCstParser parser(input);
  input.assign(input.size(), 'x');
  const auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result->sourceText(), expected);
  ASSERT_EQ(result->tokens.back().kind, TokenKind::Eof);
  expectExhausted(parser, result->tokens.back().range);

  const auto retained = [] {
    PtxCstParser temporary(std::string{"/*temporary*/ ret; //owned"});
    return temporary.parseInstruction();
  }();
  ASSERT_TRUE(retained.has_value());
  EXPECT_EQ(retained->sourceText(), "/*temporary*/ ret; //owned");
  ASSERT_NE(retained->instruction(), nullptr);
  EXPECT_EQ(retained->token(retained->instruction()->opcode).text, "ret");
}

/** Clean and recovered module values both exhaust the same persistent facade. */
TEST(PtxParserLifecycle, ExhaustsAfterCleanOrRecoveredModuleTransfer) {
  constexpr std::array sources{".entry k() { ret; } //EOF\n",
                               ".entry k() { mov.u32 %r, ; ret; } //EOF\n"};
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    PtxCstParser parser(source);
    const auto result = parser.parseModule();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sourceText(), source);
    ASSERT_NE(result->module(), nullptr);
    EXPECT_EQ(result.diagnostics.size(),
              std::string_view(source).find("mov") == std::string_view::npos
                  ? 0u
                  : 1u);
    expectExhausted(parser, result->tokens.back().range);
  }
}

/** Failed end-of-input checks preserve the next root and all consumed prefix tokens. */
TEST(PtxParserLifecycle, ResumesInstructionAndModuleAfterTrailingInput) {
  constexpr std::array suffixes{"mov.u32 %r, 1;", ".entry k() { ret; }"};
  for (const auto suffix : suffixes) {
    const std::string source = "ret; " + std::string(suffix) + " //EOF\n";
    SCOPED_TRACE(source);
    PtxCstParser parser(source);
    const auto first = parser.parseInstruction();
    ASSERT_FALSE(first.has_value());
    ASSERT_EQ(first.diagnostics.size(), 1u);
    EXPECT_EQ(first.diagnostics.front().message, "expected end of input");
    const bool module = std::string_view(suffix).starts_with(".entry");
    const auto resumed =
        module ? parser.parseModule() : parser.parseInstruction();
    ASSERT_TRUE(resumed.has_value());
    EXPECT_TRUE(resumed.diagnostics.empty());
    EXPECT_EQ(resumed->sourceText(), source);
    const auto range = module ? resumed->module()->token_range
                              : resumed->instruction()->token_range;
    EXPECT_EQ(range.first, 2u);
    EXPECT_EQ(resumed->sourceRange(range).start,
              first.diagnostics.front().range.start);
    expectExhausted(parser, resumed->tokens.back().range);
  }
}

/** Syntax errors retain the offending token rather than restarting the source. */
TEST(PtxParserLifecycle, RetainsLookaheadAfterNoValueFailure) {
  for (const auto source : {"mov.u32 %r, ; ret;", "? ret;"}) {
    SCOPED_TRACE(source);
    PtxCstParser parser(source);
    const auto first = parser.parseInstruction();
    ASSERT_FALSE(first.has_value());
    ASSERT_EQ(first.diagnostics.size(), 1u);
    const auto second = parser.parseInstruction();
    ASSERT_FALSE(second.has_value());
    ASSERT_EQ(second.diagnostics.size(), 1u);
    EXPECT_EQ(second.diagnostics.front().message,
              "expected instruction opcode");
    EXPECT_EQ(second.diagnostics.front().range,
              first.diagnostics.front().range);
    const auto recovered = parser.parseModule();
    ASSERT_TRUE(recovered.has_value());
    ASSERT_FALSE(recovered.diagnostics.empty());
    EXPECT_EQ(recovered.diagnostics.front().range,
              first.diagnostics.front().range);
    EXPECT_EQ(recovered->sourceText(), source);
    expectExhausted(parser, recovered->tokens.back().range);
  }
}

/** Semantic-action failures resume at the exact cursor observed in the old parser. */
TEST(PtxParserLifecycle, RetainsCursorAfterDepthAndConstantActionFailures) {
  const std::array expressions{std::string(128, '+') + "1",
                               std::string(128, '('), std::string{"(1+%r)"},
                               std::string{"1+%r"}};
  for (const auto& expression : expressions) {
    const std::string source = "mov.u32 %r, " + expression + "; ret;";
    SCOPED_TRACE(source);
    PtxCstParser parser(source);
    const auto first = parser.parseInstruction();
    ASSERT_FALSE(first.has_value());
    ASSERT_EQ(first.diagnostics.size(), 1u);
    EXPECT_EQ(first.diagnostics.front().message,
              expression.size() >= 128
                  ? "constant expression tree depth limit exceeded"
                  : "registers cannot participate in constant expressions");
    const auto second = parser.parseInstruction();
    ASSERT_FALSE(second.has_value());
    ASSERT_EQ(second.diagnostics.size(), 1u);
    EXPECT_EQ(second.diagnostics.front().message,
              "expected instruction opcode");
    const auto cursor =
        expression.front() == '+' ? source.find(';') - 1 : source.find(';');
    EXPECT_EQ(second.diagnostics.front().range.start,
              (SourcePos{1, static_cast<int32_t>(cursor + 1)}));
    const auto recovered = parser.parseModule();
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->sourceText(), source);
    ASSERT_FALSE(recovered.diagnostics.empty());
    EXPECT_EQ(recovered.diagnostics.front().range,
              second.diagnostics.front().range);
    expectExhausted(parser, recovered->tokens.back().range);
  }
}

/** Empty no-value entries preserve their EOF anchor across repeated/mixed calls. */
TEST(PtxParserLifecycle, RepeatsEmptyEntriesWithoutTransferringTokens) {
  PtxCstParser parser("");
  for (int iteration = 0; iteration < 2; ++iteration) {
    const auto module = parser.parseModule();
    ASSERT_FALSE(module.has_value());
    ASSERT_EQ(module.diagnostics.size(), 1u);
    EXPECT_EQ(module.diagnostics.front().message, "expected module item");
    const auto instruction = parser.parseInstruction();
    ASSERT_FALSE(instruction.has_value());
    ASSERT_EQ(instruction.diagnostics.size(), 1u);
    EXPECT_EQ(instruction.diagnostics.front().message,
              "expected instruction opcode");
    EXPECT_EQ(module.diagnostics.front().range,
              instruction.diagnostics.front().range);
    EXPECT_EQ(module.diagnostics.front().range, (SourceRange{{1, 1}, {1, 1}}));
  }
}

}  // namespace
}  // namespace ptx_frontend
