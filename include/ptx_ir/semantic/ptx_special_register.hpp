#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ptx_ir/base.hpp"

namespace ptx_frontend::special_registers {

/** Target-independent identity shared by a vector base and its components. */
enum class SpecialRegisterKind : uint8_t {
  Invalid,
  LaneId,
  WarpId,
  NWarpId,
  SmId,
  NSmId,
  GridId,
  IsExplicitCluster,
  ClusterCtaRank,
  ClusterNCtaRank,
  LaneMaskEq,
  LaneMaskLe,
  LaneMaskLt,
  LaneMaskGe,
  LaneMaskGt,
  Clock,
  ClockHi,
  Clock64,
  GlobalTimer,
  GlobalTimerLo,
  GlobalTimerHi,
  ReservedSmemOffsetBegin,
  ReservedSmemOffsetEnd,
  ReservedSmemOffsetCap,
  TotalSmemSize,
  AggrSmemSize,
  DynamicSmemSize,
  CurrentGraphExec,
  Tid,
  NTid,
  CtaId,
  NCtaId,
  ClusterId,
  NClusterId,
  ClusterCtaId,
  ClusterNCtaId,
  PerformanceMonitor,
  PerformanceMonitor64,
  Environment,
  ReservedSmemOffset,
};

/** Stable identity of one intrinsic register or indexed-family member. */
struct SpecialRegisterId {
  SpecialRegisterKind kind = SpecialRegisterKind::Invalid;
  uint8_t index = 0;
  bool operator==(const SpecialRegisterId&) const = default;
};

/** A selected component kept separately from the intrinsic register identity. */
enum class VectorComponent : uint8_t { X, Y, Z };

/** Target requirements and declared element type of one predefined sreg. */
struct Info {
  SpecialRegisterId id;
  ScalarType element_type = ScalarType::Invalid;
  uint8_t vector_width = 1;
  uint16_t minimum_ptx_major = 0;
  uint16_t minimum_ptx_minor = 0;
  uint32_t minimum_sm = 0;
  bool operator==(const Info&) const = default;
};

/** Return semantic metadata for an exact predefined special-register spelling. */
[[nodiscard]] std::optional<Info> lookup(std::string_view spelling) noexcept;

/** Return intrinsic metadata for a previously resolved stable identity. */
[[nodiscard]] Info metadata(SpecialRegisterId id) noexcept;

}  // namespace ptx_frontend::special_registers
