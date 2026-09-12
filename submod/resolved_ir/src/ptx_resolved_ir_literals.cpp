#include "ptx_resolved_ir_private.hpp"

#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

#include <ptx_frontend/base/ptx_integer.hpp>

namespace ptx_frontend::resolved_ir {
namespace detail {
ResolveDiagnostic invalid_immediate(const syntax_ast::AstImmediate& immediate,
                                    std::string message) {
  return ResolveDiagnostic{
      .range = immediate.syntax.range,
      .message = std::move(message),
  };
}

std::expected<uint64_t, ResolveDiagnostic> parse_unsigned_literal(
    const syntax_ast::AstImmediate& immediate, std::string_view text,
    int base) {
  if (!text.empty() && (text.back() == 'u' || text.back() == 'U'))
    text.remove_suffix(1);

  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size()) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Invalid integer literal '{}'.", immediate.syntax.text)));
  }
  return value;
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_integer_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    std::string_view text, bool negative, bool require_target_range = false) {
  using base::ScalarKind;
  const ScalarKind kind = scalar_kind(type);
  if (kind != ScalarKind::Unsigned && kind != ScalarKind::Signed &&
      kind != ScalarKind::Bit) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format(
            "Integer literal '{}' is incompatible with scalar type '{}'.",
            immediate.syntax.text, to_string(type))));
  }
  const uint8_t byte_size = scalar_size_of(type);
  if (byte_size == 0 || byte_size > sizeof(uint64_t)) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Immediate type '{}' is not representable in 64 bits.",
                    to_string(type))));
  }
  const uint8_t bit_width = byte_size * 8;
  const uint64_t bit_mask = bit_width == 64
                                ? std::numeric_limits<uint64_t>::max()
                                : (uint64_t{1} << bit_width) - 1;

  const auto magnitude = base::parseIntegerMagnitude(text);
  if (!magnitude)
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Invalid integer literal '{}'.", immediate.syntax.text)));

  const bool source_is_unsigned =
      text.ends_with('u') || text.ends_with('U') ||
      *magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  const uint64_t source_bits = negative ? uint64_t{0} - *magnitude : *magnitude;
  const bool source_is_negative =
      !source_is_unsigned && std::bit_cast<int64_t>(source_bits) < 0;
  if (require_target_range) {
    const uint64_t positive_limit = kind == base::ScalarKind::Signed
                                        ? (uint64_t{1} << (bit_width - 1)) - 1
                                        : bit_mask;
    const uint64_t negative_limit = kind == base::ScalarKind::Signed
                                        ? uint64_t{1} << (bit_width - 1)
                                        : bit_mask;
    const bool representable = source_is_negative
                                   ? uint64_t{0} - source_bits <= negative_limit
                                   : source_bits <= positive_limit;
    if (!representable) {
      return std::unexpected(invalid_immediate(
          immediate,
          fmt::format(
              "Integer literal '{}' is out of range for scalar type '{}'.",
              immediate.syntax.text, to_string(type))));
    }
  }
  return ResolvedImmediate{
      .bits = source_bits & bit_mask,
      .type = type,
      .is_negative = source_is_negative,
      .integer_source_bits = source_bits,
  };
}

/**
 * Convert IEEE binary64 bits to binary32 using round-to-nearest, ties-to-even.
 * Literal conversion is independent of instruction rounding and host floating
 * point modes. Overflow becomes signed infinity; underflow is gradual. NaNs
 * retain their sign and high payload bits and are quieted across precisions.
 */
uint32_t narrow_float_literal_bits(uint64_t bits) {
  const auto sign = static_cast<uint32_t>(bits >> 32) & 0x80000000U;
  const auto exponent = static_cast<int>((bits >> 52) & 0x7ffU);
  const uint64_t fraction = bits & 0x000fffffffffffffULL;
  if (exponent == 0x7ff) {
    return sign | 0x7f800000U |
           (fraction == 0
                ? 0U
                : static_cast<uint32_t>(fraction >> 29) | 0x00400000U);
  }
  // Every binary64 subnormal is smaller than half a binary32 subnormal ULP.
  if (exponent == 0)
    return sign;
  const int unbiased_exponent = exponent - 1023;
  if (unbiased_exponent > 127)
    return sign | 0x7f800000U;
  if (unbiased_exponent < -150)
    return sign;

  const uint64_t significand = (uint64_t{1} << 52) | fraction;
  // Retain 24 bits for normals, or fewer for subnormals (shift is 29..53).
  const int shift = unbiased_exponent >= -126 ? 29 : -97 - unbiased_exponent;
  uint32_t rounded = static_cast<uint32_t>(significand >> shift);
  const uint64_t remainder = significand & ((uint64_t{1} << shift) - 1);
  const uint64_t halfway = uint64_t{1} << (shift - 1);
  if (remainder > halfway || (remainder == halfway && (rounded & 1U)))
    ++rounded;
  if (unbiased_exponent < -126)
    return sign | rounded;
  // The retained implicit bit supplies one exponent unit; rounding may carry
  // into the next exponent, including the infinity encoding at overflow.
  return sign |
         ((static_cast<uint32_t>(unbiased_exponent + 126) << 23) + rounded);
}

/**
 * Widen IEEE binary32 literal bits exactly without host floating arithmetic.
 * Subnormals and signed zero are preserved; NaNs are quieted with their sign
 * and payload retained in the high binary64 fraction bits.
 */
uint64_t widen_float_literal_bits(uint32_t bits) {
  const uint64_t sign = static_cast<uint64_t>(bits & 0x80000000U) << 32;
  const uint32_t exponent = (bits >> 23) & 0xffU;
  const uint32_t fraction = bits & 0x007fffffU;
  if (exponent == 0xffU) {
    return sign | 0x7ff0000000000000ULL |
           (fraction == 0 ? 0ULL
                          : (static_cast<uint64_t>(fraction) << 29) |
                                0x0008000000000000ULL);
  }
  if (exponent != 0) {
    return sign | (static_cast<uint64_t>(exponent + 896) << 52) |
           (static_cast<uint64_t>(fraction) << 29);
  }
  if (fraction == 0)
    return sign;
  // A binary32 subnormal is fraction * 2^-149, normal in binary64.
  const int leading_bit = std::bit_width(fraction) - 1;
  return sign | (static_cast<uint64_t>(leading_bit + 874) << 52) |
         ((static_cast<uint64_t>(fraction) << (52 - leading_bit)) &
          0x000fffffffffffffULL);
}

/**
 * Decode a floating literal in its lexical precision, then convert to the
 * operand precision. Equal-width literals retain their exact payload bits.
 */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_float_bits_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    std::string_view text, bool negative, uint8_t bit_width) {
  if (negative) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Floating bit-pattern literal '{}' cannot have a sign.",
                    immediate.syntax.text)));
  }
  if (type != ScalarType::F32 && type != ScalarType::F64) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format(
            "Floating bit-pattern literal '{}' is incompatible with scalar "
            "type '{}'.",
            immediate.syntax.text, to_string(type))));
  }

  text.remove_prefix(2);  // 0f or 0d
  const auto bits = parse_unsigned_literal(immediate, text, 16);
  if (!bits)
    return std::unexpected(bits.error());
  if (bit_width == 32 && type == ScalarType::F64) {
    return ResolvedImmediate{
        .bits = widen_float_literal_bits(static_cast<uint32_t>(*bits)),
        .type = type};
  }
  if (bit_width == 64 && type == ScalarType::F32) {
    return ResolvedImmediate{.bits = narrow_float_literal_bits(*bits),
                             .type = type};
  }
  return ResolvedImmediate{.bits = *bits, .type = type};
}

std::expected<ResolvedImmediate, ResolveDiagnostic>
resolve_decimal_float_literal(const syntax_ast::AstImmediate& immediate,
                              ScalarType type, std::string_view text) {
  if (type != ScalarType::F32 && type != ScalarType::F64) {
    return std::unexpected(invalid_immediate(
        immediate,
        fmt::format("Decimal floating literal '{}' is incompatible with scalar "
                    "type '{}'.",
                    immediate.syntax.text, to_string(type))));
  }

  if (!text.empty() && text.front() == '+') {
    text.remove_prefix(1);
    if (text.empty() || text.front() == '+' || text.front() == '-') {
      return std::unexpected(invalid_immediate(
          immediate, fmt::format("Invalid decimal floating literal '{}'.",
                                 immediate.syntax.text)));
    }
  }

  double value = 0.0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value,
                      std::chars_format::general);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size() || !std::isfinite(value)) {
    return std::unexpected(invalid_immediate(
        immediate, fmt::format("Invalid decimal floating literal '{}'.",
                               immediate.syntax.text)));
  }

  if (type == ScalarType::F32) {
    return ResolvedImmediate{
        .bits = narrow_float_literal_bits(std::bit_cast<uint64_t>(value)),
        .type = type};
  }
  return ResolvedImmediate{.bits = std::bit_cast<uint64_t>(value),
                           .type = type};
}

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    bool require_target_range) {
  std::string_view text = immediate.syntax.text;
  bool negative = false;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }

  switch (immediate.kind) {
    case syntax_ast::AstImmediateKind::DecimalInteger:
    case syntax_ast::AstImmediateKind::HexInteger:
      return resolve_integer_literal(immediate, type, text, negative,
                                     require_target_range);
    case syntax_ast::AstImmediateKind::F32Hex:
      return resolve_float_bits_literal(immediate, type, text, negative, 32);
    case syntax_ast::AstImmediateKind::F64Hex:
      return resolve_float_bits_literal(immediate, type, text, negative, 64);
    case syntax_ast::AstImmediateKind::DecimalFloat:
      return resolve_decimal_float_literal(immediate, type,
                                           immediate.syntax.text);
    case syntax_ast::AstImmediateKind::WarpSize:
      // PTX defines WARP_SZ as the signed source integer constant 32.
      return resolve_integer_literal(immediate, type, "32", negative,
                                     require_target_range);
  }
  throw ResolveException("Unknown AstImmediateKind.");
}

}  // namespace detail

std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type) {
  return detail::resolve_immediate_value(immediate, type);
}

std::expected<WithLocs<ResolvedImmediate>, ResolveDiagnostic>
resolve_call_literal(
    const ResolvedCallLiteral& literal, SourceRange range,
    const declaration_semantics::FunctionParameterContract& formal) {
  if (formal.scalar_type == ScalarType::Invalid) {
    return std::unexpected(ResolveDiagnostic{
        .range = range,
        .message = fmt::format(
            "Call literal '{}' has unsupported formal scalar type '{}'.",
            literal.spelling, formal.type_spelling),
    });
  }
  const syntax_ast::AstImmediate immediate{
      .syntax = {.text = literal.spelling, .range = range},
      .kind = literal.kind,
  };
  // Call arguments retain their formal-parameter representability contract.
  auto resolved =
      detail::resolve_immediate_value(immediate, formal.scalar_type, true);
  if (!resolved)
    return std::unexpected(resolved.error());
  return WithLocs<ResolvedImmediate>{std::move(*resolved), range};
}

}  // namespace ptx_frontend::resolved_ir
