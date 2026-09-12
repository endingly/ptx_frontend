#pragma once

#include <string_view>

#include <gtest/gtest.h>

#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir::test_helpers {

/** Parse a complete source module without asserting so the caller owns failure control flow. */
inline SyntaxModuleParseResult parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  return parser.parseModule();
}

/** Parse one source instruction without asserting so the caller owns failure control flow. */
inline SyntaxInstructionParseResult parseInstruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  return parser.parseInstruction();
}

}  // namespace ptx_frontend::resolved_ir::test_helpers

/**
 * Stop the current test when a named module parse result is incomplete.
 *
 * The argument must be a stable lvalue. It is evaluated once so callers can
 * safely reuse it after this assertion before dereferencing its AST value.
 */
#define ASSERT_MODULE_PARSE_SUCCEEDS(result_lvalue)             \
  do {                                                          \
    const auto& parse_result = (result_lvalue);                 \
    ASSERT_TRUE(parse_result.has_value())                       \
        << (parse_result.diagnostics.empty()                    \
                ? "PTX source did not produce a syntax module." \
                : parse_result.diagnostics.front().message);    \
    ASSERT_TRUE(parse_result.diagnostics.empty())               \
        << parse_result.diagnostics.front().message;            \
  } while (false)

/**
 * Stop the current test when a named instruction parse result is incomplete.
 *
 * The argument must be a stable lvalue. It is evaluated once so callers can
 * safely reuse it after this assertion before dereferencing its AST value.
 */
#define ASSERT_INSTRUCTION_PARSE_SUCCEEDS(result_lvalue)             \
  do {                                                               \
    const auto& parse_result = (result_lvalue);                      \
    ASSERT_TRUE(parse_result.has_value())                            \
        << (parse_result.diagnostics.empty()                         \
                ? "PTX source did not produce a syntax instruction." \
                : parse_result.diagnostics.front().message);         \
    ASSERT_TRUE(parse_result.diagnostics.empty())                    \
        << parse_result.diagnostics.front().message;                 \
  } while (false)
