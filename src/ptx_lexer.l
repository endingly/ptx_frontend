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
".u8x4"         { TOK(DotU8x4); } 
".u8"           { TOK(DotU8); }
".u16x2"        { TOK(DotU16x2); }
".u16"          { TOK(DotU16); }
".u32"          { TOK(DotU32); }
".u64"          { TOK(DotU64); }
 /* Signed */
".s8x4"         { TOK(DotS8x4); } 
".s8"           { TOK(DotS8); }
".s16x2"        { TOK(DotS16x2); }
".s16"          { TOK(DotS16); }
".s32"          { TOK(DotS32); }
".s64"          { TOK(DotS64); }
 /* Float */
".f16x2"        { TOK(DotF16x2); }
".f16"          { TOK(DotF16); }
".f32x2"        { TOK(DotF32x2); }
".f32"          { TOK(DotF32); }
".f64"          { TOK(DotF64); }
".bf16x2"       { TOK(DotBF16x2); }
".bf16"         { TOK(DotBF16); }
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
"stmatrix"       { TOK(Stmatrix); }
"mma"            { TOK(Mma); }
"wgmma"          { TOK(Wgmma); }
"tcgen05"        { TOK(Tcgen05); }
"griddepcontrol" { TOK(Griddepcontrol); }
"movmatrix"      { TOK(Movmatrix); }

 /* Synchronisation / fence / mbarrier */
"fence"                  { TOK(Fence); }
"red"                    { TOK(Red); }
"mbarrier"               { TOK(Mbarrier); }
"tensormap"              { TOK(Tensormap); }
"prefetchu"              { TOK(Prefetchu); }
"clusterlaunchcontrol"   { TOK(Clusterlaunchcontrol); }

 /* Misc PTX 9.2 */
"setmaxnreg"     { TOK(Setmaxnreg); }
"getctarank"     { TOK(Getctarank); }
"elect"          { TOK(Elect); }
"discard"        { TOK(Discard); }
"brkpt"          { TOK(Brkpt); }

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
".gl"           { TOK(DotGl); }
".sys"          { TOK(DotSys); }

 /* State spaces (as modifiers, e.g. ld.global) */
".generic"      { TOK(DotGeneric); }
".to"           { TOK(DotTo); }

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
".popc"         { TOK(DotPopc); }

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

 /* ── PTX 9.2 dot-modifiers ─────────────────────────────────────────── */

 /* wgmma / tcgen05 */
".mma_async"       { TOK(DotMmaAsync); }
".scale_d"         { TOK(DotScaleD); }

 /* mbarrier sub-ops */
".arrive_drop"     { TOK(DotArriveDrop); }
".arrive"          { TOK(DotArrive); }
".drop"            { TOK(DotDrop); }
".init"            { TOK(DotInit); }
".inval"           { TOK(DotInval); }
".expect_tx"       { TOK(DotExpectTx); }
".complete_tx"     { TOK(DotCompleteTx); }
".test_wait"       { TOK(DotTestWait); }
".try_wait"        { TOK(DotTryWait); }
".parity"          { TOK(DotParity); }
".no_complete"     { TOK(DotNoComplete); }

 /* fence */
".proxy"                       { TOK(DotProxy); }
".sc"                          { TOK(DotSc); }
".alias"                       { TOK(DotAlias); }
".op_restrict"                 { TOK(DotOpRestrict); }
 /* proxy kind multi-token — must appear before .async short rules */
".async.global"                { TOK(DotAsyncGlobal); }
".async.shared::cta"           { TOK(DotAsyncSharedCta); }
".async.shared::cluster"       { TOK(DotAsyncSharedCluster); }
".async::generic"              { TOK(DotAsyncGeneric); }
 /* sync_restrict multi-token */
".sync_restrict::shared::cta"     { TOK(DotSyncRestrictSharedCta); }
".sync_restrict::shared::cluster" { TOK(DotSyncRestrictSharedCluster); }

 /* tcgen05 sub-ops */
".alloc"           { TOK(DotAlloc); }
".dealloc"         { TOK(DotDealloc); }
".commit_arrival"  { TOK(DotCommitArrival); }
".relinquish"      { TOK(DotRelinquish); }
".pack"            { TOK(DotPack); }
".offset"          { TOK(DotOffset); }

 /* tcgen05 cta group */
".cta1"            { TOK(DotCta1); }
".cta2"            { TOK(DotCta2); }

 /* cluster launch control */
".try_cancel"      { TOK(DotTryCancel); }
".query_cancel"    { TOK(DotQueryCancel); }

 /* cp.async.bulk */
".bulk_group"      { TOK(DotBulkGroup); }
".bulk"            { TOK(DotBulk); }
".async"           { TOK(DotAsync); }
".commit_group"    { TOK(DotCommitGroup); }
".wait_group"      { TOK(DotWaitGroup); }

 /* tensormap sub-ops */
".replace"         { TOK(DotReplace); }
".cp_fenceproxy"   { TOK(DotCpFenceproxy); }

 /* tcgen05 shapes (数字开头，必须在 DOT_IDENT 之前显式列出) */
".16x64b"          { TOK(DotShape16x64b);   }
".16x128b"         { TOK(DotShape16x128b);  }
".16x256b"         { TOK(DotShape16x256b);  }
".32x32b"          { TOK(DotShape32x32b);   }
".32x64b"          { TOK(DotShape32x64b);   }
".32x128b"         { TOK(DotShape32x128b);  }
".16x32bx2"        { TOK(DotShape16x32bx2); }
".128x"            { TOK(DotShape128x);     }
".4x256b"          { TOK(DotShape4x256b);   }

 /* ── tcgen05 .kind::* 和其他含 :: 的复合 dot-token → DotIdent ─────── */
".kind::mxf8f6f4"     { TOK(DotIdent); }
".kind::mxf4nvf4"     { TOK(DotIdent); }
".kind::mxf4"         { TOK(DotIdent); }
".kind::f16bf16"      { TOK(DotIdent); }
".kind::tf32"         { TOK(DotIdent); }
".kind::i8"           { TOK(DotIdent); }
".kind::b1"           { TOK(DotIdent); }
".mbarrier::complete_tx::bytes"                         { TOK(DotIdent); }
".multicast::cluster::mbarrier::complete_tx::bytes"     { TOK(DotIdent); }
".b8x16.b6x16_p32"    { TOK(DotIdent); }
".b8x16.b4x16_p64"    { TOK(DotIdent); }
".b6x16_p32"          { TOK(DotIdent); }
".b4x16_p64"          { TOK(DotIdent); }
".collector::b0::smem" { TOK(DotIdent); }
".scale::2,1"         { TOK(DotIdent); }
".scale::1,2"         { TOK(DotIdent); }
".neg::a"             { TOK(DotIdent); }
".neg::b"             { TOK(DotIdent); }
".tensormap::generic" { TOK(DotIdent); }

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