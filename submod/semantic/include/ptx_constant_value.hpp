#pragma once

#include <bit>
#include <cstdint>
#include <variant>

namespace ptx_frontend::declaration_semantics {
/** A fully evaluated integer constant with the signedness used by PTX rules. */
struct IntegerConstantValue {
  /** Two's-complement bits of the evaluated 64-bit integer expression. */
  uint64_t bits{};
  /** True when the expression's usual-arithmetic result is unsigned. */
  bool is_unsigned{};

  /** Compare the normalized integer bits and signedness. */
  bool operator==(const IntegerConstantValue&) const = default;
};

/** A compile-time floating expression evaluated in the PTX f64 domain. */
struct FloatingConstantValue {
  /** Binary64 value before conversion to an instruction operand type. */
  double value{};
  /** Compare evaluated values for owned constant transport. */
  bool operator==(const FloatingConstantValue& other) const {
    return std::bit_cast<uint64_t>(value) ==
           std::bit_cast<uint64_t>(other.value);
  }
};

/** Evaluated numeric source, independent of any instruction destination width. */
using NumericConstantValue =
    std::variant<IntegerConstantValue, FloatingConstantValue>;
}  // namespace ptx_frontend::declaration_semantics
