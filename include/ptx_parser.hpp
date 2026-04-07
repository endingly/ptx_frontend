#pragma once
#include <expected.hpp>
#include <string>
#include "ptx_ir.hpp"
#include "ptx_lexer.hpp"

namespace ptx_frontend {

struct ParseError {
  int line;
  std::string message;
};

/**
 * Parse a full PTX module from source text.
 *
 *   auto result = parse_module(src);
 *   if (!result) { /* handle result.error() *\/ }
 *   Module& mod = *result;
 */
tl::expected<Module, ParseError> parse_module(std::string_view src);

}  // namespace ptx_frontend