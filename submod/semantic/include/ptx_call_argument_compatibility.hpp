#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <ptx_frontend/base/base.hpp>

namespace ptx_frontend::call_argument_compatibility {

/** State space of a value passed at a direct-call boundary. */
enum class CallArgumentStateSpace : uint8_t {
  Invalid,
  Register,
  Parameter,
  Local,
  Shared,
  Global,
  Constant,
};

/** A concrete state space addressed by a pointer. */
enum class PointedStateSpace : uint8_t {
  Local,
  Shared,
  Global,
  Constant,
  Invalid,
};

/** Vector lane shape carried by a direct-call ABI value. */
enum class CallArgumentVectorShape : uint8_t {
  Invalid,
  Scalar,
  V2,
  V4,
};

/** Canonical pointer contract supplied by the caller. */
struct PointerProperties {
  // An absent space is generic.
  std::optional<PointedStateSpace> pointed_state_space;
  uint64_t pointed_alignment = 4;

  bool operator==(const PointerProperties&) const = default;
};

/**
 * Canonical properties shared by a formal parameter and an actual argument.
 *
 * Scalar/vector formals and actuals may independently be Register or
 * Parameter. `scalar_type` and `vector_shape` are normalized separately;
 * `type_spelling` survives only for source diagnostics. `array_alignment` is
 * the effective byte alignment used only for `.param .b8` arrays. An unsized
 * array has `is_array == true` and no `array_size`.
 */
struct CallArgumentProperties {
  /** Semantic state space, or Invalid for unsupported constructed input. */
  CallArgumentStateSpace state_space = CallArgumentStateSpace::Invalid;
  /** Modeled scalar identity, or Invalid for an unsupported source spelling. */
  base::ScalarType scalar_type{base::ScalarType::Invalid};
  /** Vector lane shape, or Invalid for an unsupported constructed value. */
  CallArgumentVectorShape vector_shape{CallArgumentVectorShape::Invalid};
  /** Retained source spelling for diagnostics; ABI comparison does not read it. */
  std::string type_spelling;
  /** Effective byte alignment used by parameter-byte arrays. */
  uint64_t array_alignment = 1;
  /** Whether this value is an array ABI argument. */
  bool is_array{};
  /** Known byte-array extent; absent denotes an unsized formal. */
  std::optional<uint64_t> array_size;
  /** Optional pointer ABI properties. */
  std::optional<PointerProperties> pointer;

  /** Compare every normalized ABI field, including retained diagnostic spelling. */
  bool operator==(const CallArgumentProperties&) const = default;
};

/** The first incompatible property, or Compatible. */
enum class CallArgumentCompatibility : uint8_t {
  Compatible,
  FormalStateSpaceMismatch,
  ActualStateSpaceMismatch,
  TypeMismatch,
  ArrayMismatch,
  ArraySizeMismatch,
  AlignmentMismatch,
  PointerMismatch,
  PointedStateSpaceMismatch,
  PointedAlignmentMismatch,
};

/**
 * Check whether `actual` satisfies the (intentionally asymmetric) `formal`.
 */
[[nodiscard]] CallArgumentCompatibility checkCallArgumentCompatibility(
    const CallArgumentProperties& formal, const CallArgumentProperties& actual);

}  // namespace ptx_frontend::call_argument_compatibility
