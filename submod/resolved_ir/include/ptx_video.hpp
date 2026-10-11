#pragma once

#include <algorithm>
#include <optional>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir {

/** Derived vmad interpretation independent of its written destination suffix. */
struct VideoMadInterpretation {
  /** Product/intermediate sign follows signed multiplicands or product minus. */
  bool product_signed{};
  /** Input-C interpretation from the instruction Description. */
  bool c_signed{};
  /** Final-result interpretation, independent of the written dtype. */
  bool result_signed{};
  /** Compare all derived arithmetic interpretation facts. */
  bool operator==(const VideoMadInterpretation&) const = default;
};

/** Derive vmad arithmetic signs without changing its written dtype or negation.
 * Invalid type enum values are rejected rather than interpreted as unsigned.
 * Negation inputs describe written register controls, never negative literals.
 * Input-C and result signs describe arithmetic interpretation, not the
 * pseudocode's internal c128 sign-extension action. No arithmetic is executed.
 * This query does not validate po or simultaneous product/c negation controls.
 */
inline std::optional<VideoMadInterpretation> video_mad_interpretation(
    VideoType atype, VideoType btype, bool a_negated, bool b_negated,
    bool c_negated) {
  if ((atype != VideoType::S32 && atype != VideoType::U32) ||
      (btype != VideoType::S32 && btype != VideoType::U32))
    return std::nullopt;
  const bool product = atype == VideoType::S32 || btype == VideoType::S32 ||
                       (a_negated != b_negated);
  return VideoMadInterpretation{product, product, product || c_negated};
}

/** Return the packed default without changing the written selector's omission.
 * Arrays follow the spelling's most-significant-to-least-significant digit
 * order. Source indices address concatenated a+b halfwords or bytes.
 * Scalar and merge-c operands have no implicit selector.
 */
inline std::optional<VideoSelector> video_default_selector(
    VideoLanes lanes, VideoOperandPosition position) {
  if (lanes == VideoLanes::Two) {
    switch (position) {
      case VideoOperandPosition::Destination:
        return VideoHalfMask{{1, 0}, 2};
      case VideoOperandPosition::A:
        return VideoHalfSwizzle{{1, 0}};
      case VideoOperandPosition::B:
        return VideoHalfSwizzle{{3, 2}};
      case VideoOperandPosition::C:
        return std::nullopt;
    }
  }
  if (lanes == VideoLanes::Four) {
    switch (position) {
      case VideoOperandPosition::Destination:
        return VideoByteMask{{3, 2, 1, 0}, 4};
      case VideoOperandPosition::A:
        return VideoByteSwizzle{{3, 2, 1, 0}};
      case VideoOperandPosition::B:
        return VideoByteSwizzle{{7, 6, 5, 4}};
      case VideoOperandPosition::C:
        return std::nullopt;
    }
  }
  return std::nullopt;
}

/** Obtain the effective selector while preserving owned written provenance. */
inline std::optional<VideoSelector> video_effective_selector(
    const ResolvedVideoOperand& operand, VideoLanes lanes,
    VideoOperandPosition position) {
  if (operand.selector)
    return operand.selector->value;
  return video_default_selector(lanes, position);
}

/** Validate a typed selector independently of its instruction-specific role.
 * Destination masks are nonempty descending subsets; unused fixed-array
 * entries must be zero so malformed owned payloads cannot hide extra lanes.
 */
inline bool video_selector_is_well_formed(const VideoSelector& selector) {
  return std::visit(
      [](const auto& value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, VideoByteSelector>) {
          return value.index < 4;
        } else if constexpr (std::same_as<Value, VideoHalfSelector>) {
          return value.index < 2;
        } else if constexpr (std::same_as<Value, VideoHalfSwizzle>) {
          return std::ranges::all_of(value.indices,
                                     [](uint8_t index) { return index < 4; });
        } else if constexpr (std::same_as<Value, VideoByteSwizzle>) {
          return std::ranges::all_of(value.indices,
                                     [](uint8_t index) { return index < 8; });
        } else {
          constexpr uint8_t capacity =
              std::tuple_size_v<decltype(value.indices)>;
          if (value.count == 0 || value.count > capacity)
            return false;
          for (uint8_t index = 0; index < capacity; ++index) {
            if (index >= value.count) {
              if (value.indices[index] != 0)
                return false;
            } else if (value.indices[index] >= capacity ||
                       (index != 0 &&
                        value.indices[index - 1] <= value.indices[index])) {
              return false;
            }
          }
          return true;
        }
      },
      selector);
}

}  // namespace ptx_frontend::resolved_ir
