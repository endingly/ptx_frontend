#include "ptx_ir/base.hpp"

namespace ptx_frontend {

ScalarKind scalar_kind(ScalarType t) {
  switch (t) {
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
  return ScalarKind::Bit;  // unreachable
}

uint8_t scalar_size_of(ScalarType t) {
  switch (t) {
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

std::string to_string(bool v) {
  return v ? "true" : "false";
}

std::string to_string(std::optional<bool> t) {
  if (!t.has_value())
    return "nullopt";
  return t.value() ? "true" : "false";
}

};  // namespace ptx_frontend