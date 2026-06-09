#pragma once

#include <cstdint>
#include <string_view>

#include "ptx_ir/source_loc.hpp"

namespace ptx_frontend {

/**
 * TokenKind -- terminal tokens produced by the PTX lexer.
 *
 * The lexer intentionally does not classify instruction opcodes, instruction
 * modifiers, type suffixes, shape suffixes, cache operators, or memory scopes
 * into dedicated tokens. Those spellings are reported as Ident or DotIdent and
 * interpreted later by the parser / generated instruction matcher.
 */
enum class TokenKind : uint16_t {
  // End / error
  Eof,
  Error,

  // Punctuation
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

  // Literals
  F32Hex,   // 0f<8hex>
  F64Hex,   // 0d<16hex>
  Hex,      // 0x...  (integer hex, optional U suffix)
  F64,      // decimal floating literal
  Decimal,  // decimal integer, optional U suffix
  String,   // "..."
  WarpSz,   // WARP_SZ

  // Dynamic text tokens
  Ident,     // opcode, symbol, label, register, PTX special register, etc.
  DotIdent,  // .suffix, .modifier, .type, .state-space, .shape, etc.

  // Stable PTX module / debug directives
  DotVersion,
  DotTarget,
  DotAddressSize,
  DotLoc,
  DotPragma,
  DotFile,
  DotSection,

  // Linking / visibility directives
  DotExtern,
  DotVisible,
  DotWeak,

  // Function / entry directives
  DotEntry,
  DotFunc,

  // Kernel tuning directives
  DotMaxnreg,
  DotMaxntid,
  DotReqntid,
  DotMinnctapersm,
  DotNoreturn,

  // Declaration directives / declaration qualifiers
  DotReg,
  DotAlign,
  DotPtr,
  DotGlobal,
  DotConst,
  DotShared,
  DotLocal,
  DotParam,
};

struct PtxSVal {
  std::string_view sv;
  SourceRange range;
};

struct PtxLexerExtra {
  SourcePos pos{1, 1};
};

}  // namespace ptx_frontend