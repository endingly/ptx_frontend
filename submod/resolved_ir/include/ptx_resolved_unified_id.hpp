#pragma once

#include <cstdint>

namespace ptx_frontend::resolved_ir {

/**
 * The two numeric UUID operands of PTX `.attribute(.unified(...))`.
 *
 * ``upper`` is source operand `uuid1` and ``lower`` is source operand `uuid2`.
 * They are numeric halves, not a byte sequence, host address, or host-endian
 * representation.
 */
struct ResolvedUnifiedId {
  /** PTX `uuid1`, the upper 64-bit numeric half. */
  uint64_t upper{};
  /** PTX `uuid2`, the lower 64-bit numeric half. */
  uint64_t lower{};
  /** Compare the complete source-level UUID identity by numeric halves. */
  bool operator==(const ResolvedUnifiedId&) const = default;
};

}  // namespace ptx_frontend::resolved_ir
