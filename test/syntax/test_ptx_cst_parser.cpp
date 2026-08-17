#include <gtest/gtest.h>

#include <string_view>
#include <variant>

#include "ptx_ir/cst/ptx_cst_parser.hpp"

namespace ptx_frontend {
namespace {

using syntax_cst::CstAddress;
using syntax_cst::CstBranchTarget;
using syntax_cst::CstCallParameterList;
using syntax_cst::CstCallTarget;
using syntax_cst::CstCallTargetSet;
using syntax_cst::CstVectorMember;
using syntax_cst::CstVectorPack;

TEST(PtxCstParser, RoundTripsInstructionWithAllTriviaAndPunctuation) {
  constexpr std::string_view source =
      "  // lead\n@!%p add /* type */ .u32 "
      "[%rd1 /* op */ + 16], {%r1, -2} /* tail */ ;\n// eof";
  PtxCstParser parser(source);

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;

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

TEST(PtxCstParser, RetainsStructuredOperandDelimiterTokens) {
  PtxCstParser parser("mov.b32 [%rd1-4], %r2.x, {%r3, 1};");

  auto result = parser.parseInstruction();
  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto* instruction = result->instruction();
  ASSERT_NE(instruction, nullptr);
  ASSERT_EQ(instruction->operands.size(), 1u);
  const auto& target =
      std::get<CstBranchTarget>(instruction->operands[0].operand);
  EXPECT_EQ(result->token(target.name.token).text, "done");
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
            "direct branch accepts exactly one label target"}}) {
    PtxCstParser parser(source);
    const auto result = parser.parseInstruction();
    ASSERT_FALSE(result.has_value()) << source;
    EXPECT_EQ(result.error().message, message) << source;
  }
}

TEST(PtxCstParser, RejectsTrailingSignificantInput) {
  PtxCstParser parser("add.u32 %r1, %r2, %r3; sub.u32 %r4, %r5, %r6;");

  auto result = parser.parseInstruction();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().message, "expected end of input");
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

TEST(PtxCstParser, FindsFuncNameAfterReturnParameterList) {
  constexpr std::string_view source =
      ".func (.param .b32 result) helper(.param .b32 input) { ret; }";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

TEST(PtxCstParser, ParsesRegisterDeclarationsAndLabels) {
  constexpr std::string_view source =
      ".entry kernel() { .reg .align 16 .u32 %r<3>, %tmp; loop: ret; }";
  PtxCstParser parser(source);

  const auto result = parser.parseModule();

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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

  ASSERT_TRUE(result.has_value()) << result.error().message;
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
    ASSERT_FALSE(result.has_value()) << source_and_message.first;
    EXPECT_EQ(result.error().message, source_and_message.second);
  }
}

}  // namespace
}  // namespace ptx_frontend
