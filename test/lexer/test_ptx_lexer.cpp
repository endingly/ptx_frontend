#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ptx_lexer.hpp"

using namespace ptx_frontend;

struct LexedToken {
  TokenKind kind;
  std::string text;
  SourceRange range;
};

static std::vector<LexedToken> lex_all(std::string_view src) {
  PtxLexer lexer(src);
  std::vector<LexedToken> out;

  while (true) {
    auto tok = lexer.next();

    if (tok.kind == TokenKind::Eof) {
      break;
    }

    out.push_back(LexedToken{
        .kind = tok.kind,
        .text = std::string(tok.text),
        .range = tok.range,
    });

    if (tok.kind == TokenKind::Error) {
      break;
    }
  }

  return out;
}

static std::vector<LexedToken> lex_all_with_eof(std::string_view src) {
  PtxLexer lexer(src);
  std::vector<LexedToken> out;

  while (true) {
    auto tok = lexer.next();

    out.push_back(LexedToken{
        .kind = tok.kind,
        .text = std::string(tok.text),
        .range = tok.range,
    });

    if (tok.kind == TokenKind::Eof || tok.kind == TokenKind::Error) {
      break;
    }
  }

  return out;
}

static void expect_token(const LexedToken& tok, TokenKind kind,
                         std::string_view text) {
  EXPECT_EQ(tok.kind, kind) << "text=" << tok.text;
  EXPECT_EQ(tok.text, text);
}

// -----------------------------------------------------------------------------
// Basic instruction lexing
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, EmitsEof) {
  PtxLexer lexer("add.s32;");

  EXPECT_EQ(lexer.next().kind, TokenKind::Ident);
  EXPECT_EQ(lexer.next().kind, TokenKind::DotIdent);
  EXPECT_EQ(lexer.next().kind, TokenKind::Semicolon);

  auto eof = lexer.next();
  EXPECT_EQ(eof.kind, TokenKind::Eof);
  EXPECT_EQ(eof.text, "");
}

TEST(PtxLexerNew, InstructionOpcodeIsGenericIdent) {
  auto toks = lex_all("add.sat.s32 %r1, %r2, %r3;");

  ASSERT_EQ(toks.size(), 9u);

  expect_token(toks[0], TokenKind::Ident, "add");
  expect_token(toks[1], TokenKind::DotIdent, ".sat");
  expect_token(toks[2], TokenKind::DotIdent, ".s32");
  expect_token(toks[3], TokenKind::Ident, "%r1");
  expect_token(toks[4], TokenKind::Comma, ",");
  expect_token(toks[5], TokenKind::Ident, "%r2");
  expect_token(toks[6], TokenKind::Comma, ",");
  expect_token(toks[7], TokenKind::Ident, "%r3");
  expect_token(toks[8], TokenKind::Semicolon, ";");
}

TEST(PtxLexerNew, MultipleOpcodesAreGenericIdent) {
  auto toks = lex_all("mov ld st add sub mul mma wgmma tcgen05");

  ASSERT_EQ(toks.size(), 9u);

  expect_token(toks[0], TokenKind::Ident, "mov");
  expect_token(toks[1], TokenKind::Ident, "ld");
  expect_token(toks[2], TokenKind::Ident, "st");
  expect_token(toks[3], TokenKind::Ident, "add");
  expect_token(toks[4], TokenKind::Ident, "sub");
  expect_token(toks[5], TokenKind::Ident, "mul");
  expect_token(toks[6], TokenKind::Ident, "mma");
  expect_token(toks[7], TokenKind::Ident, "wgmma");
  expect_token(toks[8], TokenKind::Ident, "tcgen05");
}

TEST(PtxLexerNew, TypeSuffixesAreDotIdent) {
  auto toks = lex_all(
      ".u8 .u8x4 .u16 .u16x2 .u32 .u64 "
      ".s8 .s8x4 .s16 .s16x2 .s32 .s64 "
      ".f16 .f16x2 .f32 .f32x2 .f64 .bf16 .bf16x2 .pred");

  ASSERT_EQ(toks.size(), 20u);

  for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
    EXPECT_EQ(toks[i].kind, TokenKind::DotIdent) << "text=" << toks[i].text;
  }

  expect_token(toks[0], TokenKind::DotIdent, ".u8");
  expect_token(toks[1], TokenKind::DotIdent, ".u8x4");
  expect_token(toks[4], TokenKind::DotIdent, ".u32");
  expect_token(toks[10], TokenKind::DotIdent, ".s32");
  expect_token(toks[14], TokenKind::DotIdent, ".f32");
  expect_token(toks[19], TokenKind::DotIdent, ".pred");
}

TEST(PtxLexerNew, InstructionModifiersAreDotIdent) {
  auto toks = lex_all(
      ".sat .rn .rz .rm .rp .ftz .approx .relu "
      ".sync .aligned .row .col .m16n8k16 .x4 .trans");

  ASSERT_EQ(toks.size(), 15u);

  for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
    EXPECT_EQ(toks[i].kind, TokenKind::DotIdent) << "text=" << toks[i].text;
  }

  expect_token(toks[0], TokenKind::DotIdent, ".sat");
  expect_token(toks[1], TokenKind::DotIdent, ".rn");
  expect_token(toks[8], TokenKind::DotIdent, ".sync");
  expect_token(toks[12], TokenKind::DotIdent, ".m16n8k16");
}

// -----------------------------------------------------------------------------
// Compound opcode / compound modifier behavior
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, CompoundOpcodesAreIdentPlusDotIdent) {
  {
    auto toks = lex_all("cvt.pack.u16.sat.s8");

    ASSERT_EQ(toks.size(), 5u);
    expect_token(toks[0], TokenKind::Ident, "cvt");
    expect_token(toks[1], TokenKind::DotIdent, ".pack");
    expect_token(toks[2], TokenKind::DotIdent, ".u16");
    expect_token(toks[3], TokenKind::DotIdent, ".sat");
    expect_token(toks[4], TokenKind::DotIdent, ".s8");
  }

  {
    auto toks = lex_all("add.cc.u32");

    ASSERT_EQ(toks.size(), 3u);
    expect_token(toks[0], TokenKind::Ident, "add");
    expect_token(toks[1], TokenKind::DotIdent, ".cc");
    expect_token(toks[2], TokenKind::DotIdent, ".u32");
  }
}

TEST(PtxLexerNew, ComplexDotIdentifiersRemainSingleToken) {
  auto toks = lex_all(
      ".kind::mxf8f6f4 "
      ".async.shared::cta "
      ".shared::cluster "
      ".scale::2,1 "
      ".b8x16.b6x16_p32 "
      ".collector::b0::smem "
      ".mbarrier::complete_tx::bytes");

  ASSERT_EQ(toks.size(), 9u);

  expect_token(toks[0], TokenKind::DotIdent, ".kind::mxf8f6f4");
  expect_token(toks[1], TokenKind::DotIdent, ".async.shared::cta");
  expect_token(toks[2], TokenKind::DotIdent, ".shared::cluster");
  expect_token(toks[3], TokenKind::DotIdent, ".scale::2");
  expect_token(toks[4], TokenKind::Comma, ",");
  expect_token(toks[5], TokenKind::Decimal, "1");
  expect_token(toks[6], TokenKind::DotIdent, ".b8x16.b6x16_p32");
  expect_token(toks[7], TokenKind::DotIdent, ".collector::b0::smem");
  expect_token(toks[8], TokenKind::DotIdent, ".mbarrier::complete_tx::bytes");
}

TEST(PtxLexerNew, PreservesLeadingTrivia) {
  PtxLexer lexer("  // comment\n/* block */ add");

  auto token = lexer.next();
  ASSERT_EQ(token.kind, TokenKind::Ident);
  EXPECT_EQ(token.text, "add");
  ASSERT_EQ(token.leading_trivia.size(), 5u);
  EXPECT_EQ(token.leading_trivia[0].kind, TriviaKind::Whitespace);
  EXPECT_EQ(token.leading_trivia[0].text, "  ");
  EXPECT_EQ(token.leading_trivia[1].kind, TriviaKind::LineComment);
  EXPECT_EQ(token.leading_trivia[1].text, "// comment");
  EXPECT_EQ(token.leading_trivia[2].kind, TriviaKind::Whitespace);
  EXPECT_EQ(token.leading_trivia[2].text, "\n");
  EXPECT_EQ(token.leading_trivia[3].kind, TriviaKind::BlockComment);
  EXPECT_EQ(token.leading_trivia[3].text, "/* block */");
  EXPECT_EQ(token.leading_trivia[4].kind, TriviaKind::Whitespace);
  EXPECT_EQ(token.leading_trivia[4].text, " ");
}

TEST(PtxLexerNew, PreservesTrailingTriviaOnEof) {
  PtxLexer lexer("add\n// trailing");

  EXPECT_EQ(lexer.next().kind, TokenKind::Ident);
  auto eof = lexer.next();
  ASSERT_EQ(eof.kind, TokenKind::Eof);
  ASSERT_EQ(eof.leading_trivia.size(), 2u);
  EXPECT_EQ(eof.leading_trivia[0].kind, TriviaKind::Whitespace);
  EXPECT_EQ(eof.leading_trivia[0].text, "\n");
  EXPECT_EQ(eof.leading_trivia[1].kind, TriviaKind::LineComment);
  EXPECT_EQ(eof.leading_trivia[1].text, "// trailing");
}

TEST(PtxLexerNew, NumericLeadingDotShapesAreDotIdent) {
  auto toks = lex_all(
      ".16x64b .16x128b .16x256b .32x32b .32x64b .32x128b .16x32bx2 "
      ".128x .4x256b");

  ASSERT_EQ(toks.size(), 9u);

  for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
    EXPECT_EQ(toks[i].kind, TokenKind::DotIdent) << "text=" << toks[i].text;
  }

  expect_token(toks[0], TokenKind::DotIdent, ".16x64b");
  expect_token(toks[6], TokenKind::DotIdent, ".16x32bx2");
  expect_token(toks[8], TokenKind::DotIdent, ".4x256b");
}

// -----------------------------------------------------------------------------
// Module / declaration directives
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, ModuleDirectivesRemainDedicatedTokens) {
  auto toks = lex_all(".version 8.0\n.target sm_80\n.address_size 64");

  ASSERT_EQ(toks.size(), 6u);

  expect_token(toks[0], TokenKind::DotVersion, ".version");
  expect_token(toks[1], TokenKind::F64, "8.0");
  expect_token(toks[2], TokenKind::DotTarget, ".target");
  expect_token(toks[3], TokenKind::Ident, "sm_80");
  expect_token(toks[4], TokenKind::DotAddressSize, ".address_size");
  expect_token(toks[5], TokenKind::Decimal, "64");
}

TEST(PtxLexerNew, FunctionAndVisibilityDirectivesRemainDedicatedTokens) {
  auto toks = lex_all(".visible .entry _Z6kernelv .func .extern .weak");

  ASSERT_EQ(toks.size(), 6u);

  expect_token(toks[0], TokenKind::DotVisible, ".visible");
  expect_token(toks[1], TokenKind::DotEntry, ".entry");
  expect_token(toks[2], TokenKind::Ident, "_Z6kernelv");
  expect_token(toks[3], TokenKind::DotFunc, ".func");
  expect_token(toks[4], TokenKind::DotExtern, ".extern");
  expect_token(toks[5], TokenKind::DotWeak, ".weak");
}

TEST(PtxLexerNew, DeclarationDirectivesRemainDedicatedTokens) {
  auto toks = lex_all(".reg .align .ptr .global .const .shared .local .param");

  ASSERT_EQ(toks.size(), 8u);

  expect_token(toks[0], TokenKind::DotReg, ".reg");
  expect_token(toks[1], TokenKind::DotAlign, ".align");
  expect_token(toks[2], TokenKind::DotPtr, ".ptr");
  expect_token(toks[3], TokenKind::DotGlobal, ".global");
  expect_token(toks[4], TokenKind::DotConst, ".const");
  expect_token(toks[5], TokenKind::DotShared, ".shared");
  expect_token(toks[6], TokenKind::DotLocal, ".local");
  expect_token(toks[7], TokenKind::DotParam, ".param");
}

// -----------------------------------------------------------------------------
// Declarations and instruction snippets
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, RegisterDeclarationSnippet) {
  auto toks = lex_all(".reg .b32 %r<4>;");

  ASSERT_EQ(toks.size(), 7u);

  expect_token(toks[0], TokenKind::DotReg, ".reg");
  expect_token(toks[1], TokenKind::DotIdent, ".b32");
  expect_token(toks[2], TokenKind::Ident, "%r");
  expect_token(toks[3], TokenKind::Lt, "<");
  expect_token(toks[4], TokenKind::Decimal, "4");
  expect_token(toks[5], TokenKind::Gt, ">");
  expect_token(toks[6], TokenKind::Semicolon, ";");
}

TEST(PtxLexerNew, LoadInstructionWithDeclarationLikeModifier) {
  auto toks = lex_all("ld.global.u32 %r1, [%rd1];");

  ASSERT_EQ(toks.size(), 9u);

  expect_token(toks[0], TokenKind::Ident, "ld");

  // In the proposed transitional lexer, .global is still a dedicated
  // declaration token. The parser should normalize this by spelling when it is
  // parsing an instruction suffix list.
  expect_token(toks[1], TokenKind::DotGlobal, ".global");

  expect_token(toks[2], TokenKind::DotIdent, ".u32");
  expect_token(toks[3], TokenKind::Ident, "%r1");
  expect_token(toks[4], TokenKind::Comma, ",");
  expect_token(toks[5], TokenKind::LBracket, "[");
  expect_token(toks[6], TokenKind::Ident, "%rd1");
  expect_token(toks[7], TokenKind::RBracket, "]");
  expect_token(toks[8], TokenKind::Semicolon, ";");
}

TEST(PtxLexerNew, PredicateGuardAndInstruction) {
  auto toks = lex_all("@!%p1 add.s32 %r1, %r2, %r3;");

  ASSERT_EQ(toks.size(), 11u);

  expect_token(toks[0], TokenKind::At, "@");
  expect_token(toks[1], TokenKind::Exclamation, "!");
  expect_token(toks[2], TokenKind::Ident, "%p1");
  expect_token(toks[3], TokenKind::Ident, "add");
  expect_token(toks[4], TokenKind::DotIdent, ".s32");
  expect_token(toks[5], TokenKind::Ident, "%r1");
  expect_token(toks[6], TokenKind::Comma, ",");
  expect_token(toks[7], TokenKind::Ident, "%r2");
  expect_token(toks[8], TokenKind::Comma, ",");
  expect_token(toks[9], TokenKind::Ident, "%r3");
  expect_token(toks[10], TokenKind::Semicolon, ";");
}

// -----------------------------------------------------------------------------
// Literals
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, NumericLiterals) {
  auto toks =
      lex_all("0 123 123U 0x10 0x10U 0f3f800000 0d3ff0000000000000 1.25 1e-3");

  ASSERT_EQ(toks.size(), 9u);

  expect_token(toks[0], TokenKind::Decimal, "0");
  expect_token(toks[1], TokenKind::Decimal, "123");
  expect_token(toks[2], TokenKind::Decimal, "123U");
  expect_token(toks[3], TokenKind::Hex, "0x10");
  expect_token(toks[4], TokenKind::Hex, "0x10U");
  expect_token(toks[5], TokenKind::F32Hex, "0f3f800000");
  expect_token(toks[6], TokenKind::F64Hex, "0d3ff0000000000000");
  expect_token(toks[7], TokenKind::F64, "1.25");
  expect_token(toks[8], TokenKind::F64, "1e-3");
}

TEST(PtxLexerNew, StringLiteral) {
  auto toks = lex_all(R"(".file_name" "escaped\"quote")");

  ASSERT_EQ(toks.size(), 2u);

  expect_token(toks[0], TokenKind::String, R"(".file_name")");
  expect_token(toks[1], TokenKind::String, R"("escaped\"quote")");
}

// -----------------------------------------------------------------------------
// Comments and whitespace
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, SkipsLineAndBlockComments) {
  auto toks = lex_all(R"(
    // line comment
    add.s32 %r1, %r2, %r3; /* block comment */
    sub.s32 %r4, %r5, %r6;
  )");

  ASSERT_EQ(toks.size(), 16u);

  expect_token(toks[0], TokenKind::Ident, "add");
  expect_token(toks[1], TokenKind::DotIdent, ".s32");
  expect_token(toks[2], TokenKind::Ident, "%r1");
  expect_token(toks[3], TokenKind::Comma, ",");
  expect_token(toks[4], TokenKind::Ident, "%r2");
  expect_token(toks[5], TokenKind::Comma, ",");
  expect_token(toks[6], TokenKind::Ident, "%r3");
  expect_token(toks[7], TokenKind::Semicolon, ";");

  expect_token(toks[8], TokenKind::Ident, "sub");
  expect_token(toks[9], TokenKind::DotIdent, ".s32");
  expect_token(toks[10], TokenKind::Ident, "%r4");
  expect_token(toks[11], TokenKind::Comma, ",");
  expect_token(toks[12], TokenKind::Ident, "%r5");
  expect_token(toks[13], TokenKind::Comma, ",");
  expect_token(toks[14], TokenKind::Ident, "%r6");
  expect_token(toks[15], TokenKind::Semicolon, ";");
}

TEST(PtxLexerNew, UnterminatedBlockCommentReturnsError) {
  auto toks = lex_all("add.s32 %r1, %r2, %r3; /* unterminated");

  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks.back().kind, TokenKind::Error);
}

// -----------------------------------------------------------------------------
// Peek / consume behavior
// -----------------------------------------------------------------------------

TEST(PtxLexerNew, PeekDoesNotConsume) {
  PtxLexer lexer("add.s32;");

  auto p0 = lexer.peek();
  EXPECT_EQ(p0.kind, TokenKind::Ident);
  EXPECT_EQ(p0.text, "add");

  auto p1 = lexer.peek();
  EXPECT_EQ(p1.kind, TokenKind::Ident);
  EXPECT_EQ(p1.text, "add");

  auto c0 = lexer.consume();
  EXPECT_EQ(c0.kind, TokenKind::Ident);
  EXPECT_EQ(c0.text, "add");

  auto c1 = lexer.consume();
  EXPECT_EQ(c1.kind, TokenKind::DotIdent);
  EXPECT_EQ(c1.text, ".s32");

  auto c2 = lexer.consume();
  EXPECT_EQ(c2.kind, TokenKind::Semicolon);
  EXPECT_EQ(c2.text, ";");

  auto c3 = lexer.consume();
  EXPECT_EQ(c3.kind, TokenKind::Eof);
}
