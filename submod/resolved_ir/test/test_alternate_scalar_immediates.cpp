#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sub.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <string>
#include <string_view>
#include "ptx_resolved_ir_private.hpp"
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Exact lexical input and independently adopted expected target word. */
struct AlternateGolden {
  /** Immutable external case identity used to join validation evidence. */
  std::string_view id;
  /** Literal spelling, copied from the independent corpus without conversion. */
  std::string_view source;
  /** Logical scalar destination, independent of physical register carriers. */
  ScalarType type;
  /** Independent low-width encoded word, never calculated by the test. */
  uint64_t bits;
};
constexpr auto alternate_goldens = std::to_array<AlternateGolden>({
    {"ALTSC-GOLDEN-bf16_f64_positive_zero", "0d0000000000000000",
     ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f64_negative_zero", "0d8000000000000000",
     ScalarType::BF16, 0x8000},
    {"ALTSC-GOLDEN-bf16_f64_one_point_five", "0d3ff8000000000000",
     ScalarType::BF16, 0x3fc0},
    {"ALTSC-GOLDEN-bf16_f64_negative_one_point_five", "0dbff8000000000000",
     ScalarType::BF16, 0xbfc0},
    {"ALTSC-GOLDEN-bf16_f32_positive_zero", "0f00000000", ScalarType::BF16,
     0x0000},
    {"ALTSC-GOLDEN-bf16_f32_negative_zero", "0f80000000", ScalarType::BF16,
     0x8000},
    {"ALTSC-GOLDEN-bf16_f32_one_point_five", "0f3fc00000", ScalarType::BF16,
     0x3fc0},
    {"ALTSC-GOLDEN-bf16_f32_negative_one_point_five", "0fbfc00000",
     ScalarType::BF16, 0xbfc0},
    {"ALTSC-GOLDEN-bf16_f64_binary64_minimum_subnormal", "0d0000000000000001",
     ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f64_negative_binary64_minimum_subnormal",
     "0d8000000000000001", ScalarType::BF16, 0x8000},
    {"ALTSC-GOLDEN-bf16_normal_tie_even_neighborhood_below",
     "0d3ff00fffffffffff", ScalarType::BF16, 0x3f80},
    {"ALTSC-GOLDEN-bf16_normal_tie_even_neighborhood_tie", "0d3ff0100000000000",
     ScalarType::BF16, 0x3f80},
    {"ALTSC-GOLDEN-bf16_normal_tie_even_neighborhood_above",
     "0d3ff0100000000001", ScalarType::BF16, 0x3f81},
    {"ALTSC-GOLDEN-bf16_normal_tie_odd_neighborhood_below",
     "0d3ff02fffffffffff", ScalarType::BF16, 0x3f81},
    {"ALTSC-GOLDEN-bf16_normal_tie_odd_neighborhood_tie", "0d3ff0300000000000",
     ScalarType::BF16, 0x3f82},
    {"ALTSC-GOLDEN-bf16_normal_tie_odd_neighborhood_above",
     "0d3ff0300000000001", ScalarType::BF16, 0x3f82},
    {"ALTSC-GOLDEN-bf16_f64_double_round_trap_direct", "0d3ff0100000000001",
     ScalarType::BF16, 0x3f81},
    {"ALTSC-GOLDEN-bf16_f32_double_round_intermediate", "0f3f808000",
     ScalarType::BF16, 0x3f80},
    {"ALTSC-GOLDEN-bf16_f64_minimum_subnormal", "0d37a0000000000000",
     ScalarType::BF16, 0x0001},
    {"ALTSC-GOLDEN-bf16_f64_negative_minimum_subnormal", "0db7a0000000000000",
     ScalarType::BF16, 0x8001},
    {"ALTSC-GOLDEN-bf16_f64_zero_to_min_subnormal_midpoint_below",
     "0d378fffffffffffff", ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f64_zero_to_min_subnormal_midpoint_tie",
     "0d3790000000000000", ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f64_zero_to_min_subnormal_midpoint_above",
     "0d3790000000000001", ScalarType::BF16, 0x0001},
    {"ALTSC-GOLDEN-bf16_f64_negative_zero_midpoint", "0db790000000000000",
     ScalarType::BF16, 0x8000},
    {"ALTSC-GOLDEN-bf16_f32_minimum_subnormal", "0f00010000", ScalarType::BF16,
     0x0001},
    {"ALTSC-GOLDEN-bf16_f32_zero_to_min_subnormal_midpoint_below", "0f00007fff",
     ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f32_zero_to_min_subnormal_midpoint_tie", "0f00008000",
     ScalarType::BF16, 0x0000},
    {"ALTSC-GOLDEN-bf16_f32_zero_to_min_subnormal_midpoint_above", "0f00008001",
     ScalarType::BF16, 0x0001},
    {"ALTSC-GOLDEN-bf16_f64_maximum_subnormal", "0d380fc00000000000",
     ScalarType::BF16, 0x007f},
    {"ALTSC-GOLDEN-bf16_f64_minimum_normal", "0d3810000000000000",
     ScalarType::BF16, 0x0080},
    {"ALTSC-GOLDEN-bf16_f64_subnormal_normal_transition_below",
     "0d380fdfffffffffff", ScalarType::BF16, 0x007f},
    {"ALTSC-GOLDEN-bf16_f64_subnormal_normal_transition_tie",
     "0d380fe00000000000", ScalarType::BF16, 0x0080},
    {"ALTSC-GOLDEN-bf16_f64_subnormal_normal_transition_above",
     "0d380fe00000000001", ScalarType::BF16, 0x0080},
    {"ALTSC-GOLDEN-bf16_f64_maximum_finite", "0d47efe00000000000",
     ScalarType::BF16, 0x7f7f},
    {"ALTSC-GOLDEN-bf16_f64_negative_maximum_finite", "0dc7efe00000000000",
     ScalarType::BF16, 0xff7f},
    {"ALTSC-GOLDEN-bf16_f64_terminal_boundary_below", "0d47efefffffffffff",
     ScalarType::BF16, 0x7f7f},
    {"ALTSC-GOLDEN-bf16_f64_terminal_boundary_tie", "0d47eff00000000000",
     ScalarType::BF16, 0x7f80},
    {"ALTSC-GOLDEN-bf16_f64_terminal_boundary_above", "0d47eff00000000001",
     ScalarType::BF16, 0x7f80},
    {"ALTSC-GOLDEN-bf16_f64_negative_terminal_boundary", "0dc7eff00000000000",
     ScalarType::BF16, 0xff80},
    {"ALTSC-GOLDEN-bf16_f32_terminal_boundary", "0f7f7f8000", ScalarType::BF16,
     0x7f80},
    {"ALTSC-GOLDEN-bf16_f32_terminal_boundary_below", "0f7f7f7fff",
     ScalarType::BF16, 0x7f7f},
    {"ALTSC-GOLDEN-bf16_f32_terminal_boundary_above", "0f7f7f8001",
     ScalarType::BF16, 0x7f80},
    {"ALTSC-GOLDEN-bf16_f64_positive_infinity", "0d7ff0000000000000",
     ScalarType::BF16, 0x7f80},
    {"ALTSC-GOLDEN-bf16_f64_negative_infinity", "0dfff0000000000000",
     ScalarType::BF16, 0xff80},
    {"ALTSC-GOLDEN-bf16_f64_positive_signaling_low_payload_nan",
     "0d7ff0000000000001", ScalarType::BF16, 0x7fc0},
    {"ALTSC-GOLDEN-bf16_f64_negative_signaling_low_payload_nan",
     "0dfff0000000000001", ScalarType::BF16, 0xffc0},
    {"ALTSC-GOLDEN-bf16_f64_positive_signaling_high_payload_nan",
     "0d7ff7fe0000000000", ScalarType::BF16, 0x7fff},
    {"ALTSC-GOLDEN-bf16_f64_negative_signaling_high_payload_nan",
     "0dfff7fe0000000000", ScalarType::BF16, 0xffff},
    {"ALTSC-GOLDEN-bf16_f64_positive_quiet_high_payload_nan",
     "0d7fffffffffffffff", ScalarType::BF16, 0x7fff},
    {"ALTSC-GOLDEN-bf16_f64_negative_quiet_high_payload_nan",
     "0dffffffffffffffff", ScalarType::BF16, 0xffff},
    {"ALTSC-GOLDEN-bf16_f32_positive_infinity", "0f7f800000", ScalarType::BF16,
     0x7f80},
    {"ALTSC-GOLDEN-bf16_f32_negative_infinity", "0fff800000", ScalarType::BF16,
     0xff80},
    {"ALTSC-GOLDEN-bf16_f32_positive_signaling_low_payload_nan", "0f7f800001",
     ScalarType::BF16, 0x7fc0},
    {"ALTSC-GOLDEN-bf16_f32_negative_signaling_low_payload_nan", "0fff800001",
     ScalarType::BF16, 0xffc0},
    {"ALTSC-GOLDEN-bf16_f32_positive_signaling_high_payload_nan", "0f7fb00000",
     ScalarType::BF16, 0x7ff0},
    {"ALTSC-GOLDEN-bf16_f32_negative_signaling_high_payload_nan", "0fffb00000",
     ScalarType::BF16, 0xfff0},
    {"ALTSC-GOLDEN-bf16_f32_positive_quiet_high_payload_nan", "0f7fffffff",
     ScalarType::BF16, 0x7fff},
    {"ALTSC-GOLDEN-bf16_f32_negative_quiet_high_payload_nan", "0fffffffff",
     ScalarType::BF16, 0xffff},
    {"ALTSC-GOLDEN-e4m3_f64_positive_zero", "0d0000000000000000",
     ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f64_negative_zero", "0d8000000000000000",
     ScalarType::E4m3, 0x80},
    {"ALTSC-GOLDEN-e4m3_f64_one_point_five", "0d3ff8000000000000",
     ScalarType::E4m3, 0x3c},
    {"ALTSC-GOLDEN-e4m3_f64_negative_one_point_five", "0dbff8000000000000",
     ScalarType::E4m3, 0xbc},
    {"ALTSC-GOLDEN-e4m3_f32_positive_zero", "0f00000000", ScalarType::E4m3,
     0x00},
    {"ALTSC-GOLDEN-e4m3_f32_negative_zero", "0f80000000", ScalarType::E4m3,
     0x80},
    {"ALTSC-GOLDEN-e4m3_f32_one_point_five", "0f3fc00000", ScalarType::E4m3,
     0x3c},
    {"ALTSC-GOLDEN-e4m3_f32_negative_one_point_five", "0fbfc00000",
     ScalarType::E4m3, 0xbc},
    {"ALTSC-GOLDEN-e4m3_f64_binary64_minimum_subnormal", "0d0000000000000001",
     ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f64_negative_binary64_minimum_subnormal",
     "0d8000000000000001", ScalarType::E4m3, 0x80},
    {"ALTSC-GOLDEN-e4m3_normal_tie_even_neighborhood_below",
     "0d3ff0ffffffffffff", ScalarType::E4m3, 0x38},
    {"ALTSC-GOLDEN-e4m3_normal_tie_even_neighborhood_tie", "0d3ff1000000000000",
     ScalarType::E4m3, 0x38},
    {"ALTSC-GOLDEN-e4m3_normal_tie_even_neighborhood_above",
     "0d3ff1000000000001", ScalarType::E4m3, 0x39},
    {"ALTSC-GOLDEN-e4m3_normal_tie_odd_neighborhood_below",
     "0d3ff2ffffffffffff", ScalarType::E4m3, 0x39},
    {"ALTSC-GOLDEN-e4m3_normal_tie_odd_neighborhood_tie", "0d3ff3000000000000",
     ScalarType::E4m3, 0x3a},
    {"ALTSC-GOLDEN-e4m3_normal_tie_odd_neighborhood_above",
     "0d3ff3000000000001", ScalarType::E4m3, 0x3a},
    {"ALTSC-GOLDEN-e4m3_f64_double_round_trap_direct", "0d3ff1000000000001",
     ScalarType::E4m3, 0x39},
    {"ALTSC-GOLDEN-e4m3_f32_double_round_intermediate", "0f3f880000",
     ScalarType::E4m3, 0x38},
    {"ALTSC-GOLDEN-e4m3_f64_minimum_subnormal", "0d3f60000000000000",
     ScalarType::E4m3, 0x01},
    {"ALTSC-GOLDEN-e4m3_f64_negative_minimum_subnormal", "0dbf60000000000000",
     ScalarType::E4m3, 0x81},
    {"ALTSC-GOLDEN-e4m3_f64_zero_to_min_subnormal_midpoint_below",
     "0d3f4fffffffffffff", ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f64_zero_to_min_subnormal_midpoint_tie",
     "0d3f50000000000000", ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f64_zero_to_min_subnormal_midpoint_above",
     "0d3f50000000000001", ScalarType::E4m3, 0x01},
    {"ALTSC-GOLDEN-e4m3_f64_negative_zero_midpoint", "0dbf50000000000000",
     ScalarType::E4m3, 0x80},
    {"ALTSC-GOLDEN-e4m3_f32_minimum_subnormal", "0f3b000000", ScalarType::E4m3,
     0x01},
    {"ALTSC-GOLDEN-e4m3_f32_zero_to_min_subnormal_midpoint_below", "0f3a7fffff",
     ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f32_zero_to_min_subnormal_midpoint_tie", "0f3a800000",
     ScalarType::E4m3, 0x00},
    {"ALTSC-GOLDEN-e4m3_f32_zero_to_min_subnormal_midpoint_above", "0f3a800001",
     ScalarType::E4m3, 0x01},
    {"ALTSC-GOLDEN-e4m3_f64_maximum_subnormal", "0d3f8c000000000000",
     ScalarType::E4m3, 0x07},
    {"ALTSC-GOLDEN-e4m3_f64_minimum_normal", "0d3f90000000000000",
     ScalarType::E4m3, 0x08},
    {"ALTSC-GOLDEN-e4m3_f64_subnormal_normal_transition_below",
     "0d3f8dffffffffffff", ScalarType::E4m3, 0x07},
    {"ALTSC-GOLDEN-e4m3_f64_subnormal_normal_transition_tie",
     "0d3f8e000000000000", ScalarType::E4m3, 0x08},
    {"ALTSC-GOLDEN-e4m3_f64_subnormal_normal_transition_above",
     "0d3f8e000000000001", ScalarType::E4m3, 0x08},
    {"ALTSC-GOLDEN-e4m3_f64_maximum_finite", "0d407c000000000000",
     ScalarType::E4m3, 0x7e},
    {"ALTSC-GOLDEN-e4m3_f64_negative_maximum_finite", "0dc07c000000000000",
     ScalarType::E4m3, 0xfe},
    {"ALTSC-GOLDEN-e4m3_f64_terminal_boundary_below", "0d407cffffffffffff",
     ScalarType::E4m3, 0x7e},
    {"ALTSC-GOLDEN-e4m3_f64_terminal_boundary_tie", "0d407d000000000000",
     ScalarType::E4m3, 0x7e},
    {"ALTSC-GOLDEN-e4m3_f64_terminal_boundary_above", "0d407d000000000001",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_negative_terminal_boundary", "0dc07d000000000000",
     ScalarType::E4m3, 0xfe},
    {"ALTSC-GOLDEN-e4m3_f32_terminal_boundary", "0f43e80000", ScalarType::E4m3,
     0x7e},
    {"ALTSC-GOLDEN-e4m3_f32_terminal_boundary_below", "0f43e7ffff",
     ScalarType::E4m3, 0x7e},
    {"ALTSC-GOLDEN-e4m3_f32_terminal_boundary_above", "0f43e80001",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_finite_above_terminal_boundary",
     "0d407e000000000000", ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_positive_infinity", "0d7ff0000000000000",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_negative_infinity", "0dfff0000000000000",
     ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f64_positive_signaling_low_payload_nan",
     "0d7ff0000000000001", ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_negative_signaling_low_payload_nan",
     "0dfff0000000000001", ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f64_positive_signaling_high_payload_nan",
     "0d7ff7fe0000000000", ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_negative_signaling_high_payload_nan",
     "0dfff7fe0000000000", ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f64_positive_quiet_high_payload_nan",
     "0d7fffffffffffffff", ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f64_negative_quiet_high_payload_nan",
     "0dffffffffffffffff", ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f32_positive_infinity", "0f7f800000", ScalarType::E4m3,
     0x7f},
    {"ALTSC-GOLDEN-e4m3_f32_negative_infinity", "0fff800000", ScalarType::E4m3,
     0xff},
    {"ALTSC-GOLDEN-e4m3_f32_positive_signaling_low_payload_nan", "0f7f800001",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f32_negative_signaling_low_payload_nan", "0fff800001",
     ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f32_positive_signaling_high_payload_nan", "0f7fb00000",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f32_negative_signaling_high_payload_nan", "0fffb00000",
     ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e4m3_f32_positive_quiet_high_payload_nan", "0f7fffffff",
     ScalarType::E4m3, 0x7f},
    {"ALTSC-GOLDEN-e4m3_f32_negative_quiet_high_payload_nan", "0fffffffff",
     ScalarType::E4m3, 0xff},
    {"ALTSC-GOLDEN-e5m2_f64_positive_zero", "0d0000000000000000",
     ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f64_negative_zero", "0d8000000000000000",
     ScalarType::E5m2, 0x80},
    {"ALTSC-GOLDEN-e5m2_f64_one_point_five", "0d3ff8000000000000",
     ScalarType::E5m2, 0x3e},
    {"ALTSC-GOLDEN-e5m2_f64_negative_one_point_five", "0dbff8000000000000",
     ScalarType::E5m2, 0xbe},
    {"ALTSC-GOLDEN-e5m2_f32_positive_zero", "0f00000000", ScalarType::E5m2,
     0x00},
    {"ALTSC-GOLDEN-e5m2_f32_negative_zero", "0f80000000", ScalarType::E5m2,
     0x80},
    {"ALTSC-GOLDEN-e5m2_f32_one_point_five", "0f3fc00000", ScalarType::E5m2,
     0x3e},
    {"ALTSC-GOLDEN-e5m2_f32_negative_one_point_five", "0fbfc00000",
     ScalarType::E5m2, 0xbe},
    {"ALTSC-GOLDEN-e5m2_f64_binary64_minimum_subnormal", "0d0000000000000001",
     ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f64_negative_binary64_minimum_subnormal",
     "0d8000000000000001", ScalarType::E5m2, 0x80},
    {"ALTSC-GOLDEN-e5m2_normal_tie_even_neighborhood_below",
     "0d3ff1ffffffffffff", ScalarType::E5m2, 0x3c},
    {"ALTSC-GOLDEN-e5m2_normal_tie_even_neighborhood_tie", "0d3ff2000000000000",
     ScalarType::E5m2, 0x3c},
    {"ALTSC-GOLDEN-e5m2_normal_tie_even_neighborhood_above",
     "0d3ff2000000000001", ScalarType::E5m2, 0x3d},
    {"ALTSC-GOLDEN-e5m2_normal_tie_odd_neighborhood_below",
     "0d3ff5ffffffffffff", ScalarType::E5m2, 0x3d},
    {"ALTSC-GOLDEN-e5m2_normal_tie_odd_neighborhood_tie", "0d3ff6000000000000",
     ScalarType::E5m2, 0x3e},
    {"ALTSC-GOLDEN-e5m2_normal_tie_odd_neighborhood_above",
     "0d3ff6000000000001", ScalarType::E5m2, 0x3e},
    {"ALTSC-GOLDEN-e5m2_f64_double_round_trap_direct", "0d3ff2000000000001",
     ScalarType::E5m2, 0x3d},
    {"ALTSC-GOLDEN-e5m2_f32_double_round_intermediate", "0f3f900000",
     ScalarType::E5m2, 0x3c},
    {"ALTSC-GOLDEN-e5m2_f64_minimum_subnormal", "0d3ef0000000000000",
     ScalarType::E5m2, 0x01},
    {"ALTSC-GOLDEN-e5m2_f64_negative_minimum_subnormal", "0dbef0000000000000",
     ScalarType::E5m2, 0x81},
    {"ALTSC-GOLDEN-e5m2_f64_zero_to_min_subnormal_midpoint_below",
     "0d3edfffffffffffff", ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f64_zero_to_min_subnormal_midpoint_tie",
     "0d3ee0000000000000", ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f64_zero_to_min_subnormal_midpoint_above",
     "0d3ee0000000000001", ScalarType::E5m2, 0x01},
    {"ALTSC-GOLDEN-e5m2_f64_negative_zero_midpoint", "0dbee0000000000000",
     ScalarType::E5m2, 0x80},
    {"ALTSC-GOLDEN-e5m2_f32_minimum_subnormal", "0f37800000", ScalarType::E5m2,
     0x01},
    {"ALTSC-GOLDEN-e5m2_f32_zero_to_min_subnormal_midpoint_below", "0f36ffffff",
     ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f32_zero_to_min_subnormal_midpoint_tie", "0f37000000",
     ScalarType::E5m2, 0x00},
    {"ALTSC-GOLDEN-e5m2_f32_zero_to_min_subnormal_midpoint_above", "0f37000001",
     ScalarType::E5m2, 0x01},
    {"ALTSC-GOLDEN-e5m2_f64_maximum_subnormal", "0d3f08000000000000",
     ScalarType::E5m2, 0x03},
    {"ALTSC-GOLDEN-e5m2_f64_minimum_normal", "0d3f10000000000000",
     ScalarType::E5m2, 0x04},
    {"ALTSC-GOLDEN-e5m2_f64_subnormal_normal_transition_below",
     "0d3f0bffffffffffff", ScalarType::E5m2, 0x03},
    {"ALTSC-GOLDEN-e5m2_f64_subnormal_normal_transition_tie",
     "0d3f0c000000000000", ScalarType::E5m2, 0x04},
    {"ALTSC-GOLDEN-e5m2_f64_subnormal_normal_transition_above",
     "0d3f0c000000000001", ScalarType::E5m2, 0x04},
    {"ALTSC-GOLDEN-e5m2_f64_maximum_finite", "0d40ec000000000000",
     ScalarType::E5m2, 0x7b},
    {"ALTSC-GOLDEN-e5m2_f64_negative_maximum_finite", "0dc0ec000000000000",
     ScalarType::E5m2, 0xfb},
    {"ALTSC-GOLDEN-e5m2_f64_terminal_boundary_below", "0d40edffffffffffff",
     ScalarType::E5m2, 0x7b},
    {"ALTSC-GOLDEN-e5m2_f64_terminal_boundary_tie", "0d40ee000000000000",
     ScalarType::E5m2, 0x7c},
    {"ALTSC-GOLDEN-e5m2_f64_terminal_boundary_above", "0d40ee000000000001",
     ScalarType::E5m2, 0x7c},
    {"ALTSC-GOLDEN-e5m2_f64_negative_terminal_boundary", "0dc0ee000000000000",
     ScalarType::E5m2, 0xfc},
    {"ALTSC-GOLDEN-e5m2_f32_terminal_boundary", "0f47700000", ScalarType::E5m2,
     0x7c},
    {"ALTSC-GOLDEN-e5m2_f32_terminal_boundary_below", "0f476fffff",
     ScalarType::E5m2, 0x7b},
    {"ALTSC-GOLDEN-e5m2_f32_terminal_boundary_above", "0f47700001",
     ScalarType::E5m2, 0x7c},
    {"ALTSC-GOLDEN-e5m2_f64_positive_infinity", "0d7ff0000000000000",
     ScalarType::E5m2, 0x7c},
    {"ALTSC-GOLDEN-e5m2_f64_negative_infinity", "0dfff0000000000000",
     ScalarType::E5m2, 0xfc},
    {"ALTSC-GOLDEN-e5m2_f64_positive_signaling_low_payload_nan",
     "0d7ff0000000000001", ScalarType::E5m2, 0x7e},
    {"ALTSC-GOLDEN-e5m2_f64_negative_signaling_low_payload_nan",
     "0dfff0000000000001", ScalarType::E5m2, 0xfe},
    {"ALTSC-GOLDEN-e5m2_f64_positive_signaling_high_payload_nan",
     "0d7ff7fe0000000000", ScalarType::E5m2, 0x7f},
    {"ALTSC-GOLDEN-e5m2_f64_negative_signaling_high_payload_nan",
     "0dfff7fe0000000000", ScalarType::E5m2, 0xff},
    {"ALTSC-GOLDEN-e5m2_f64_positive_quiet_high_payload_nan",
     "0d7fffffffffffffff", ScalarType::E5m2, 0x7f},
    {"ALTSC-GOLDEN-e5m2_f64_negative_quiet_high_payload_nan",
     "0dffffffffffffffff", ScalarType::E5m2, 0xff},
    {"ALTSC-GOLDEN-e5m2_f32_positive_infinity", "0f7f800000", ScalarType::E5m2,
     0x7c},
    {"ALTSC-GOLDEN-e5m2_f32_negative_infinity", "0fff800000", ScalarType::E5m2,
     0xfc},
    {"ALTSC-GOLDEN-e5m2_f32_positive_signaling_low_payload_nan", "0f7f800001",
     ScalarType::E5m2, 0x7e},
    {"ALTSC-GOLDEN-e5m2_f32_negative_signaling_low_payload_nan", "0fff800001",
     ScalarType::E5m2, 0xfe},
    {"ALTSC-GOLDEN-e5m2_f32_positive_signaling_high_payload_nan", "0f7fb00000",
     ScalarType::E5m2, 0x7f},
    {"ALTSC-GOLDEN-e5m2_f32_negative_signaling_high_payload_nan", "0fffb00000",
     ScalarType::E5m2, 0xff},
    {"ALTSC-GOLDEN-e5m2_f32_positive_quiet_high_payload_nan", "0f7fffffff",
     ScalarType::E5m2, 0x7f},
    {"ALTSC-GOLDEN-e5m2_f32_negative_quiet_high_payload_nan", "0fffffffff",
     ScalarType::E5m2, 0xff},
});
/** Preserve the complete modern target profile for generic and instruction checks. */
checker::Context alternate_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Join a successful recipe to the externally fixed evidence identity. */
void record_alternate(std::string_view id) {
  if (!::testing::Test::HasFailure())
    std::cout << "ALTSC " << id << " PASS\n";
}
/** Convert an owning lexical leaf through the existing exported helper. */
auto alternate_literal(std::string_view text, ScalarType type) {
  auto kind = base::AstImmediateKind::DecimalFloat;
  if (text.starts_with("0d"))
    kind = base::AstImmediateKind::F64Hex;
  if (text.starts_with("0f"))
    kind = base::AstImmediateKind::F32Hex;
  syntax_ast::AstImmediate leaf{
      .syntax = {.text = std::string(text), .range = {{3, 4}, {3, 24}}},
      .kind = kind};
  return resolve_immediate_literal(leaf, type);
}
/** Resolve the same parsed numeric operand without inventing an opcode permission. */
auto alternate_expression(std::string_view text, ScalarType type) {
  const std::string source = "mov.b64 d," + std::string(text) + ";";
  const auto ast = test_helpers::parseInstruction(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  return detail::resolve_numeric_operand(ast->operands.at(1), type);
}
/** Check the current typed word using real common-checker descriptor and view inputs. */
checker::CheckResult alternate_word(
    ScalarType type, uint64_t bits,
    std::optional<uint64_t> origin = std::nullopt, bool negative = false) {
  const checker::OperandDescriptor descriptor{
      .target_field_id = "src",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = type},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Immediate};
  const SourceRange range{{3, 4}, {3, 24}};
  const checker::OperandView operand{
      .field_id = "src",
      .actual_shape = checker::OperandShape::Immediate,
      .immediate_type = type,
      .immediate_bits = bits,
      .immediate_is_negative = negative,
      .locations = {&range, 1},
      .integer_source_bits = origin};
  return checker::check_operands({&descriptor, 1}, {}, {&operand, 1}, {},
                                 alternate_context());
}
TEST(AlternateScalarImmediates, MatchesIndependentScalarGoldens) {
  ASSERT_EQ(alternate_goldens.size(), 175u);
  for (const auto& item : alternate_goldens) {
    SCOPED_TRACE(item.id);
    const auto result = alternate_literal(item.source, item.type);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->bits, item.bits);
    EXPECT_EQ(result->type, item.type);
    EXPECT_FALSE(result->integer_source_bits);
    EXPECT_FALSE(result->is_negative);
    record_alternate(item.id);
  }
}
TEST(AlternateScalarImmediates, PreservesNumericRoutesAndIntegerOrigins) {
  {
    SCOPED_TRACE("ALTSC-DECIMAL-bf16-0.0");
    const auto value = alternate_literal("0.0", ScalarType::BF16);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x0000);
    record_alternate("ALTSC-DECIMAL-bf16-0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-bf16--0.0");
    const auto value = alternate_literal("-0.0", ScalarType::BF16);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x8000);
    record_alternate("ALTSC-DECIMAL-bf16--0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-bf16-1.5");
    const auto value = alternate_literal("1.5", ScalarType::BF16);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3fc0);
    record_alternate("ALTSC-DECIMAL-bf16-1.5");
  }
  {
    const auto value = alternate_expression("1.0+0.5", ScalarType::BF16);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3fc0);
    record_alternate("ALTSC-NUMERIC-bf16");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-0");
    EXPECT_FALSE(alternate_expression("0", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-1");
    EXPECT_FALSE(alternate_expression("1", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-0x3fc0");
    EXPECT_FALSE(alternate_expression("0x3fc0", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-0x3fc0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16--1");
    EXPECT_FALSE(alternate_expression("-1", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16--1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-1U");
    EXPECT_FALSE(alternate_expression("1U", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-1U");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-(1+2)");
    EXPECT_FALSE(alternate_expression("(1+2)", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-(1+2)");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-bf16-(1.0<2.0)");
    EXPECT_FALSE(alternate_expression("(1.0<2.0)", ScalarType::BF16));
    record_alternate("ALTSC-INTEGER-bf16-(1.0<2.0)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-bf16-+0f3fc00000");
    EXPECT_FALSE(alternate_expression("+0f3fc00000", ScalarType::BF16));
    record_alternate("ALTSC-F32-RESTRICTION-bf16-+0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-bf16--0f3fc00000");
    EXPECT_FALSE(alternate_expression("-0f3fc00000", ScalarType::BF16));
    record_alternate("ALTSC-F32-RESTRICTION-bf16--0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-bf16-(0f3fc00000)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000)", ScalarType::BF16));
    record_alternate("ALTSC-F32-RESTRICTION-bf16-(0f3fc00000)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-bf16-(0f3fc00000+0)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000+0)", ScalarType::BF16));
    record_alternate("ALTSC-F32-RESTRICTION-bf16-(0f3fc00000+0)");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e4m3-0.0");
    const auto value = alternate_literal("0.0", ScalarType::E4m3);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x00);
    record_alternate("ALTSC-DECIMAL-e4m3-0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e4m3--0.0");
    const auto value = alternate_literal("-0.0", ScalarType::E4m3);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x80);
    record_alternate("ALTSC-DECIMAL-e4m3--0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e4m3-1.5");
    const auto value = alternate_literal("1.5", ScalarType::E4m3);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3c);
    record_alternate("ALTSC-DECIMAL-e4m3-1.5");
  }
  {
    const auto value = alternate_expression("1.0+0.5", ScalarType::E4m3);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3c);
    record_alternate("ALTSC-NUMERIC-e4m3");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-0");
    EXPECT_FALSE(alternate_expression("0", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-1");
    EXPECT_FALSE(alternate_expression("1", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-0x3fc0");
    EXPECT_FALSE(alternate_expression("0x3fc0", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-0x3fc0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3--1");
    EXPECT_FALSE(alternate_expression("-1", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3--1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-1U");
    EXPECT_FALSE(alternate_expression("1U", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-1U");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-(1+2)");
    EXPECT_FALSE(alternate_expression("(1+2)", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-(1+2)");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e4m3-(1.0<2.0)");
    EXPECT_FALSE(alternate_expression("(1.0<2.0)", ScalarType::E4m3));
    record_alternate("ALTSC-INTEGER-e4m3-(1.0<2.0)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e4m3-+0f3fc00000");
    EXPECT_FALSE(alternate_expression("+0f3fc00000", ScalarType::E4m3));
    record_alternate("ALTSC-F32-RESTRICTION-e4m3-+0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e4m3--0f3fc00000");
    EXPECT_FALSE(alternate_expression("-0f3fc00000", ScalarType::E4m3));
    record_alternate("ALTSC-F32-RESTRICTION-e4m3--0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e4m3-(0f3fc00000)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000)", ScalarType::E4m3));
    record_alternate("ALTSC-F32-RESTRICTION-e4m3-(0f3fc00000)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e4m3-(0f3fc00000+0)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000+0)", ScalarType::E4m3));
    record_alternate("ALTSC-F32-RESTRICTION-e4m3-(0f3fc00000+0)");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e5m2-0.0");
    const auto value = alternate_literal("0.0", ScalarType::E5m2);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x00);
    record_alternate("ALTSC-DECIMAL-e5m2-0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e5m2--0.0");
    const auto value = alternate_literal("-0.0", ScalarType::E5m2);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x80);
    record_alternate("ALTSC-DECIMAL-e5m2--0.0");
  }
  {
    SCOPED_TRACE("ALTSC-DECIMAL-e5m2-1.5");
    const auto value = alternate_literal("1.5", ScalarType::E5m2);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3e);
    record_alternate("ALTSC-DECIMAL-e5m2-1.5");
  }
  {
    const auto value = alternate_expression("1.0+0.5", ScalarType::E5m2);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3e);
    record_alternate("ALTSC-NUMERIC-e5m2");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-0");
    EXPECT_FALSE(alternate_expression("0", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-1");
    EXPECT_FALSE(alternate_expression("1", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-0x3fc0");
    EXPECT_FALSE(alternate_expression("0x3fc0", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-0x3fc0");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2--1");
    EXPECT_FALSE(alternate_expression("-1", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2--1");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-1U");
    EXPECT_FALSE(alternate_expression("1U", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-1U");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-(1+2)");
    EXPECT_FALSE(alternate_expression("(1+2)", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-(1+2)");
  }
  {
    SCOPED_TRACE("ALTSC-INTEGER-e5m2-(1.0<2.0)");
    EXPECT_FALSE(alternate_expression("(1.0<2.0)", ScalarType::E5m2));
    record_alternate("ALTSC-INTEGER-e5m2-(1.0<2.0)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e5m2-+0f3fc00000");
    EXPECT_FALSE(alternate_expression("+0f3fc00000", ScalarType::E5m2));
    record_alternate("ALTSC-F32-RESTRICTION-e5m2-+0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e5m2--0f3fc00000");
    EXPECT_FALSE(alternate_expression("-0f3fc00000", ScalarType::E5m2));
    record_alternate("ALTSC-F32-RESTRICTION-e5m2--0f3fc00000");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e5m2-(0f3fc00000)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000)", ScalarType::E5m2));
    record_alternate("ALTSC-F32-RESTRICTION-e5m2-(0f3fc00000)");
  }
  {
    SCOPED_TRACE("ALTSC-F32-RESTRICTION-e5m2-(0f3fc00000+0)");
    EXPECT_FALSE(alternate_expression("(0f3fc00000+0)", ScalarType::E5m2));
    record_alternate("ALTSC-F32-RESTRICTION-e5m2-(0f3fc00000+0)");
  }
}
TEST(AlternateScalarImmediates,
     AcceptsAllCurrentWordsAndRejectsInvalidMetadata) {
  for (uint64_t bits = 0; bits < 65536; ++bits) {
    ASSERT_TRUE(alternate_word(ScalarType::BF16, bits)) << bits;
  }
  record_alternate("ALTSC-WORD-ALL-bf16");
  {
    const auto result = alternate_word(ScalarType::BF16, 0x10000);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-bf16-highbits");
  }
  {
    const auto result = alternate_word(ScalarType::BF16, 0x3c, 0);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-bf16-origin-zero");
  }
  {
    const auto result = alternate_word(ScalarType::BF16, 0x3c, 0x3c);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-bf16-origin-equal");
  }
  {
    const auto result =
        alternate_word(ScalarType::BF16, 0x3c, std::nullopt, true);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-bf16-negative");
  }
  EXPECT_TRUE(alternate_word(ScalarType::BF16, 0x7f81));
  record_alternate("ALTSC-WORD-SNAN-bf16");
  for (uint64_t bits = 0; bits < 256; ++bits) {
    ASSERT_TRUE(alternate_word(ScalarType::E4m3, bits)) << bits;
  }
  record_alternate("ALTSC-WORD-ALL-e4m3");
  {
    const auto result = alternate_word(ScalarType::E4m3, 0x100);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e4m3-highbits");
  }
  {
    const auto result = alternate_word(ScalarType::E4m3, 0x3c, 0);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e4m3-origin-zero");
  }
  {
    const auto result = alternate_word(ScalarType::E4m3, 0x3c, 0x3c);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e4m3-origin-equal");
  }
  {
    const auto result =
        alternate_word(ScalarType::E4m3, 0x3c, std::nullopt, true);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e4m3-negative");
  }
  EXPECT_TRUE(alternate_word(ScalarType::E4m3, 0x7f));
  record_alternate("ALTSC-WORD-SNAN-e4m3");
  for (uint64_t bits = 0; bits < 256; ++bits) {
    ASSERT_TRUE(alternate_word(ScalarType::E5m2, bits)) << bits;
  }
  record_alternate("ALTSC-WORD-ALL-e5m2");
  {
    const auto result = alternate_word(ScalarType::E5m2, 0x100);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e5m2-highbits");
  }
  {
    const auto result = alternate_word(ScalarType::E5m2, 0x3c, 0);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e5m2-origin-zero");
  }
  {
    const auto result = alternate_word(ScalarType::E5m2, 0x3c, 0x3c);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e5m2-origin-equal");
  }
  {
    const auto result =
        alternate_word(ScalarType::E5m2, 0x3c, std::nullopt, true);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(result.error().front().range, (SourceRange{{3, 4}, {3, 24}}));
    record_alternate("ALTSC-WORD-NEG-e5m2-negative");
  }
  EXPECT_TRUE(alternate_word(ScalarType::E5m2, 0x7d));
  record_alternate("ALTSC-WORD-SNAN-e5m2");
}
TEST(AlternateScalarImmediates, OwnsReturnedValuesAfterLexicalLifetime) {
  {
    const auto value = [] {
      return alternate_literal(std::string("1.5"), ScalarType::BF16);
    }();
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3fc0);
    EXPECT_EQ(value->type, ScalarType::BF16);
    ResolvedImmediate copied(*value), assigned;
    assigned = copied;
    ResolvedImmediate moved(std::move(copied));
    for (const auto* current : {&assigned, &moved}) {
      EXPECT_EQ(current->type, value->type);
      EXPECT_EQ(current->bits, value->bits);
      EXPECT_FALSE(current->integer_source_bits);
      EXPECT_FALSE(current->is_negative);
      EXPECT_TRUE(alternate_word(current->type, current->bits));
    }
    record_alternate("ALTSC-COPY-VALUE-bf16");
  }
  {
    const auto value = [] {
      return alternate_literal(std::string("1.5"), ScalarType::E4m3);
    }();
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3c);
    EXPECT_EQ(value->type, ScalarType::E4m3);
    ResolvedImmediate copied(*value), assigned;
    assigned = copied;
    ResolvedImmediate moved(std::move(copied));
    for (const auto* current : {&assigned, &moved}) {
      EXPECT_EQ(current->type, value->type);
      EXPECT_EQ(current->bits, value->bits);
      EXPECT_FALSE(current->integer_source_bits);
      EXPECT_FALSE(current->is_negative);
      EXPECT_TRUE(alternate_word(current->type, current->bits));
    }
    record_alternate("ALTSC-COPY-VALUE-e4m3");
  }
  {
    const auto value = [] {
      return alternate_literal(std::string("1.5"), ScalarType::E5m2);
    }();
    ASSERT_TRUE(value);
    EXPECT_EQ(value->bits, 0x3e);
    EXPECT_EQ(value->type, ScalarType::E5m2);
    ResolvedImmediate copied(*value), assigned;
    assigned = copied;
    ResolvedImmediate moved(std::move(copied));
    for (const auto* current : {&assigned, &moved}) {
      EXPECT_EQ(current->type, value->type);
      EXPECT_EQ(current->bits, value->bits);
      EXPECT_FALSE(current->integer_source_bits);
      EXPECT_FALSE(current->is_negative);
      EXPECT_TRUE(alternate_word(current->type, current->bits));
    }
    record_alternate("ALTSC-COPY-VALUE-e5m2");
  }
}
/** Build a complete source profile with independent logical and physical types. */
std::string alternate_module(std::string_view instruction) {
  return ".version 9.3\n.target sm_100a\n.entry kernel(){"
         ".reg .b16 b; .reg .f16 h; .reg .f32 f0,f1; .reg .s32 s;"
         ".reg .u64 w; .reg .b32 u; .reg .f64 d; .reg .pred p,q;" +
         std::string(instruction) + "}";
}
/** Return strict owned IR after parser, text and AST have all died. */
auto alternate_owned(std::string instruction) {
  const auto source = alternate_module(instruction);
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  return resolveAndValidateModule(*ast);
}
/** Access only the five selected already-admitted concrete BF16 source fields. */
ResolvedImmediate& alternate_bf16_word(Instruction& instruction) {
  if (auto* add = dynamic_cast<AddMixedF32*>(&instruction))
    return std::get<ResolvedImmediate>(add->src.value);
  if (auto* sub = dynamic_cast<SubMixedF32*>(&instruction))
    return std::get<ResolvedImmediate>(sub->src.value);
  if (auto* cvt = dynamic_cast<CvtF32Bf16*>(&instruction))
    return std::get<ResolvedImmediate>(cvt->src.value);
  if (auto* cvt = dynamic_cast<CvtF32Bf16Ftz*>(&instruction))
    return std::get<ResolvedImmediate>(cvt->src.value);
  return std::get<ResolvedImmediate>(
      dynamic_cast<CvtNonF32BaseFromBf16&>(instruction).src.value);
}
/** Validate independent module/function copies, moves, assignments and instruction clones. */
void expect_alternate_owned(const ResolvedModule& original) {
  EXPECT_TRUE(validateModule(original));
  ResolvedModule copied(original), assigned;
  assigned = copied;
  ResolvedModule moved(std::move(copied));
  EXPECT_TRUE(validateModule(moved));
  ResolvedFunction function(assigned.functions.front()), function_assigned;
  function_assigned = function;
  assigned.functions.front() = std::move(function_assigned);
  assigned.functions.front().body.front() = function.body.front()->clone();
  EXPECT_TRUE(validateModule(assigned));
}
TEST(AlternateScalarImmediates, ConvertsFiveActualFieldsAfterRegisterControls) {
  {
    SCOPED_TRACE("ALTSC-SOURCE-add.add_mixed_f32.default.src-decimal");
    const auto control = alternate_owned("add.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("add.f32.bf16 f0,1.5,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("add.f32.bf16 f0,1.5,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-add.add_mixed_f32.default.src-decimal");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-add.add_mixed_f32.default.src-expression");
    const auto control = alternate_owned("add.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("add.f32.bf16 f0,(1.0+0.5),f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("add.f32.bf16 f0,(1.0+0.5),f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-add.add_mixed_f32.default.src-expression");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-add.add_mixed_f32.default.src-f64");
    const auto control = alternate_owned("add.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual =
        alternate_owned("add.f32.bf16 f0,0d3ff8000000000000,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text =
        alternate_module("add.f32.bf16 f0,0d3ff8000000000000,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-add.add_mixed_f32.default.src-f64");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-add.add_mixed_f32.default.src-f32");
    const auto control = alternate_owned("add.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("add.f32.bf16 f0,0f3fc00000,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("add.f32.bf16 f0,0f3fc00000,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-add.add_mixed_f32.default.src-f32");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-decimal");
    const auto control = alternate_owned("sub.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("sub.f32.bf16 f0,1.5,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("sub.f32.bf16 f0,1.5,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-decimal");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-expression");
    const auto control = alternate_owned("sub.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("sub.f32.bf16 f0,(1.0+0.5),f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("sub.f32.bf16 f0,(1.0+0.5),f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-expression");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-f64");
    const auto control = alternate_owned("sub.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual =
        alternate_owned("sub.f32.bf16 f0,0d3ff8000000000000,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text =
        alternate_module("sub.f32.bf16 f0,0d3ff8000000000000,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-f64");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-f32");
    const auto control = alternate_owned("sub.f32.bf16 f0,b,f1;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("sub.f32.bf16 f0,0f3fc00000,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("sub.f32.bf16 f0,0f3fc00000,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-sub.sub_mixed_f32.default.src-f32");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-decimal");
    const auto control = alternate_owned("cvt.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.f32.bf16 f0,1.5;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.f32.bf16 f0,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-decimal");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-expression");
    const auto control = alternate_owned("cvt.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.f32.bf16 f0,(1.0+0.5);");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.f32.bf16 f0,(1.0+0.5);");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-expression");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-f64");
    const auto control = alternate_owned("cvt.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.f32.bf16 f0,0d3ff8000000000000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.f32.bf16 f0,0d3ff8000000000000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-f64");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-f32");
    const auto control = alternate_owned("cvt.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.f32.bf16 f0,0f3fc00000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.f32.bf16 f0,0f3fc00000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16.default.src-f32");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-decimal");
    const auto control = alternate_owned("cvt.ftz.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.ftz.f32.bf16 f0,1.5;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.ftz.f32.bf16 f0,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-decimal");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-expression");
    const auto control = alternate_owned("cvt.ftz.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.ftz.f32.bf16 f0,(1.0+0.5);");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.ftz.f32.bf16 f0,(1.0+0.5);");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate(
        "ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-expression");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-f64");
    const auto control = alternate_owned("cvt.ftz.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual =
        alternate_owned("cvt.ftz.f32.bf16 f0,0d3ff8000000000000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text =
        alternate_module("cvt.ftz.f32.bf16 f0,0d3ff8000000000000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-f64");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-f32");
    const auto control = alternate_owned("cvt.ftz.f32.bf16 f0,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.ftz.f32.bf16 f0,0f3fc00000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.ftz.f32.bf16 f0,0f3fc00000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate("ALTSC-SOURCE-cvt.cvt_f32_bf16_ftz.default.src-f32");
  }
  {
    SCOPED_TRACE(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-decimal");
    const auto control = alternate_owned("cvt.rzi.s32.bf16 s,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.rzi.s32.bf16 s,1.5;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.rzi.s32.bf16 s,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-decimal");
  }
  {
    SCOPED_TRACE(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-expression");
    const auto control = alternate_owned("cvt.rzi.s32.bf16 s,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.rzi.s32.bf16 s,(1.0+0.5);");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.rzi.s32.bf16 s,(1.0+0.5);");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-expression");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-f64");
    const auto control = alternate_owned("cvt.rzi.s32.bf16 s,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual =
        alternate_owned("cvt.rzi.s32.bf16 s,0d3ff8000000000000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text =
        alternate_module("cvt.rzi.s32.bf16 s,0d3ff8000000000000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-f64");
  }
  {
    SCOPED_TRACE("ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-f32");
    const auto control = alternate_owned("cvt.rzi.s32.bf16 s,b;");
    ASSERT_TRUE(control) << control.error().front().message;
    const auto actual = alternate_owned("cvt.rzi.s32.bf16 s,0f3fc00000;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    auto copy = *actual;
    const auto& word =
        alternate_bf16_word(*copy.functions.front().body.front());
    EXPECT_EQ(word.type, ScalarType::BF16);
    EXPECT_EQ(word.bits, 0x3fc0u);
    EXPECT_FALSE(word.integer_source_bits);
    EXPECT_FALSE(word.is_negative);
    expect_alternate_owned(*actual);
    const auto text = alternate_module("cvt.rzi.s32.bf16 s,0f3fc00000;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_TRUE(validateModule(*ast, *actual));
    record_alternate(
        "ALTSC-SOURCE-cvt.cvt_non_f32_base_from_bf16.default.src-f32");
  }
}
TEST(AlternateScalarImmediates, ValidatesActualCurrentWordsAcrossOwnedCopies) {
  {
    const auto text = alternate_module("add.f32.bf16 f0,1.5,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto actual = alternate_owned("add.f32.bf16 f0,1.5,f1;");
    ASSERT_TRUE(actual);
    auto& instruction = *actual->functions.front().body.front();
    auto& word = alternate_bf16_word(instruction);
    const auto original = word;
    word = original;
    word.bits = 0x10000;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-add.add_mixed_f32.default.src-word highbit16");
    word = original;
    word.integer_source_bits = 0;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-add.add_mixed_f32.default.src-integer_source_bits=0");
    word = original;
    word.is_negative = true;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-add.add_mixed_f32.default.src-is_negative=true");
    word = original;
    word.type = ScalarType::F16;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-add.add_mixed_f32.default.src-wrong immediate "
        "logical ScalarType F16");
    word = original;
    word.bits = 0x7f81;
    EXPECT_TRUE(instruction.check(alternate_context()));
    EXPECT_TRUE(validateModule(*ast, *actual));
    expect_alternate_owned(*actual);
    record_alternate(
        "ALTSC-ACTUAL-OWN-add.add_mixed_f32.default.src-coherent in-width word "
        "edit includingBF16sNaN0x7f81");
    word = original;
    EXPECT_TRUE(validateModule(*actual));
  }
  {
    const auto text = alternate_module("sub.f32.bf16 f0,1.5,f1;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto actual = alternate_owned("sub.f32.bf16 f0,1.5,f1;");
    ASSERT_TRUE(actual);
    auto& instruction = *actual->functions.front().body.front();
    auto& word = alternate_bf16_word(instruction);
    const auto original = word;
    word = original;
    word.bits = 0x10000;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-sub.sub_mixed_f32.default.src-word highbit16");
    word = original;
    word.integer_source_bits = 0;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-sub.sub_mixed_f32.default.src-integer_source_bits=0");
    word = original;
    word.is_negative = true;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-sub.sub_mixed_f32.default.src-is_negative=true");
    word = original;
    word.type = ScalarType::F16;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-sub.sub_mixed_f32.default.src-wrong immediate "
        "logical ScalarType F16");
    word = original;
    word.bits = 0x7f81;
    EXPECT_TRUE(instruction.check(alternate_context()));
    EXPECT_TRUE(validateModule(*ast, *actual));
    expect_alternate_owned(*actual);
    record_alternate(
        "ALTSC-ACTUAL-OWN-sub.sub_mixed_f32.default.src-coherent in-width word "
        "edit includingBF16sNaN0x7f81");
    word = original;
    EXPECT_TRUE(validateModule(*actual));
  }
  {
    const auto text = alternate_module("cvt.f32.bf16 f0,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto actual = alternate_owned("cvt.f32.bf16 f0,1.5;");
    ASSERT_TRUE(actual);
    auto& instruction = *actual->functions.front().body.front();
    auto& word = alternate_bf16_word(instruction);
    const auto original = word;
    word = original;
    word.bits = 0x10000;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16.default.src-word highbit16");
    word = original;
    word.integer_source_bits = 0;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16.default.src-integer_source_bits=0");
    word = original;
    word.is_negative = true;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16.default.src-is_negative=true");
    word = original;
    word.type = ScalarType::F16;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16.default.src-wrong immediate logical "
        "ScalarType F16");
    word = original;
    word.bits = 0x7f81;
    EXPECT_TRUE(instruction.check(alternate_context()));
    EXPECT_TRUE(validateModule(*ast, *actual));
    expect_alternate_owned(*actual);
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16.default.src-coherent in-width word "
        "edit includingBF16sNaN0x7f81");
    word = original;
    EXPECT_TRUE(validateModule(*actual));
  }
  {
    const auto text = alternate_module("cvt.ftz.f32.bf16 f0,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto actual = alternate_owned("cvt.ftz.f32.bf16 f0,1.5;");
    ASSERT_TRUE(actual);
    auto& instruction = *actual->functions.front().body.front();
    auto& word = alternate_bf16_word(instruction);
    const auto original = word;
    word = original;
    word.bits = 0x10000;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16_ftz.default.src-word highbit16");
    word = original;
    word.integer_source_bits = 0;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16_ftz.default.src-integer_source_bits="
        "0");
    word = original;
    word.is_negative = true;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16_ftz.default.src-is_negative=true");
    word = original;
    word.type = ScalarType::F16;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16_ftz.default.src-wrong immediate "
        "logical ScalarType F16");
    word = original;
    word.bits = 0x7f81;
    EXPECT_TRUE(instruction.check(alternate_context()));
    EXPECT_TRUE(validateModule(*ast, *actual));
    expect_alternate_owned(*actual);
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_f32_bf16_ftz.default.src-coherent in-width "
        "word edit includingBF16sNaN0x7f81");
    word = original;
    EXPECT_TRUE(validateModule(*actual));
  }
  {
    const auto text = alternate_module("cvt.rzi.s32.bf16 s,1.5;");
    const auto ast = test_helpers::parseModule(text);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto actual = alternate_owned("cvt.rzi.s32.bf16 s,1.5;");
    ASSERT_TRUE(actual);
    auto& instruction = *actual->functions.front().body.front();
    auto& word = alternate_bf16_word(instruction);
    const auto original = word;
    word = original;
    word.bits = 0x10000;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_non_f32_base_from_bf16.default.src-word "
        "highbit16");
    word = original;
    word.integer_source_bits = 0;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_non_f32_base_from_bf16.default.src-integer_"
        "source_bits=0");
    word = original;
    word.is_negative = true;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_non_f32_base_from_bf16.default.src-is_"
        "negative=true");
    word = original;
    word.type = ScalarType::F16;
    EXPECT_FALSE(instruction.check(alternate_context()));
    EXPECT_FALSE(validateModule(*actual));
    EXPECT_FALSE(validateModule(*ast, *actual));
    {
      ResolvedModule copy(*actual);
      copy.functions.front().body.front() = instruction.clone();
      EXPECT_FALSE(validateModule(copy));
    }
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_non_f32_base_from_bf16.default.src-wrong "
        "immediate logical ScalarType F16");
    word = original;
    word.bits = 0x7f81;
    EXPECT_TRUE(instruction.check(alternate_context()));
    EXPECT_TRUE(validateModule(*ast, *actual));
    expect_alternate_owned(*actual);
    record_alternate(
        "ALTSC-ACTUAL-OWN-cvt.cvt_non_f32_base_from_bf16.default.src-coherent "
        "in-width word edit includingBF16sNaN0x7f81");
    word = original;
    EXPECT_TRUE(validateModule(*actual));
  }
}
TEST(AlternateScalarImmediates, PreservesFieldControlsAndConditionalCvtTuples) {
  const std::array<std::pair<std::string_view, std::string_view>, 5> cases{
      {{"add.add_mixed_f32.default.src", "add.f32.bf16 f0,1.5,f1;"},
       {"sub.sub_mixed_f32.default.src", "sub.f32.bf16 f0,1.5,f1;"},
       {"cvt.cvt_f32_bf16.default.src", "cvt.f32.bf16 f0,1.5;"},
       {"cvt.cvt_f32_bf16_ftz.default.src", "cvt.ftz.f32.bf16 f0,1.5;"},
       {"cvt.cvt_non_f32_base_from_bf16.default.src",
        "cvt.rzi.s32.bf16 s,1.5;"}}};
  for (const auto& [key, instruction] : cases) {
    auto actual = alternate_owned(std::string(instruction));
    ASSERT_TRUE(actual);
    EXPECT_EQ(alternate_bf16_word(*actual->functions.front().body.front()).bits,
              0x3fc0u);
    auto context = alternate_context();
    context.target.ptx_version = {1, 0};
    context.target.sm_version = 10;
    EXPECT_FALSE(actual->functions.front().body.front()->check(context));
    record_alternate("ALTSC-CONTEXT-" + std::string(key));
  }
  for (const auto mode : {"rn", "rz", "rm", "rp"}) {
    auto actual = alternate_owned("add." + std::string(mode) +
                                  ".sat.f32.bf16 f0,1.5,f1;");
    ASSERT_TRUE(actual) << actual.error().front().message;
    EXPECT_EQ(alternate_bf16_word(*actual->functions.front().body.front()).bits,
              0x3fc0u);
  }
  for (const auto type : {"u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
                          "f16", "f64", "bf16"}) {
    const std::string dtype(type);
    const std::string destination = dtype == "f64"   ? "d"
                                    : dtype == "f16" ? "h"
                                    : dtype == "bf16"
                                        ? "b"
                                        : (dtype.ends_with("64") ? "w" : "s");
    if (dtype == "f64") {
      EXPECT_FALSE(alternate_owned("cvt.rn.f64.bf16 d,b;"));
      EXPECT_FALSE(alternate_owned("cvt.rn.f64.bf16 d,1.5;"));
    }
    const std::string mode = dtype == "f64"   ? ""
                             : dtype == "f16" ? "rn."
                                              : "rzi.";
    const std::string prefix =
        "cvt." + mode + dtype + ".bf16 " + destination + ",";
    const auto control = alternate_owned(prefix + "b;");
    const auto actual = alternate_owned(prefix + "1.5;");
    if (control) {
      ASSERT_TRUE(actual) << actual.error().front().message;
      expect_alternate_owned(*actual);
      std::cout << "CANDIDATE " << dtype << " matched-register-positive\n";
    } else {
      EXPECT_FALSE(actual);
      std::cout << "CANDIDATE " << dtype << " conditional-register-rejection "
                << control.error().front().message << '\n';
    }
    record_alternate("ALTSC-CVT-CANDIDATE-" + dtype);
  }
}
TEST(AlternateScalarImmediates, PreservesIndependentTypeAndContextBoundaries) {
  const auto mixed = alternate_owned("add.f32.bf16 f0,1.5,1.5;");
  ASSERT_TRUE(mixed);
  const auto& add =
      dynamic_cast<const AddMixedF32&>(*mixed->functions.front().body.front());
  EXPECT_EQ(std::get<ResolvedImmediate>(add.src.value).bits, 0x3fc0u);
  EXPECT_EQ(std::get<ResolvedImmediate>(add.addend.value).type,
            ScalarType::F32);
  EXPECT_EQ(std::get<ResolvedImmediate>(add.addend.value).bits, 0x3fc00000u);
  record_alternate("ALTSC-CONTROL-mixed-F32-other");
  for (const auto type :
       {ScalarType::BF16, ScalarType::E4m3, ScalarType::E5m2}) {
    EXPECT_FALSE(detail::resolve_numeric_value(
        declaration_semantics::IntegerConstantValue{0, false}, {}, type));
  }
  for (const auto type : {ScalarType::B8, ScalarType::B16}) {
    EXPECT_FALSE(alternate_literal("1.5", type));
    EXPECT_FALSE(alternate_literal("0d3ff8000000000000", type));
  }
  EXPECT_EQ(alternate_literal("0f3fc00000", ScalarType::B32)->bits,
            0x3fc00000u);
  EXPECT_EQ(alternate_literal("0d3ff8000000000000", ScalarType::B64)->bits,
            0x3ff8000000000000ULL);
  record_alternate("ALTSC-CONTROL-Bit-types");
  for (const auto type :
       {ScalarType::F16x2, ScalarType::BF16x2, ScalarType::E4m3x2,
        ScalarType::E5m2x2, ScalarType::E4m3x4, ScalarType::E5m2x4})
    EXPECT_FALSE(alternate_literal("1.5", type));
  EXPECT_EQ(alternate_literal("0f3fc00000", ScalarType::E4m3x4)->bits,
            0x3fc00000u);
  record_alternate("ALTSC-CONTROL-packed");
  for (const auto format : {"bf16", "e4m3", "e5m2"}) {
    for (const auto space : {"global", "const"}) {
      const std::string source = ".version 9.3\n.target sm_100a\n." +
                                 std::string(space) + " ." + format +
                                 " value = 1.5;";
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    const std::string source =
        ".version 9.3\n.target sm_100a\n.extern .func take(.param ." +
        std::string(format) + " x);\n.entry kernel(){call take,(1.5);}";
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  record_alternate("ALTSC-CONTROL-storage-alternates");
  record_alternate("ALTSC-CONTROL-call-alternates");
  for (const auto instruction :
       {"add.bf16 b,1.5,b;", "set.eq.bf16.bf16 b,1.5,b;",
        "setp.eq.bf16 p,1.5,b;"}) {
    const auto ast = test_helpers::parseModule(alternate_module(instruction));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveModuleOnly(*ast));
  }
  record_alternate("ALTSC-CONTROL-regonly");
  const std::string source = alternate_module("add.f32.bf16 f0,1.5,f1;");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto actual = alternate_owned("add.f32.bf16 f0,1.5,f1;");
  ASSERT_TRUE(actual);
  const auto replacement =
      test_helpers::parseModule(alternate_module("add.f32.bf16 f0,2.5,f1;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  const auto mismatch = validateModule(*replacement, *actual);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  record_alternate("ALTSC-CONTROL-source-association");
  std::string partial_source = source.substr(source.find(".entry"));
  const auto partial_ast = test_helpers::parseModule(partial_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(partial_ast);
  auto partial = resolveModuleOnly(*partial_ast);
  ASSERT_TRUE(partial);
  alternate_bf16_word(*partial->functions.front().body.front()).bits = 0x10000;
  EXPECT_TRUE(
      validateModule(*partial, ModuleValidationPolicy::AvailableContext));
  EXPECT_FALSE(validateModule(*partial));
  record_alternate("ALTSC-CONTROL-partial");
}
TEST(AlternateScalarImmediates,
     UsesIntegerRoundingIndependentlyOfHostEnvironment) {
  const int original = std::fegetround();
  for (const int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode) != 0)
      continue;
    for (const auto& item : alternate_goldens) {
      if (!item.source.starts_with("0d"))
        continue;
      const auto value = alternate_literal(item.source, item.type);
      EXPECT_TRUE(value);
      if (value)
        EXPECT_EQ(value->bits, item.bits) << item.id;
    }
  }
  EXPECT_EQ(std::fesetround(original), 0);
  record_alternate("ALTSC-CONTROL-fenv");
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
