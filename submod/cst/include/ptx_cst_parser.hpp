#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/cst/ptx_cst.hpp>
#include <ptx_frontend/lexer/ptx_lexer.hpp>

namespace ptx_frontend {

/** One source-located parser failure, optionally naming a missing token kind. */
struct CstParseDiagnostic {
  /** Original source span; missing-token diagnostics can use an anchor location. */
  SourceRange range;
  /** Owned human-readable failure explanation. */
  std::string message;
  /** Present only when the grammar requires a particular missing token kind. */
  std::optional<TokenKind> expected_kind;
};

using CstParseDiagnostics = DiagnosticCollection<CstParseDiagnostic>;
using CstParseResult =
    ResultWithDiagnostics<syntax_cst::CstFile, CstParseDiagnostic>;

/** Owns one source stream and parses a fragment or module into a lossless CST. */
class PtxCstParser {
 public:
  /** Maximum permitted height of a constant expression or initializer tree. */
  static constexpr std::size_t maxConstantTreeDepth = 128;

  /** Copy source into an owned scanner; the caller's view need not stay alive. */
  explicit PtxCstParser(std::string_view source);
  /** Destroy private parser state without exposing generated implementation types. */
  ~PtxCstParser();
  /** Scanner ownership cannot be duplicated. */
  PtxCstParser(const PtxCstParser&) = delete;
  /** Scanner ownership cannot be reassigned by copying. */
  PtxCstParser& operator=(const PtxCstParser&) = delete;
  /** Preserve the established nonmovable stream-owning public type. */
  PtxCstParser(PtxCstParser&&) = delete;
  /** Preserve the established nonmovable stream-owning public type. */
  PtxCstParser& operator=(PtxCstParser&&) = delete;

  /** Parse one complete instruction; a value exhausts the stream, failure retains it. */
  CstParseResult parseInstruction();
  /** Parse/recover a module; any returned value transfers and exhausts the stream. */
  CstParseResult parseModule();

 private:
  /** Hides the persistent driver and all generated parser headers from consumers. */
  struct Impl;
  /** Sole owner of scanner/cursor state; results independently own transferred tokens. */
  std::unique_ptr<Impl> impl_;
};

}  // namespace ptx_frontend
