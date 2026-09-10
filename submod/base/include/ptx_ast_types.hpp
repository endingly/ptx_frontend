#pragma once

#include <cstdint>

namespace ptx_frontend::base {

/** Lexical category retained for an immediate literal before resolution. */
enum class AstImmediateKind : uint8_t {
  DecimalInteger,
  HexInteger,
  F32Hex,
  F64Hex,
  DecimalFloat,
  WarpSize,
};

/** PTX declaration state space independent of the complete syntax AST. */
enum class AstStateSpace : uint8_t {
  Register,
  Parameter,
  Local,
  Shared,
  Global,
  Constant,
};

}  // namespace ptx_frontend::base

/** Preserve the established AST namespace without requiring AST definitions. */
namespace ptx_frontend::syntax_ast {
using AstImmediateKind = base::AstImmediateKind;
using AstStateSpace = base::AstStateSpace;
}  // namespace ptx_frontend::syntax_ast
