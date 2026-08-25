#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::base {

ScalarKind scalar_kind(ScalarType t) {
  switch (t) {
    case ScalarType::Invalid:
      return ScalarKind::Invalid;
    case ScalarType::U8:
    case ScalarType::U8x4:
    case ScalarType::U16:
    case ScalarType::U16x2:
    case ScalarType::U32:
    case ScalarType::U64:
      return ScalarKind::Unsigned;
    case ScalarType::S8:
    case ScalarType::S8x4:
    case ScalarType::S16:
    case ScalarType::S16x2:
    case ScalarType::S32:
    case ScalarType::S64:
      return ScalarKind::Signed;
    case ScalarType::B8:
    case ScalarType::B16:
    case ScalarType::B32:
    case ScalarType::B64:
    case ScalarType::B128:
      return ScalarKind::Bit;
    case ScalarType::F16:
    case ScalarType::F16x2:
    case ScalarType::F32:
    case ScalarType::F32x2:
    case ScalarType::F64:
    case ScalarType::BF16:
    case ScalarType::BF16x2:
    case ScalarType::E4m3x2:
    case ScalarType::E5m2x2:
    case ScalarType::TF32:
    case ScalarType::E4m3:
    case ScalarType::E5m2:
      return ScalarKind::Float;
    case ScalarType::Pred:
      return ScalarKind::Pred;
  }
  return ScalarKind::Invalid;
}

uint8_t scalar_size_of(ScalarType t) {
  switch (t) {
    case ScalarType::Invalid:
      return 0;
    case ScalarType::U8:
    case ScalarType::S8:
    case ScalarType::B8:
    case ScalarType::Pred:
    case ScalarType::E4m3:
    case ScalarType::E5m2:
      return 1;
    case ScalarType::U16:
    case ScalarType::S16:
    case ScalarType::B16:
    case ScalarType::F16:
    case ScalarType::BF16:
    case ScalarType::E4m3x2:
    case ScalarType::E5m2x2:
      return 2;
    case ScalarType::U32:
    case ScalarType::S32:
    case ScalarType::B32:
    case ScalarType::F32:
    case ScalarType::U16x2:
    case ScalarType::S16x2:
    case ScalarType::F16x2:
    case ScalarType::BF16x2:
    case ScalarType::TF32:
    case ScalarType::U8x4:
    case ScalarType::S8x4:
      return 4;
    case ScalarType::U64:
    case ScalarType::S64:
    case ScalarType::B64:
    case ScalarType::F64:
    case ScalarType::F32x2:
      return 8;
    case ScalarType::B128:
      return 16;
  }
  return 0;
}

bool scalar_types_compatible(ScalarType actual, ScalarType instruction) {
  if (actual == instruction)
    return true;
  if (scalar_size_of(actual) != scalar_size_of(instruction))
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

};  // namespace ptx_frontend
