#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

#include <limits>
#include <numeric>
#include <tuple>

namespace ptx_frontend::resolved_ir {
namespace {
/** Compare source positions without assuming a single-line operand. */
bool before(SourcePos left, SourcePos right) {
  return std::tie(left.line, left.column) <= std::tie(right.line, right.column);
}
/** Require a nonempty positive source range inside its owning operand. */
bool inside(SourceRange child, SourceRange outer) {
  return child.start.line > 0 && child.start.column > 0 &&
         before(outer.start, child.start) && before(child.end, outer.end) &&
         before(child.start, child.end) && child.start != child.end;
}
/** Return the largest power of two dividing a byte displacement. */
uint64_t low_bit(uint64_t value) {
  return value & (uint64_t{0} - value);
}
}  // namespace

std::optional<int64_t> named_array_byte_displacement(
    const ResolvedNamedArrayIndex& index) {
  if (index.scalar_stride == 0 ||
      (index.operation != ResolvedAddressOffsetOperator::Add &&
       index.operation != ResolvedAddressOffsetOperator::Subtract))
    return std::nullopt;
  const auto* constant =
      std::get_if<declaration_semantics::IntegerConstantValue>(&index.index);
  if (constant && (index.displacement ||
                   index.operation != ResolvedAddressOffsetOperator::Add))
    return std::nullopt;
  const auto value = constant
                         ? *constant
                         : index.displacement.value_or(
                               declaration_semantics::IntegerConstantValue{});
  const bool source_negative =
      !value.is_unsigned && std::bit_cast<int64_t>(value.bits) < 0;
  const uint64_t magnitude =
      source_negative ? uint64_t{0} - value.bits : value.bits;
  const bool negative =
      source_negative !=
      (index.operation == ResolvedAddressOffsetOperator::Subtract);
  const uint64_t limit =
      negative ? uint64_t{1} << 63 : static_cast<uint64_t>(INT64_MAX);
  if (magnitude > limit / index.scalar_stride)
    return std::nullopt;
  const uint64_t bytes = magnitude * index.scalar_stride;
  return std::bit_cast<int64_t>(negative ? uint64_t{0} - bytes : bytes);
}

bool valid_named_array_address(const ResolvedAddress& address,
                               NamedArrayAddressPolicy policy,
                               SourceRange range) {
  if (!address.named_index)
    return true;
  if (address.unified || address.unified_range != SourceRange{})
    return false;
  if (policy != NamedArrayAddressPolicy::Memory &&
      policy != NamedArrayAddressPolicy::Mov)
    return false;
  const auto& index = *address.named_index;
  const auto* symbol = std::get_if<ResolvedSymbolRef>(&address.base);
  const auto bytes = named_array_byte_displacement(index);
  if (!symbol || !symbol->symbol_id || !symbol->declared_type ||
      address.offset ||
      index.scalar_stride != base::scalar_size_of(*symbol->declared_type) ||
      !bytes || *bytes != index.byte_displacement ||
      (policy == NamedArrayAddressPolicy::Memory &&
       (*bytes < INT32_MIN || *bytes > INT32_MAX)) ||
      !inside(index.range, range) || !inside(index.base_range, range) ||
      !inside(index.left_bracket_range, index.range) ||
      !inside(index.right_bracket_range, index.range) ||
      !inside(index.index_range, index.range) ||
      !before(index.base_range.end, index.left_bracket_range.start) ||
      !before(index.left_bracket_range.end, index.index_range.start) ||
      !before(index.index_range.end, index.right_bracket_range.start))
    return false;
  if (const auto* reg =
          std::get_if<WithLocs<ResolvedRegisterRef>>(&index.index)) {
    const auto& value = reg->value;
    if (!value.symbol_id || !value.declared_type ||
        value.register_class != ResolvedRegisterClass::General ||
        value.vector_width || value.component || reg->locs.size() != 1 ||
        reg->locs.front() != index.index_range)
      return false;
    const auto kind = base::scalar_kind(*value.declared_type);
    if ((kind != base::ScalarKind::Unsigned &&
         kind != base::ScalarKind::Signed && kind != base::ScalarKind::Bit) ||
        base::scalar_size_of(*value.declared_type) > 8)
      return false;
  }
  if (index.displacement)
    return inside(index.operator_range, index.range) &&
           inside(index.displacement_range, index.range) &&
           before(index.index_range.end, index.operator_range.start) &&
           before(index.operator_range.end, index.displacement_range.start) &&
           before(index.displacement_range.end,
                  index.right_bracket_range.start);
  return index.operation == ResolvedAddressOffsetOperator::Add &&
         index.operator_range == SourceRange{} &&
         index.displacement_range == SourceRange{};
}

std::optional<uint64_t> resolved_address_alignment(
    const ResolvedAddress& address) {
  std::optional<uint64_t> alignment;
  if (const auto* symbol = std::get_if<ResolvedSymbolRef>(&address.base))
    alignment = symbol->address_alignment;
  else if (const auto* immediate =
               std::get_if<ResolvedImmediate>(&address.base))
    alignment = low_bit(immediate->bits);
  uint64_t contribution = address.offset ? address.offset->value.bits : 0;
  if (address.named_index) {
    if (std::holds_alternative<WithLocs<ResolvedRegisterRef>>(
            address.named_index->index) &&
        alignment)
      alignment = std::gcd(*alignment, address.named_index->scalar_stride);
    contribution =
        static_cast<uint64_t>(address.named_index->byte_displacement);
  }
  if (alignment && contribution)
    alignment = std::gcd(*alignment, low_bit(contribution));
  return alignment;
}
}  // namespace ptx_frontend::resolved_ir
