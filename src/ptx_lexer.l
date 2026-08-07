/*
 * ptx_lexer.l -- Flex lexer for the PTX frontend.
 *
 * This lexer intentionally avoids enumerating every PTX opcode, type suffix,
 * instruction modifier, shape suffix, or cache operator.
 */

%{
#include "ptx_ir/lex/ptx_token.hpp"
using ptx_frontend::SourcePos;
using ptx_frontend::SourceRange;
using ptx_frontend::TokenKind;

#define STORE_SV() \
    yylval->sv = std::string_view(yytext, static_cast<std::size_t>(yyleng))

#define SET_EMPTY_SV() \
    yylval->sv = {}

#define TOK(k) \
    do {       \
        STORE_SV(); \
        return TokenKind::k; \
    } while (0)

typedef ptx_frontend::PtxSVal YYSTYPE;

#define YY_DECL TokenKind yylex(YYSTYPE* yylval_param, yyscan_t yyscanner)

static ptx_frontend::SourceRange ptx_make_current_range(
    ptx_frontend::PtxLexerExtra* extra) {
  return ptx_frontend::SourceRange{extra->pos, extra->pos};
}

static void ptx_advance_position(
    ptx_frontend::PtxLexerExtra* extra,
    const char* text,
    int len) {
  SourcePos pos = extra->pos;

  for (int i = 0; i < len; ++i) {
    const char ch = text[i];

    if (ch == '\n') {
      ++pos.line;
      pos.column = 1;
    } else if (ch == '\r') {
      ++pos.line;
      pos.column = 1;

      if (i + 1 < len && text[i + 1] == '\n') {
        ++i;
      }
    } else {
      ++pos.column;
    }
  }

  extra->pos = pos;
}

#define YY_USER_ACTION                                                        \
    do {                                                                      \
        SourcePos start = yyextra->pos;                                       \
        ptx_advance_position(yyextra, yytext, yyget_leng(yyscanner));         \
        SourcePos end = yyextra->pos;                                         \
        yylval->range = SourceRange{start, end};                              \
    } while (0);
%}

%option reentrant
%option bison-bridge
%option noyywrap
%option nounput noinput
%option extra-type="ptx_frontend::PtxLexerExtra*"

%x BLOCK_COMMENT

WS              [ \t\r\n]+
LINE_CMT        "//"[^\n\r]*

ID_START        [A-Za-z_$%]
ID_CONT         [A-Za-z0-9_$%]
IDENT           {ID_START}{ID_CONT}*

DOT_ATOM        [A-Za-z0-9_$%]+
DOT_IDENT       \.{DOT_ATOM}(::{DOT_ATOM})*

F32HEX          0[fF][0-9a-fA-F]{8}
F64HEX          0[dD][0-9a-fA-F]{16}
HEX             0[xX][0-9a-fA-F]+[uU]?
DECIMAL         [0-9]+[uU]?

F64_A           [0-9]*\.[0-9]+([eE][-+]?[0-9]+)?
F64_B           [0-9]+[eE][-+]?[0-9]+
F64             ({F64_A}|{F64_B})

STRING          \"([^\\\"\n\r]|\\.)*\"

%%

{WS}                    { TOK(Whitespace); }
{LINE_CMT}              { TOK(LineComment); }

"/*"                    {
                            yyextra->block_comment_text.assign(yytext, yyleng);
                            yyextra->block_comment_start = yylval->range.start;
                            BEGIN(BLOCK_COMMENT);
                        }

<BLOCK_COMMENT>"*/"     {
                            yyextra->block_comment_text.append(yytext, yyleng);
                            yylval->sv = yyextra->block_comment_text;
                            yylval->range = SourceRange{
                                yyextra->block_comment_start,
                                yylval->range.end,
                            };
                            BEGIN(INITIAL);
                            return TokenKind::BlockComment;
                        }
<BLOCK_COMMENT>.|\n     { yyextra->block_comment_text.append(yytext, yyleng); }
<BLOCK_COMMENT><<EOF>>  {
                            yylval->sv = yyextra->block_comment_text;
                            yylval->range = SourceRange{
                                yyextra->block_comment_start,
                                ptx_make_current_range(yyextra).end,
                            };
                            return TokenKind::Error;
                        }

","                     { TOK(Comma); }
"."                     { TOK(Dot); }
":"                     { TOK(Colon); }
";"                     { TOK(Semicolon); }
"@"                     { TOK(At); }
"|"                     { TOK(Pipe); }
"!"                     { TOK(Exclamation); }

"("                     { TOK(LParen); }
")"                     { TOK(RParen); }
"["                     { TOK(LBracket); }
"]"                     { TOK(RBracket); }
"{"                     { TOK(LBrace); }
"}"                     { TOK(RBrace); }
"<"                     { TOK(Lt); }
">"                     { TOK(Gt); }

"-"                     { TOK(Minus); }
"+"                     { TOK(Plus); }
"="                     { TOK(Eq); }

{F32HEX}                { TOK(F32Hex); }
{F64HEX}                { TOK(F64Hex); }
{HEX}                   { TOK(Hex); }
{F64}                   { TOK(F64); }
{DECIMAL}               { TOK(Decimal); }

{STRING}                { TOK(String); }

".version"              { TOK(DotVersion); }
".target"               { TOK(DotTarget); }
".address_size"         { TOK(DotAddressSize); }

".loc"                  { TOK(DotLoc); }
".pragma"               { TOK(DotPragma); }
".file"                 { TOK(DotFile); }
".section"              { TOK(DotSection); }

".extern"               { TOK(DotExtern); }
".visible"              { TOK(DotVisible); }
".weak"                 { TOK(DotWeak); }

".entry"                { TOK(DotEntry); }
".func"                 { TOK(DotFunc); }

".maxnreg"              { TOK(DotMaxnreg); }
".maxntid"              { TOK(DotMaxntid); }
".reqntid"              { TOK(DotReqntid); }
".minnctapersm"         { TOK(DotMinnctapersm); }
".noreturn"             { TOK(DotNoreturn); }

".reg"                  { TOK(DotReg); }
".align"                { TOK(DotAlign); }
".ptr"                  { TOK(DotPtr); }
".global"               { TOK(DotGlobal); }
".const"                { TOK(DotConst); }
".shared"               { TOK(DotShared); }
".local"                { TOK(DotLocal); }
".param"                { TOK(DotParam); }

"WARP_SZ"               { TOK(WarpSz); }

 /* Known compound dot identifiers that contain ordinary '.' separators. */
".async.global"                   { TOK(DotIdent); }
".async.shared::cta"              { TOK(DotIdent); }
".async.shared::cluster"          { TOK(DotIdent); }
".sync_restrict::shared::cta"     { TOK(DotIdent); }
".sync_restrict::shared::cluster" { TOK(DotIdent); }

".b8x16.b6x16_p32"                { TOK(DotIdent); }
".b8x16.b4x16_p64"                { TOK(DotIdent); }

 /* Generic dot-prefixed token. Does not consume chained suffixes like .sat.s32. */
{DOT_IDENT}                       { TOK(DotIdent); }
{IDENT}                 { TOK(Ident); }

<<EOF>>                 {
                            SET_EMPTY_SV();
                            yylval->range = ptx_make_current_range(yyextra);
                            return TokenKind::Eof;
                        }

.                       {
                            STORE_SV();
                            return TokenKind::Error;
                        }

%%

/* No user-code section needed. */
