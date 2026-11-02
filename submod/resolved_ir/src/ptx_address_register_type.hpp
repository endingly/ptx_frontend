#pragma once

#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Preserve the existing ordinary address-register kind and width policy. */
inline bool is_address_register_type(base::ScalarType type) {
  const auto kind = base::scalar_kind(type);
  return (kind == base::ScalarKind::Unsigned ||
          kind == base::ScalarKind::Signed || kind == base::ScalarKind::Bit) &&
         base::scalar_size_of(type) <= sizeof(uint64_t);
}

/** A function address uses a physical fundamental integer/bit register <=64 bits.
 * This is carrier eligibility, not proof of a representable runtime code address.
 */
inline bool is_function_address_register_type(base::ScalarType type) {
  const auto* metadata = base::find_scalar_type_metadata(type);
  return metadata &&
         metadata->register_declaration_usage ==
             base::ScalarDeclarationUsage::Fundamental &&
         metadata->size_bytes != 0 && is_address_register_type(type);
}

}  // namespace ptx_frontend::resolved_ir::detail
