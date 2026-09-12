#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::base {

std::span<const ScalarTypeMetadata> scalar_type_metadata() noexcept {
  return detail::kScalarTypeMetadata;
}

const ScalarTypeMetadata* find_scalar_type_metadata(ScalarType type) noexcept {
  const size_t index = static_cast<size_t>(type);
  if (index == 0 || index > detail::kScalarTypeMetadata.size())
    return nullptr;
  const auto& metadata = detail::kScalarTypeMetadata[index - 1];
  return metadata.type == type ? &metadata : nullptr;
}

const ScalarTypeMetadata* find_scalar_type_metadata(
    std::string_view source_spelling) noexcept {
  for (const auto& metadata : scalar_type_metadata()) {
    if (metadata.source_spelling == source_spelling)
      return &metadata;
  }
  return nullptr;
}

ScalarKind scalar_kind(ScalarType t) {
  const auto* metadata = find_scalar_type_metadata(t);
  return metadata ? metadata->kind : ScalarKind::Invalid;
}

uint8_t scalar_size_of(ScalarType t) {
  const auto* metadata = find_scalar_type_metadata(t);
  return metadata ? metadata->size_bytes : 0;
}

bool scalar_types_compatible(ScalarType actual, ScalarType instruction,
                             ScalarTypeSizePolicy size_policy) {
  if (actual == instruction)
    return true;
  if (size_policy == ScalarTypeSizePolicy::Exact)
    return false;
  // Reject wider .b128 until declaration-type availability is
  // checked centrally; remove this guard when that registry/checker exists.
  if (size_policy == ScalarTypeSizePolicy::EqualOrWider &&
      actual == ScalarType::B128)
    return false;
  const uint8_t actual_size = scalar_size_of(actual);
  const uint8_t instruction_size = scalar_size_of(instruction);
  if (actual_size != instruction_size &&
      (size_policy != ScalarTypeSizePolicy::EqualOrWider ||
       actual_size < instruction_size))
    return false;

  const ScalarKind actual_kind = scalar_kind(actual);
  const ScalarKind instruction_kind = scalar_kind(instruction);
  if (actual_kind == ScalarKind::Bit || instruction_kind == ScalarKind::Bit)
    return true;
  const auto is_fundamental_integer = [](ScalarType type) {
    switch (type) {
      case ScalarType::U8:
      case ScalarType::U16:
      case ScalarType::U32:
      case ScalarType::U64:
      case ScalarType::S8:
      case ScalarType::S16:
      case ScalarType::S32:
      case ScalarType::S64:
        return true;
      default:
        return false;
    }
  };
  return is_fundamental_integer(actual) && is_fundamental_integer(instruction);
}

};  // namespace ptx_frontend::base
