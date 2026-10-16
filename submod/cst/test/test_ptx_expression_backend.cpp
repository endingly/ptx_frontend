#include <gtest/gtest.h>

#include <array>
#include <string>
#include <type_traits>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>

namespace ptx_frontend {
namespace {

using namespace syntax_cst;

/** Serialize expression structure and every token/subexpression source range. */
std::string expressionSnapshot(const CstFile& file,
                               const CstConstantExpression& expression) {
  const auto range = file.sourceRange(expression.token_range);
  const auto token = [&](TokenId id) {
    const auto& value = file.token(id);
    return value.text + "@" + std::to_string(id) + ":" +
           std::to_string(value.range.start.line) + ":" +
           std::to_string(value.range.start.column) + "-" +
           std::to_string(value.range.end.line) + ":" +
           std::to_string(value.range.end.column);
  };
  const auto child = [&](const CstConstantExpressionPtr& value) {
    return expressionSnapshot(file, *value);
  };
  std::string result = std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, CstConstantLiteral>)
          return "literal(" + token(node.literal) + ")";
        else if constexpr (std::is_same_v<T, CstConstantSymbol>)
          return "symbol(" + token(node.name) + ")";
        else if constexpr (std::is_same_v<T, CstConstantParenthesized>)
          return "paren(" + token(node.left_paren) + "," +
                 child(node.expression) + "," + token(node.right_paren) + ")";
        else if constexpr (std::is_same_v<T, CstConstantCall>)
          return "call(" + child(node.callee) + "," + token(node.left_paren) +
                 "," + child(node.argument) + "," + token(node.right_paren) +
                 ")";
        else if constexpr (std::is_same_v<T, CstConstantCast>)
          return "cast(" + token(node.left_paren) + "," + token(node.type) +
                 "," + token(node.right_paren) + "," + child(node.operand) +
                 ")";
        else if constexpr (std::is_same_v<T, CstConstantUnary>)
          return "unary(" + token(node.operator_token) + "," +
                 child(node.operand) + ")";
        else if constexpr (std::is_same_v<T, CstConstantBinary>)
          return "binary(" + child(node.left) + "," +
                 token(node.operator_token) + "," + child(node.right) + ")";
        else
          return "conditional(" + child(node.condition) + "," +
                 token(node.question) + "," + child(node.true_expression) +
                 "," + token(node.colon) + "," + child(node.false_expression) +
                 ")";
      },
      expression.node);
  return result + "[" + std::to_string(expression.token_range.first) + "," +
         std::to_string(expression.token_range.last) + ";" +
         std::to_string(range.start.line) + ":" +
         std::to_string(range.start.column) + "-" +
         std::to_string(range.end.line) + ":" +
         std::to_string(range.end.column) + "]";
}

/** Snapshot valid trees across every expression entry using either backend. */
TEST(ExpressionBackend, RecordsCompleteExpressionTreesAndRanges) {
  constexpr std::array expressions{"42",
                                   "symbol",
                                   "-1",
                                   "+~!2",
                                   "(.s64)-(.u64)(1+2)",
                                   "generic(symbol)+0xff(3)(4)",
                                   "1+2*3-4/2 % 2",
                                   "8<<1+2>>1",
                                   "1<2<=3>4>=5==6!=7&8^9|10&&11||12",
                                   "1?2:3?4:5",
                                   "1?2?3:4:5",
                                   "(1?2:3)+4",
                                   "0d3ff0000000000000+2.0"};
  std::string snapshot;
  for (const auto expression : expressions) {
    const std::string source = "/*before*/ .global .u64 value[" +
                               std::string{expression} + "] = /*scalar*/ " +
                               expression + "; //eof\n";
    SCOPED_TRACE(source);
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed.diagnostics.empty())
        << parsed.diagnostics.front().message;
    EXPECT_EQ(parsed->sourceText(), source);
    const auto& declaration =
        std::get<CstVariableDeclaration>(parsed->module()->items[0]);
    const auto& declarator = declaration.declarators[0];
    ASSERT_TRUE(declarator.array_dimensions[0].size);
    ASSERT_TRUE(declarator.initializer);
    snapshot +=
        expressionSnapshot(*parsed, *declarator.array_dimensions[0].size) +
        "\n";
    snapshot +=
        expressionSnapshot(*parsed, std::get<CstConstantExpression>(
                                        declarator.initializer->value)) +
        "\n";
    const auto numeric =
        PtxCstParser("mov.u64 %rd, (" + std::string{expression} + ");")
            .parseInstruction();
    ASSERT_TRUE(numeric.has_value()) << numeric.diagnostics.front().message;
    const auto& operand = std::get<CstConstantOperand>(
        numeric->instruction()->operands[1].operand);
    snapshot += expressionSnapshot(*numeric, *operand.expression) + "\n";
  }
  RecordProperty("expression_snapshot", snapshot);
}

/** Address base parsing stops before additive operators and preserves parentheses. */
TEST(ExpressionBackend, PreservesAddressPrecedenceBoundaryAndNumericShapes) {
  const std::string source =
      "/*lead*/ mov.u64 %rd, [1*2+3], [(1+2)*3-4], -7, +8; //tail\n";
  const auto parsed = PtxCstParser(source).parseInstruction();
  ASSERT_TRUE(parsed.has_value()) << parsed.diagnostics.front().message;
  EXPECT_TRUE(parsed.diagnostics.empty());
  EXPECT_EQ(parsed->sourceText(), source);
  const auto& operands = parsed->instruction()->operands;
  const auto& first = std::get<CstAddress>(operands[1].operand);
  const auto& second = std::get<CstAddress>(operands[2].operand);
  ASSERT_TRUE(first.offset);
  ASSERT_TRUE(second.offset);
  EXPECT_EQ(parsed->token(first.offset->operator_token).text, "+");
  EXPECT_EQ(parsed->token(second.offset->operator_token).text, "-");
  const auto& first_expression =
      *std::get<CstConstantOperand>(first.base).expression;
  const auto& second_expression =
      *std::get<CstConstantOperand>(second.base).expression;
  EXPECT_EQ(
      parsed
          ->token(
              std::get<CstConstantBinary>(first_expression.node).operator_token)
          .text,
      "*");
  EXPECT_EQ(parsed
                ->token(std::get<CstConstantBinary>(second_expression.node)
                            .operator_token)
                .text,
            "*");
  EXPECT_TRUE(std::holds_alternative<CstImmediate>(operands[3].operand));
  EXPECT_TRUE(std::holds_alternative<CstImmediate>(operands[4].operand));
  RecordProperty("address_snapshot",
                 expressionSnapshot(*parsed, first_expression) + "\n" +
                     expressionSnapshot(*parsed, second_expression));
}

/** Syntax failures retain offending lookahead for standalone errors and recovery. */
TEST(ExpressionBackend, RetainsMalformedLookaheadDiagnosticsAndNeighbors) {
  /** Expected diagnostics are also part of the handwritten recovery protocol. */
  struct Case {
    std::string text;
    std::string message;
    std::optional<TokenKind> expected;
  };
  const std::array cases{
      Case{"1+;", "expected constant expression", std::nullopt},
      Case{"(1+2;", "expected ')'", TokenKind::RParen},
      Case{"(.u64 1;", "expected ')' after constant cast", TokenKind::RParen},
      Case{"1?2;", "expected ':' in conditional expression", TokenKind::Colon},
      Case{"1?2:;", "expected constant expression", std::nullopt},
      Case{"(1?2:3 bad);", "expected ')'", TokenKind::RParen},
      Case{"1(1?2:3 bad);", "expected ')' after initializer operator",
           TokenKind::RParen},
      Case{"((.u64)1 bad);", "expected ')'", TokenKind::RParen},
      Case{"((.u64 1);", "expected ')' after constant cast", TokenKind::RParen},
      Case{"(1 `;", "expected ')'", TokenKind::RParen},
      Case{"(1 $;", "expected ')'", TokenKind::RParen},
      Case{"1", "expected ';'", TokenKind::Semicolon}};
  std::string snapshot;
  for (const auto& fixture : cases) {
    const std::string fragment = "mov.u32 %r0, " + fixture.text;
    SCOPED_TRACE(fragment);
    const auto standalone = PtxCstParser(fragment).parseInstruction();
    ASSERT_FALSE(standalone.has_value());
    ASSERT_EQ(standalone.diagnostics.size(), 1u);
    EXPECT_EQ(standalone.diagnostics.front().message, fixture.message);
    EXPECT_EQ(standalone.diagnostics.front().expected_kind, fixture.expected);
    const std::string source =
        ".entry first() {\n" + fragment +
        "\nmov.u32 %r1, 7;\n}\n.entry second() { ret; } //tail\n";
    const auto module = PtxCstParser(source).parseModule();
    ASSERT_TRUE(module.has_value());
    ASSERT_EQ(module.diagnostics.size(), 1u);
    EXPECT_EQ(module->sourceText(), source);
    EXPECT_EQ(module.diagnostics.front().message, fixture.message);
    EXPECT_EQ(module.diagnostics.front().expected_kind, fixture.expected);
    ASSERT_EQ(module->module()->items.size(), 2u);
    const auto& first = std::get<CstFunction>(module->module()->items[0]);
    ASSERT_GE(first.body.size(), 2u);
    const auto& survivor = std::get<CstInstruction>(first.body.back());
    EXPECT_EQ(module->token(survivor.opcode).text, "mov");
    const auto& diagnostic = module.diagnostics.front();
    snapshot += diagnostic.message + ":" +
                std::to_string(diagnostic.range.start.line) + ":" +
                std::to_string(diagnostic.range.start.column) + "-" +
                std::to_string(diagnostic.range.end.line) + ":" +
                std::to_string(diagnostic.range.end.column) + "\n";
    for (const auto& item : first.body) {
      if (const auto* recovery = std::get_if<CstRecoveryNode>(&item)) {
        snapshot +=
            "recovery:" + std::to_string(static_cast<int>(recovery->kind)) +
            (recovery->token_range
                 ? ":" + std::to_string(recovery->token_range->first) + ":" +
                       std::to_string(recovery->token_range->last)
                 : ":inserted") +
            "\n";
      }
    }
  }
  RecordProperty("diagnostic_snapshot", snapshot);
}

/** Partial prefixes exhaust the depth budget before their missing operand. */
TEST(ExpressionBackend, PreservesMalformedDepthDiagnostics) {
  std::string snapshot;
  for (const auto prefix : {std::string{"+"}, std::string{"("},
                            std::string{"(.u64)"}, std::string{"1?1:"}}) {
    for (const auto count : {127u, 128u, 4096u}) {
      std::string source = "mov.u32 %r, ";
      for (unsigned i = 0; i < count; ++i)
        source += prefix;
      source += ";";
      SCOPED_TRACE(prefix + " count=" + std::to_string(count));
      const auto parsed = PtxCstParser(source).parseInstruction();
      ASSERT_FALSE(parsed.has_value());
      ASSERT_EQ(parsed.diagnostics.size(), 1u);
      const auto& diagnostic = parsed.diagnostics.front();
      if (count >= PtxCstParser::maxConstantTreeDepth)
        EXPECT_EQ(diagnostic.message,
                  "constant expression tree depth limit exceeded");
      else
        EXPECT_EQ(diagnostic.message, "expected constant expression");
      snapshot += prefix + ":" + std::to_string(count) + ":" +
                  diagnostic.message + ":" +
                  std::to_string(diagnostic.range.start.column) + ":" +
                  std::to_string(diagnostic.range.end.column) + "\n";
    }
  }
  RecordProperty("depth_snapshot", snapshot);
}

/** Named members and brace elements reduce the shared expression budget. */
TEST(ExpressionBackend, PreservesNamedAndListInitializerBudgets) {
  std::string snapshot;
  for (const auto count : {125u, 126u, 127u}) {
    const std::string source =
        ".global .u64 value[] = {member=" + std::string(count, '+') +
        "1};\n.global .u64 survivor = 7; //eof\n";
    const auto parsed = PtxCstParser(source).parseModule();
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->sourceText(), source);
    if (count < 126u) {
      ASSERT_TRUE(parsed.diagnostics.empty());
      const auto& declaration =
          std::get<CstVariableDeclaration>(parsed->module()->items[0]);
      const auto& list = std::get<CstInitializerList>(
          declaration.declarators[0].initializer->value);
      const auto& named = std::get<CstNamedInitializer>(list.elements[0].value);
      snapshot += expressionSnapshot(*parsed, named.value) + "\n";
    } else {
      ASSERT_EQ(parsed.diagnostics.size(), 1u);
      const auto& diagnostic = parsed.diagnostics.front();
      EXPECT_EQ(diagnostic.message,
                "constant expression tree depth limit exceeded");
      snapshot += diagnostic.message + ":" +
                  std::to_string(diagnostic.range.start.column) + "\n";
    }
  }
  RecordProperty("initializer_snapshot", snapshot);
}

}  // namespace
}  // namespace ptx_frontend
