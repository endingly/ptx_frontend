#pragma once
#include <cstdint>
#include <string_view>

namespace ptx_frontend {

/**
 * TokenKind — every terminal token the PTX lexer can produce.
 *
 * Mirrors ZLUDA's Token<'input> enum (ptx_parser/src/lib.rs),
 * extended with C++ needs (Eof, Error).
 */
enum class TokenKind : uint16_t {
  // ── End / error ──────────────────────────────────────────────
  Eof,
  Error,

  // ── Punctuation ───────────────────────────────────────────────
  Comma,        // ,
  Dot,          // .
  Colon,        // :
  Semicolon,    // ;
  At,           // @
  Pipe,         // |
  Exclamation,  // !
  LParen,       // (
  RParen,       // )
  LBracket,     // [
  RBracket,     // ]
  LBrace,       // {
  RBrace,       // }
  Lt,           // <
  Gt,           // >
  Minus,        // -
  Plus,         // +
  Eq,           // =

  // ── Literals ─────────────────────────────────────────────────
  F32Hex,   // 0f<8hex>
  F64Hex,   // 0d<16hex>
  Hex,      // 0x...  (integer hex, optional U suffix)
  F64,      // decimal float
  Decimal,  // decimal integer (optional U suffix)
  String,   // "..."
  WarpSz,   // WARP_SZ

  // ── Dynamic text tokens ──────────────────────────────────────
  Ident,     // bare identifier / opcode text
  DotIdent,  // .something (catch-all dot-token)

  // ── Module directives ────────────────────────────────────────
  DotVersion,
  DotTarget,
  DotAddressSize,
  DotLoc,
  DotPragma,
  DotFile,
  DotSection,

  // ── Linking ──────────────────────────────────────────────────
  DotExtern,
  DotVisible,
  DotWeak,  // linking .weak

  // ── Function / entry ─────────────────────────────────────────
  DotEntry,
  DotFunc,

  // ── Tuning ───────────────────────────────────────────────────
  DotMaxnreg,
  DotMaxntid,
  DotReqntid,
  DotMinnctapersm,
  DotNoreturn,

  // ── Variable / memory modifiers ──────────────────────────────
  DotReg,
  DotAlign,
  DotPtr,
  DotGlobal,
  DotConst,
  DotConst2,  // alias for .const when used as state-space modifier
  DotShared,
  DotLocal,
  DotParam,

  // ── Bit-width directives (DWARF / variable init) ─────────────
  DotB8,
  DotB16,
  DotB32,
  DotB64,
  DotB128,

  // ── Scalar types ─────────────────────────────────────────────
  DotU8,
  DotU16,
  DotU16x2,
  DotU32,
  DotU64,
  DotS8,
  DotS16,
  DotS16x2,
  DotS32,
  DotS64,
  DotF16,
  DotF16x2,
  DotF32,
  DotF64,
  DotBF16,
  DotBF16x2,
  DotE4m3x2,
  DotE5m2x2,
  DotPred,

  // ── Vector prefix ────────────────────────────────────────────
  DotV2,
  DotV4,
  DotV8,

  // ── Opcodes ──────────────────────────────────────────────────
  Mov,
  Ld,
  St,
  Cvt,
  CvtPack,
  Cvta,
  Prefetch,
  Cp,
  Createpolicy,
  Add,
  Sub,
  Mul,
  Mul24,
  Mad,
  Mad24,
  Div,
  Rem,
  Abs,
  Neg,
  Min,
  Max,
  Sad,
  AddCc,
  Addc,
  SubCc,
  Subc,
  Madc,
  Dp4a,
  Dp2a,
  Fma,
  Rcp,
  Sqrt,
  Rsqrt,
  Sin,
  Cos,
  Lg2,
  Ex2,
  Tanh,
  Copysign,
  And,
  Or,
  Xor,
  Not,
  Shl,
  Shr,
  Shf,
  Bfe,
  Bfi,
  Brev,
  Bmsk,
  Clz,
  Popc,
  Prmt,
  Setp,
  Set,
  Selp,
  Bra,
  Call,
  Ret,
  Trap,
  Bar,
  Barrier,
  Membar,
  Atom,
  Nanosleep,
  Activemask,
  Vote,
  Redux,
  Shfl,
  Ldmatrix,
  Mma,
  Griddepcontrol,

  // ── Instruction modifiers (dot-tokens) ───────────────────────

  // Rounding
  DotRn,
  DotRz,
  DotRm,
  DotRp,
  DotRni,
  DotRzi,
  DotRmi,
  DotRpi,
  DotRna,

  // FP flags
  DotFtz,
  DotSat,
  DotSatfinite,
  DotRelu,
  DotApprox,
  DotFull,
  DotNaN,

  // ld/st qualifiers
  DotWeak2,  // .weak as instruction qualifier (vs linking)
  DotVolatile,
  DotRelaxed,
  DotAcquire,
  DotRelease,
  DotAcqRel,

  // Memory scopes
  DotCta,
  DotCluster,
  DotGpu,
  DotSys,

  // State spaces (as modifiers)
  DotGeneric,

  // Cache operators
  DotCa,
  DotCg,
  DotCs,
  DotLu,
  DotCv,  // ld
  DotWb,
  DotWt,  // st
  DotL1,
  DotL2,

  // Atomic ops
  DotAnd,
  DotOr,
  DotXor,
  DotExch,
  DotInc,
  DotDec,
  DotAdd,
  DotMin,
  DotMax,
  DotCas,

  // Mul / mad modes
  DotHi,
  DotLo,
  DotWide,
  DotCc,

  // Comparison ops
  DotEq,
  DotNe,
  DotLt2,
  DotLe,
  DotGt2,
  DotGe,
  DotLs,
  DotHs,
  DotEqu,
  DotNeu,
  DotLtu,
  DotLeu,
  DotGtu,
  DotGeu,
  DotNum,
  DotNan,

  // Vote / shuffle / bar
  DotUni,
  DotSync,
  DotAligned,
  DotAll,
  DotAny,
  DotBallot,
  DotUp,
  DotDown,
  DotBfly,
  DotIdx,
  DotWarp,
  DotRed,

  // ldmatrix / mma
  DotM8n8,
  DotM16n16,
  DotX1,
  DotX2,
  DotX4,
  DotTrans,
  DotRow,
  DotCol,
  DotM16n8k16,
  DotM16n8k32,

  // shf / bmsk
  DotClamp,
  DotWrap,
  DotL,
  DotR,

  // griddepcontrol
  DotLaunchDependents,
  DotWait,

  // cp.async / createpolicy
  DotNc,
  DotNoftz,
  DotFractional,
};

/**
 * Semantic value attached to each token.
 * In a Bison %union this would be the union members.
 */
struct PtxSVal {
  std::string_view sv;  // text slice (for Ident, DotIdent, literals)
};

};  // namespace ptx_frontend