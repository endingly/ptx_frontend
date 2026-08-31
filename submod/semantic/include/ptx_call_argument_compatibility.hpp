#pragma once

#include <cstdint>
#include <optional>
#include <string>

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
 * Parameter. `type_spelling` is the normalized scalar or vector ABI spelling
 * (including its shape) and is compared exactly. `array_alignment` is the
 * effective byte alignment used only for `.param .b8` arrays. An unsized array
 * has `is_array == true` and no `array_size`.
 */
struct CallArgumentProperties {
  CallArgumentStateSpace state_space = CallArgumentStateSpace::Invalid;
  std::string type_spelling;
  uint64_t array_alignment = 1;
  bool is_array{};
  std::optional<uint64_t> array_size;
  std::optional<PointerProperties> pointer;

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
