/*
 * ptx_lexer.l  —  Flex lexer for NVIDIA PTX ISA
 *
 * Mirrors the logos-based Token<'input> in ZLUDA's ptx_parser/src/lib.rs,
 * but produces tokens for a Bison/hand-written C++ parser.
 *
 * Build:  flex -o src/ptx_lexer.cpp src/ptx_lexer.l
 *
 * The scanner is re-entrant (%option reentrant) so it can be embedded
 * in a parser state without global variables.
 */

%{
#include "ptx_token.hpp"   /* token kind enum + YYSTYPE / PtxSVal */
#include <cstdlib>
#include <cstring>
#include <cerrno>

using ptx_frontend::TokenKind;

/* Forward-declared helper: intern a string slice into the parser arena.
   Implementation lives in ptx_parser.cpp.                              */
extern std::string_view ptx_intern(const char* s, std::size_t len, void* extra);

/* Convenience: current text as a string_view stored in yylval           */
#define STORE_SV()  \
    yylval->sv = ptx_intern(yytext, yyleng, yyextra)

/* Convenience: store yytext as sv and return the token kind             */
#define TOK(k)   do { STORE_SV(); return TokenKind::k; } while(0)

typedef ptx_frontend::PtxSVal YYSTYPE;

#define YY_DECL TokenKind yylex(YYSTYPE *yylval_param, yyscan_t yyscanner)
%}

/* ── scanner options ──────────────────────────────────────────────────── */
/* yylval is passed as parameter */ 
%option reentrant
%option bison-bridge          
%option noyywrap
%option nounput noinput
%option yylineno

/* ── named patterns (shared between multiple rules) ────────────────────── */

/* Whitespace and comments — same as ZLUDA's logos skip rule:
   (?:\s+)|(?://[^\n\r]*[\n\r]*)|(?:/\*[^*]*\*+(?:[^/* ][^*]*\*+)* /) */
WS          [ \t\r\n]+
LINE_CMT    "//"[^\n\r]*[\n\r]*
BLOCK_CMT   "/*"([^*]|\*+[^*/])*\*+"/"

/* Identifiers — ZLUDA:
   r"[a-zA-Z][a-zA-Z0-9_$]*|[_$%][a-zA-Z0-9_$]+"   (priority 0)
   The % prefix covers PTX special registers like %tid, %r0 etc.        */
IDENT       [a-zA-Z][a-zA-Z0-9_$]*|[_$%][a-zA-Z0-9_$]+

/* Dot-prefixed identifiers: modifier tokens and type suffixes            */
DOT_IDENT   \.[a-zA-Z][a-zA-Z0-9_$]*|\.[_$%][a-zA-Z0-9_$]+

/* Numeric literals — same as ZLUDA                                        */
HEX         0[xX][0-9a-fA-F]+U?
DECIMAL     [0-9]+U?
F32HEX      0[fF][0-9a-fA-F]{8}
F64HEX      0[dD][0-9a-fA-F]{16}
F64         [0-9]*\.[0-9]+([eE][-+]?[0-9]+)?
STRING      \"[^\"]*\"

%%

 /* ── skip whitespace and comments ─────────────────────────────────────── */
{WS}            { /* skip */ }
{LINE_CMT}      { /* skip */ }
{BLOCK_CMT}     { /* skip */ }

 /* ── punctuation ──────────────────────────────────────────────────────── */
","             { TOK(Comma); }
"."             { TOK(Dot); }
":"             { TOK(Colon); }
";"             { TOK(Semicolon); }
"@"             { TOK(At); }
"|"             { TOK(Pipe); }
"!"             { TOK(Exclamation); }
"("             { TOK(LParen); }
")"             { TOK(RParen); }
"["             { TOK(LBracket); }
"]"             { TOK(RBracket); }
"{"             { TOK(LBrace); }
"}"             { TOK(RBrace); }
"<"             { TOK(Lt); }
">"             { TOK(Gt); }
"-"             { TOK(Minus); }
"+"             { TOK(Plus); }
"="             { TOK(Eq); }

 /* ── numeric literals ─────────────────────────────────────────────────── */
{F32HEX}        { TOK(F32Hex);   }
{F64HEX}        { TOK(F64Hex);   }
{HEX}           { TOK(Hex);      }
{F64}           { TOK(F64);      }
{DECIMAL}       { TOK(Decimal);  }

 /* ── string literal ───────────────────────────────────────────────────── */
{STRING}        { TOK(String);   }

 /* ── built-in constant ────────────────────────────────────────────────── */
"WARP_SZ"       { TOK(WarpSz); }

 /* ─────────────────────────────────────────────────────────────────────── */
 /* Module-level directives (fixed tokens — no dynamic value needed)       */
 /* ─────────────────────────────────────────────────────────────────────── */
".version"      { TOK(DotVersion); }
".target"       { TOK(DotTarget); }
".address_size" { TOK(DotAddressSize); }
".loc"          { TOK(DotLoc); }
".pragma"       { TOK(DotPragma); }
".file"         { TOK(DotFile); }
".section"      { TOK(DotSection); }

 /* Linking / visibility */
".extern"       { TOK(DotExtern); }
".visible"      { TOK(DotVisible); }
".weak"         { TOK(DotWeak); }

 /* Function / entry */
".entry"        { TOK(DotEntry); }
".func"         { TOK(DotFunc); }

 /* Tuning */
".maxnreg"      { TOK(DotMaxnreg); }
".maxntid"      { TOK(DotMaxntid); }
".reqntid"      { TOK(DotReqntid); }
".minnctapersm" { TOK(DotMinnctapersm); }
".noreturn"     { TOK(DotNoreturn); }

 /* Variable modifiers */
".reg"          { TOK(DotReg); }
".align"        { TOK(DotAlign); }
".ptr"          { TOK(DotPtr); }
".global"       { TOK(DotGlobal); }
".const"        { TOK(DotConst); }
".shared"       { TOK(DotShared); }
".local"        { TOK(DotLocal); }
".param"        { TOK(DotParam); }

 /* ── DWARF section bit-width directives ──────────────────────────────── */
".b8"           { TOK(DotB8); }
".b16"          { TOK(DotB16); }
".b32"          { TOK(DotB32); }
".b64"          { TOK(DotB64); }
".b128"         { TOK(DotB128); }

 /* ── Scalar type tokens ───────────────────────────────────────────────── */
 /* Unsigned */
".u8"           { TOK(DotU8); }
".u16"          { TOK(DotU16); }
".u16x2"        { TOK(DotU16x2); }
".u32"          { TOK(DotU32); }
".u64"          { TOK(DotU64); }
 /* Signed */
".s8"           { TOK(DotS8); }
".s16"          { TOK(DotS16); }
".s16x2"        { TOK(DotS16x2); }
".s32"          { TOK(DotS32); }
".s64"          { TOK(DotS64); }
 /* Float */
".f16"          { TOK(DotF16); }
".f16x2"        { TOK(DotF16x2); }
".f32"          { TOK(DotF32); }
".f64"          { TOK(DotF64); }
".bf16"         { TOK(DotBF16); }
".bf16x2"       { TOK(DotBF16x2); }
 /* Float8 packed */
".e4m3x2"       { TOK(DotE4m3x2); }
".e5m2x2"       { TOK(DotE5m2x2); }
 /* Predicate */
".pred"         { TOK(DotPred); }

 /* ── Vector prefix ────────────────────────────────────────────────────── */
".v2"           { TOK(DotV2); }
".v4"           { TOK(DotV4); }
".v8"           { TOK(DotV8); }

 /* ── Instruction opcodes ─────────────────────────────────────────────── */
 /* Data movement */
"mov"           { TOK(Mov); }
"ld"            { TOK(Ld); }
"st"            { TOK(St); }
"cvt"           { TOK(Cvt); }
"cvta"          { TOK(Cvta); }
"cvt.pack"      { TOK(CvtPack); }
"prefetch"      { TOK(Prefetch); }
"cp"            { TOK(Cp); }
"createpolicy"  { TOK(Createpolicy); }

 /* Arithmetic — integer */
"add"           { TOK(Add); }
"sub"           { TOK(Sub); }
"mul"           { TOK(Mul); }
"mul24"         { TOK(Mul24); }
"mad"           { TOK(Mad); }
"mad24"         { TOK(Mad24); }
"div"           { TOK(Div); }
"rem"           { TOK(Rem); }
"abs"           { TOK(Abs); }
"neg"           { TOK(Neg); }
"min"           { TOK(Min); }
"max"           { TOK(Max); }
"sad"           { TOK(Sad); }
"add.cc"        { TOK(AddCc); }
"addc"          { TOK(Addc); }
"sub.cc"        { TOK(SubCc); }
"subc"          { TOK(Subc); }
"madc"          { TOK(Madc); }
"dp4a"          { TOK(Dp4a); }
"dp2a"          { TOK(Dp2a); }

 /* Arithmetic — float */
"fma"           { TOK(Fma); }
"rcp"           { TOK(Rcp); }
"sqrt"          { TOK(Sqrt); }
"rsqrt"         { TOK(Rsqrt); }
"sin"           { TOK(Sin); }
"cos"           { TOK(Cos); }
"lg2"           { TOK(Lg2); }
"ex2"           { TOK(Ex2); }
"tanh"          { TOK(Tanh); }
"copysign"      { TOK(Copysign); }

 /* Logic / shift */
"and"           { TOK(And); }
"or"            { TOK(Or); }
"xor"           { TOK(Xor); }
"not"           { TOK(Not); }
"shl"           { TOK(Shl); }
"shr"           { TOK(Shr); }
"shf"           { TOK(Shf); }
"bfe"           { TOK(Bfe); }
"bfi"           { TOK(Bfi); }
"brev"          { TOK(Brev); }
"bmsk"          { TOK(Bmsk); }
"clz"           { TOK(Clz); }
"popc"          { TOK(Popc); }
"prmt"          { TOK(Prmt); }

 /* Comparison / select */
"setp"          { TOK(Setp); }
"set"           { TOK(Set); }
"selp"          { TOK(Selp); }

 /* Control flow */
"bra"           { TOK(Bra); }
"call"          { TOK(Call); }
"ret"           { TOK(Ret); }
"trap"          { TOK(Trap); }

 /* Synchronization / barriers */
"bar"           { TOK(Bar); }
"barrier"       { TOK(Barrier); }
"membar"        { TOK(Membar); }
"atom"          { TOK(Atom); }
"nanosleep"     { TOK(Nanosleep); }
"activemask"    { TOK(Activemask); }
"vote"          { TOK(Vote); }
"redux"         { TOK(Redux); }
"shfl"          { TOK(Shfl); }

 /* Warp/matrix */
"ldmatrix"       { TOK(Ldmatrix); }
"mma"            { TOK(Mma); }
"griddepcontrol" { TOK(Griddepcontrol); }

 /* ── Instruction modifier keywords (dot-tokens) ────────────────────────
    These must appear BEFORE the catch-all {DOT_IDENT} rule so that Flex
    picks the longer / more specific match first.                          */

 /* Rounding modes */
".rn"           { TOK(DotRn); }
".rz"           { TOK(DotRz); }
".rm"           { TOK(DotRm); }
".rp"           { TOK(DotRp); }
".rni"          { TOK(DotRni); }
".rzi"          { TOK(DotRzi); }
".rmi"          { TOK(DotRmi); }
".rpi"          { TOK(DotRpi); }
".rna"          { TOK(DotRna); }

 /* FP flags */
".ftz"          { TOK(DotFtz); }
".sat"          { TOK(DotSat); }
".satfinite"    { TOK(DotSatfinite); }
".relu"         { TOK(DotRelu); }
".approx"       { TOK(DotApprox); }
".full"         { TOK(DotFull); }
".NaN"          { TOK(DotNaN); }

 /* ld/st qualifiers */
".weak"         { TOK(DotWeak2); }   /* disambiguated from .weak linking */
".volatile"     { TOK(DotVolatile); }
".relaxed"      { TOK(DotRelaxed); }
".acquire"      { TOK(DotAcquire); }
".release"      { TOK(DotRelease); }
".acq_rel"      { TOK(DotAcqRel); }

 /* Memory scopes */
".cta"          { TOK(DotCta); }
".cluster"      { TOK(DotCluster); }
".gpu"          { TOK(DotGpu); }
".sys"          { TOK(DotSys); }

 /* State spaces (as modifiers, e.g. ld.global) */
".generic"      { TOK(DotGeneric); }

 /* ld/st cache operators */
".ca"           { TOK(DotCa); }
".cg"           { TOK(DotCg); }
".cs"           { TOK(DotCs); }
".lu"           { TOK(DotLu); }
".cv"           { TOK(DotCv); }
".wb"           { TOK(DotWb); }
".wt"           { TOK(DotWt); }

 /* Cache levels / prefetch */
".L1"           { TOK(DotL1); }
".L2"           { TOK(DotL2); }

 /* Atomic ops */
".and"          { TOK(DotAnd); }
".or"           { TOK(DotOr); }
".xor"          { TOK(DotXor); }
".exch"         { TOK(DotExch); }
".inc"          { TOK(DotInc); }
".dec"          { TOK(DotDec); }
".add"          { TOK(DotAdd); }
".min"          { TOK(DotMin); }
".max"          { TOK(DotMax); }
".cas"          { TOK(DotCas); }

 /* Mul / mad modes */
".hi"           { TOK(DotHi); }
".lo"           { TOK(DotLo); }
".wide"         { TOK(DotWide); }
".cc"           { TOK(DotCc); }

 /* Comparison operators (used in setp / set) */
".eq"           { TOK(DotEq); }
".ne"           { TOK(DotNe); }
".lt"           { TOK(DotLt2); }
".le"           { TOK(DotLe); }
".gt"           { TOK(DotGt2); }
".ge"           { TOK(DotGe); }
".lo"           { TOK(DotLo); }   /* unsigned less */
".ls"           { TOK(DotLs); }
".hs"           { TOK(DotHs); }
".equ"          { TOK(DotEqu); }
".neu"          { TOK(DotNeu); }
".ltu"          { TOK(DotLtu); }
".leu"          { TOK(DotLeu); }
".gtu"          { TOK(DotGtu); }
".geu"          { TOK(DotGeu); }
".num"          { TOK(DotNum); }
".nan"          { TOK(DotNan); }

 /* Boolean post-ops (setp.BoolOp) */
".xor"          { TOK(DotXor); }   /* already defined above */

 /* Vote / shuffle / bar */
".uni"          { TOK(DotUni); }
".sync"         { TOK(DotSync); }
".aligned"      { TOK(DotAligned); }
".all"          { TOK(DotAll); }
".any"          { TOK(DotAny); }
".ballot"       { TOK(DotBallot); }
".up"           { TOK(DotUp); }
".down"         { TOK(DotDown); }
".bfly"         { TOK(DotBfly); }
".idx"          { TOK(DotIdx); }
".warp"         { TOK(DotWarp); }
".red"          { TOK(DotRed); }

 /* ldmatrix */
".m8n8"         { TOK(DotM8n8); }
".m16n16"       { TOK(DotM16n16); }
".x1"           { TOK(DotX1); }
".x2"           { TOK(DotX2); }
".x4"           { TOK(DotX4); }
".trans"        { TOK(DotTrans); }

 /* mma layout */
".row"          { TOK(DotRow); }
".col"          { TOK(DotCol); }
".m16n8k16"     { TOK(DotM16n8k16); }
".m16n8k32"     { TOK(DotM16n8k32); }

 /* shf / bmsk */
".clamp"        { TOK(DotClamp); }
".wrap"         { TOK(DotWrap); }
".l"            { TOK(DotL); }
".r"            { TOK(DotR); }

 /* griddepcontrol */
".launch_dependents" { TOK(DotLaunchDependents); }
".wait"              { TOK(DotWait); }

 /* cache_hint / prefetch_size (compound dot-tokens) — treat as DotIdent  */
".L1::evict_normal"    { TOK(DotIdent); }
".L1::evict_unchanged" { TOK(DotIdent); }
".L1::evict_first"     { TOK(DotIdent); }
".L1::evict_last"      { TOK(DotIdent); }
".L1::no_allocate"     { TOK(DotIdent); }
".L2::cache_hint"      { TOK(DotIdent); }
".L2::evict_last"      { TOK(DotIdent); }
".L2::evict_normal"    { TOK(DotIdent); }
".L2::evict_first"     { TOK(DotIdent); }
".L2::evict_unchanged" { TOK(DotIdent); }
".L2::64B"             { TOK(DotIdent); }
".L2::128B"            { TOK(DotIdent); }
".L2::256B"            { TOK(DotIdent); }

 /* State-space sub-qualifiers (e.g. .shared::cta, .param::entry) */
".shared::cta"     { TOK(DotIdent); }
".shared::cluster" { TOK(DotIdent); }
".param::entry"    { TOK(DotIdent); }
".param::func"     { TOK(DotIdent); }
".const"           { TOK(DotConst2); }  /* already reserved as DotConst */

 /* nc (non-coherent ld) */
".nc"           { TOK(DotNc); }

 /* noftz (atom fp path) */
".noftz"        { TOK(DotNoftz); }

 /* fractional (createpolicy) */
".fractional"   { TOK(DotFractional); }

 /* ── catch-all: any other dot-prefixed word → DotIdent ──────────────── */
{DOT_IDENT}     { TOK(DotIdent); }

 /* ── identifiers (opcodes / register names that weren't matched above) ─ */
{IDENT}         { TOK(Ident); }

 /* ── EOF ─────────────────────────────────────────────────────────────── */
<<EOF>>         { yylval->sv = {}; return TokenKind::Eof; }

 /* ── error fallback ──────────────────────────────────────────────────── */
.               {
    /* unknown character — report and skip */
    return TokenKind::Error;
}

%%

/* nothing needed in the user-code section for the pure scanner */