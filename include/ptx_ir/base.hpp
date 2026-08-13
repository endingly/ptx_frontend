#pragma once
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <type_traits>

namespace ptx_frontend {

enum class ScalarType : uint8_t {
  Invalid = 0,
  U8,
  U8x4,
  U16,
  U16x2,
  U32,
  U64,
  S8,
  S8x4,
  S16,
  S16x2,
  S32,
  S64,
  B8,
  B16,
  B32,
  B64,
  B128,
  F16,
  F16x2,
  F32,
  F32x2,
  F64,
  BF16,
  BF16x2,
  E4m3x2,
  E5m2x2,
  Pred,
  TF32,  // .tf32  — 19-bit mantissa, sm_80+ tensor core
  E4m3,  // .e4m3  — FP8 single element (non packed)
  E5m2,  // .e5m2  — FP8 single element (non packed)
};

enum class ScalarKind { Invalid, Bit, Unsigned, Signed, Float, Pred };

template <typename Enum>
  requires std::is_enum_v<Enum>
std::string to_string(Enum e) {
  return std::string{magic_enum::enum_name(e)};
}

ScalarKind scalar_kind(ScalarType t);
uint8_t scalar_size_of(ScalarType t);

};  // namespace ptx_frontend
