#include "ptx_ir/semantic/ptx_special_register.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace ptx_frontend::special_registers {
namespace {

struct Entry {
  std::string_view spelling;
  Info info;
};

constexpr Info scalar(SpecialRegisterKind kind, ScalarType type,
                      uint16_t ptx_major, uint16_t ptx_minor, uint32_t sm = 0,
                      uint8_t index = 0) {
  return Info{
      .id = {.kind = kind, .index = index},
      .element_type = type,
      .vector_width = 1,
      .minimum_ptx_major = ptx_major,
      .minimum_ptx_minor = ptx_minor,
      .minimum_sm = sm,
  };
}

constexpr std::array exact_entries{
    Entry{"%laneid",
          scalar(SpecialRegisterKind::LaneId, ScalarType::U32, 1, 3)},
    Entry{"%warpid",
          scalar(SpecialRegisterKind::WarpId, ScalarType::U32, 1, 3)},
    Entry{"%nwarpid",
          scalar(SpecialRegisterKind::NWarpId, ScalarType::U32, 2, 0, 20)},
    Entry{"%smid", scalar(SpecialRegisterKind::SmId, ScalarType::U32, 1, 3)},
    Entry{"%nsmid",
          scalar(SpecialRegisterKind::NSmId, ScalarType::U32, 2, 0, 20)},
    Entry{"%gridid",
          scalar(SpecialRegisterKind::GridId, ScalarType::U64, 3, 0)},
    Entry{"%is_explicit_cluster", scalar(SpecialRegisterKind::IsExplicitCluster,
                                         ScalarType::Pred, 7, 8, 90)},
    Entry{"%cluster_ctarank", scalar(SpecialRegisterKind::ClusterCtaRank,
                                     ScalarType::U32, 7, 8, 90)},
    Entry{"%cluster_nctarank", scalar(SpecialRegisterKind::ClusterNCtaRank,
                                      ScalarType::U32, 7, 8, 90)},
    Entry{"%lanemask_eq",
          scalar(SpecialRegisterKind::LaneMaskEq, ScalarType::U32, 2, 0, 20)},
    Entry{"%lanemask_le",
          scalar(SpecialRegisterKind::LaneMaskLe, ScalarType::U32, 2, 0, 20)},
    Entry{"%lanemask_lt",
          scalar(SpecialRegisterKind::LaneMaskLt, ScalarType::U32, 2, 0, 20)},
    Entry{"%lanemask_ge",
          scalar(SpecialRegisterKind::LaneMaskGe, ScalarType::U32, 2, 0, 20)},
    Entry{"%lanemask_gt",
          scalar(SpecialRegisterKind::LaneMaskGt, ScalarType::U32, 2, 0, 20)},
    Entry{"%clock", scalar(SpecialRegisterKind::Clock, ScalarType::U32, 1, 0)},
    Entry{"%clock_hi",
          scalar(SpecialRegisterKind::ClockHi, ScalarType::U32, 5, 0, 20)},
    Entry{"%clock64",
          scalar(SpecialRegisterKind::Clock64, ScalarType::U64, 2, 0, 20)},
    Entry{"%globaltimer",
          scalar(SpecialRegisterKind::GlobalTimer, ScalarType::U64, 3, 1, 30)},
    Entry{"%globaltimer_lo", scalar(SpecialRegisterKind::GlobalTimerLo,
                                    ScalarType::U32, 3, 1, 30)},
    Entry{"%globaltimer_hi", scalar(SpecialRegisterKind::GlobalTimerHi,
                                    ScalarType::U32, 3, 1, 30)},
    Entry{"%reserved_smem_offset_begin",
          scalar(SpecialRegisterKind::ReservedSmemOffsetBegin, ScalarType::B32,
                 7, 6, 80)},
    Entry{"%reserved_smem_offset_end",
          scalar(SpecialRegisterKind::ReservedSmemOffsetEnd, ScalarType::B32, 7,
                 6, 80)},
    Entry{"%reserved_smem_offset_cap",
          scalar(SpecialRegisterKind::ReservedSmemOffsetCap, ScalarType::B32, 7,
                 6, 80)},
    Entry{"%total_smem_size", scalar(SpecialRegisterKind::TotalSmemSize,
                                     ScalarType::U32, 4, 1, 20)},
    Entry{"%aggr_smem_size",
          scalar(SpecialRegisterKind::AggrSmemSize, ScalarType::U32, 8, 1, 90)},
    Entry{"%dynamic_smem_size", scalar(SpecialRegisterKind::DynamicSmemSize,
                                       ScalarType::U32, 4, 1, 20)},
    Entry{"%current_graph_exec", scalar(SpecialRegisterKind::CurrentGraphExec,
                                        ScalarType::U64, 8, 0, 50)},
};

constexpr std::array vector_entries{
    Entry{"%tid", scalar(SpecialRegisterKind::Tid, ScalarType::U32, 2, 0)},
    Entry{"%ntid", scalar(SpecialRegisterKind::NTid, ScalarType::U32, 2, 0)},
    Entry{"%ctaid", scalar(SpecialRegisterKind::CtaId, ScalarType::U32, 2, 0)},
    Entry{"%nctaid",
          scalar(SpecialRegisterKind::NCtaId, ScalarType::U32, 2, 0)},
    Entry{"%clusterid",
          scalar(SpecialRegisterKind::ClusterId, ScalarType::U32, 7, 8, 90)},
    Entry{"%nclusterid",
          scalar(SpecialRegisterKind::NClusterId, ScalarType::U32, 7, 8, 90)},
    Entry{"%cluster_ctaid",
          scalar(SpecialRegisterKind::ClusterCtaId, ScalarType::U32, 7, 8, 90)},
    Entry{"%cluster_nctaid", scalar(SpecialRegisterKind::ClusterNCtaId,
                                    ScalarType::U32, 7, 8, 90)},
};

std::optional<uint32_t> parseIndexed(std::string_view spelling,
                                     std::string_view prefix, uint32_t count,
                                     std::string_view suffix = {}) {
  if (!spelling.starts_with(prefix) || !spelling.ends_with(suffix) ||
      spelling.size() <= prefix.size() + suffix.size()) {
    return std::nullopt;
  }
  const std::string_view index_text = spelling.substr(
      prefix.size(), spelling.size() - prefix.size() - suffix.size());
  if (index_text.size() > 1 && index_text.front() == '0')
    return std::nullopt;
  uint32_t index = 0;
  const auto [end, error] = std::from_chars(
      index_text.data(), index_text.data() + index_text.size(), index);
  if (error != std::errc{} || end != index_text.data() + index_text.size() ||
      index >= count) {
    return std::nullopt;
  }
  return index;
}

}  // namespace

std::optional<Info> lookup(std::string_view spelling) noexcept {
  const auto exact =
      std::ranges::find(exact_entries, spelling, &Entry::spelling);
  if (exact != exact_entries.end())
    return exact->info;

  for (const Entry& entry : vector_entries) {
    if (spelling == entry.spelling) {
      Info info = entry.info;
      info.vector_width = 4;
      return info;
    }
    if (spelling.starts_with(entry.spelling) &&
        spelling.size() == entry.spelling.size() + 2 &&
        spelling[entry.spelling.size()] == '.' &&
        (spelling.back() == 'x' || spelling.back() == 'y' ||
         spelling.back() == 'z')) {
      return entry.info;
    }
  }

  if (const auto index = parseIndexed(spelling, "%pm", 8, "_64")) {
    return scalar(SpecialRegisterKind::PerformanceMonitor64, ScalarType::U64, 4,
                  0, 50, static_cast<uint8_t>(*index));
  }
  if (const auto index = parseIndexed(spelling, "%pm", 8)) {
    return *index < 4
               ? scalar(SpecialRegisterKind::PerformanceMonitor,
                        ScalarType::U32, 1, 3, 0, static_cast<uint8_t>(*index))
               : scalar(SpecialRegisterKind::PerformanceMonitor,
                        ScalarType::U32, 3, 0, 20,
                        static_cast<uint8_t>(*index));
  }
  if (const auto index = parseIndexed(spelling, "%envreg", 32)) {
    return scalar(SpecialRegisterKind::Environment, ScalarType::B32, 2, 1, 0,
                  static_cast<uint8_t>(*index));
  }
  if (const auto index = parseIndexed(spelling, "%reserved_smem_offset_", 2)) {
    return scalar(SpecialRegisterKind::ReservedSmemOffset, ScalarType::B32, 7,
                  6, 80, static_cast<uint8_t>(*index));
  }
  return std::nullopt;
}

Info metadata(SpecialRegisterId id) noexcept {
  const auto has_id = [id](const Entry& entry) {
    return entry.info.id == id;
  };
  if (const auto exact = std::ranges::find_if(exact_entries, has_id);
      exact != exact_entries.end()) {
    return exact->info;
  }
  if (const auto vector = std::ranges::find_if(vector_entries, has_id);
      vector != vector_entries.end()) {
    Info info = vector->info;
    info.vector_width = 4;
    return info;
  }

  switch (id.kind) {
    case SpecialRegisterKind::PerformanceMonitor64:
      return scalar(id.kind, ScalarType::U64, 4, 0, 50, id.index);
    case SpecialRegisterKind::PerformanceMonitor:
      return id.index < 4
                 ? scalar(id.kind, ScalarType::U32, 1, 3, 0, id.index)
                 : scalar(id.kind, ScalarType::U32, 3, 0, 20, id.index);
    case SpecialRegisterKind::Environment:
      return scalar(id.kind, ScalarType::B32, 2, 1, 0, id.index);
    case SpecialRegisterKind::ReservedSmemOffset:
      return scalar(id.kind, ScalarType::B32, 7, 6, 80, id.index);
    default:
      return {};
  }
}

}  // namespace ptx_frontend::special_registers
