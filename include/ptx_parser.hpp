#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "ptx_ir/ptx_ir.hpp"

namespace ptx_frontend {
using generated::ParsedInstruction;

struct ParseDiagnostic {
  SourceRange range;
  std::string message;
};

/**
 * Public PTX parser facade.
 *
 * All lexer state, ParserCore state and generated dispatch logic are hidden
 * in the implementation.
 */
class PtxParser {
 public:
  explicit PtxParser(std::string_view source);

  ~PtxParser();

  PtxParser(const PtxParser&) = delete;

  PtxParser& operator=(const PtxParser&) = delete;

  PtxParser(PtxParser&&) noexcept;

  PtxParser& operator=(PtxParser&&) noexcept;

  /**
   * Parse one instruction from the current source position.
   */
  std::expected<ParsedInstruction, ParseDiagnostic> parseInstruction();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Convenience API for parsing exactly one instruction.
 */
std::expected<ParsedInstruction, ParseDiagnostic> parseInstruction(
    std::string_view source);

}  // namespace ptx_frontend