#pragma once

#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Identify exactly the packed floating formats with a whole 32-bit literal word. */
constexpr bool is_packed32_literal_type(base::ScalarType type) noexcept {
  return type == base::ScalarType::F16x2 || type == base::ScalarType::BF16x2 ||
         type == base::ScalarType::E4m3x4 || type == base::ScalarType::E5m2x4;
}

}  // namespace ptx_frontend::resolved_ir::detail
