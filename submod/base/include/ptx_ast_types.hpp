#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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

/** Identity kind of an opaque PTX resource, independent of physical layout. */
enum class OpaqueResourceKind : uint8_t { Texture, Sampler, Surface };

/** Classify a source declaration type without assigning resource storage bytes. */
[[nodiscard]] inline std::optional<OpaqueResourceKind> opaque_resource_kind(
    std::string_view spelling) noexcept {
  if (spelling == ".texref")
    return OpaqueResourceKind::Texture;
  if (spelling == ".samplerref")
    return OpaqueResourceKind::Sampler;
  if (spelling == ".surfref")
    return OpaqueResourceKind::Surface;
  return std::nullopt;
}

/** Semantic declaration-space name for values interpreted outside the AST. */
using DeclarationStateSpace = AstStateSpace;

/** Semantic literal-category name for deferred source provenance. */
using LiteralCategory = AstImmediateKind;

}  // namespace ptx_frontend::base

/** Preserve the established AST namespace without requiring AST definitions. */
namespace ptx_frontend::syntax_ast {
using AstImmediateKind = base::AstImmediateKind;
using AstStateSpace = base::AstStateSpace;
}  // namespace ptx_frontend::syntax_ast
