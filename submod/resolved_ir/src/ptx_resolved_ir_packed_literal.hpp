#pragma once

#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Identify the two 32-bit FP8x4 scalar types with raw packed literal support. */
constexpr bool is_raw32_fp8x4_type(base::ScalarType type) noexcept {
  return type == base::ScalarType::E4m3x4 || type == base::ScalarType::E5m2x4;
}

}  // namespace ptx_frontend::resolved_ir::detail
