#include "ptx_ir/semantic/ptx_special_register.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace ptx_frontend::special_registers {
namespace {

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
  struct ExactEntry {
    std::string_view spelling;
    Info info;
  };
  constexpr auto scalar = [](ScalarType type, uint16_t ptx_major,
                             uint16_t ptx_minor, uint32_t sm = 0) {
    return Info{
        .element_type = type,
        .vector_width = 1,
        .minimum_ptx_major = ptx_major,
        .minimum_ptx_minor = ptx_minor,
        .minimum_sm = sm,
    };
  };
  constexpr std::array exact_entries{
      ExactEntry{"%laneid", scalar(ScalarType::U32, 1, 3)},
      ExactEntry{"%warpid", scalar(ScalarType::U32, 1, 3)},
      ExactEntry{"%nwarpid", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%smid", scalar(ScalarType::U32, 1, 3)},
      ExactEntry{"%nsmid", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%gridid", scalar(ScalarType::U64, 3, 0)},
      ExactEntry{"%is_explicit_cluster", scalar(ScalarType::Pred, 7, 8, 90)},
      ExactEntry{"%cluster_ctarank", scalar(ScalarType::U32, 7, 8, 90)},
      ExactEntry{"%cluster_nctarank", scalar(ScalarType::U32, 7, 8, 90)},
      ExactEntry{"%lanemask_eq", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%lanemask_le", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%lanemask_lt", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%lanemask_ge", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%lanemask_gt", scalar(ScalarType::U32, 2, 0, 20)},
      ExactEntry{"%clock", scalar(ScalarType::U32, 1, 0)},
      ExactEntry{"%clock_hi", scalar(ScalarType::U32, 5, 0, 20)},
      ExactEntry{"%clock64", scalar(ScalarType::U64, 2, 0, 20)},
      ExactEntry{"%globaltimer", scalar(ScalarType::U64, 3, 1, 30)},
      ExactEntry{"%globaltimer_lo", scalar(ScalarType::U32, 3, 1, 30)},
      ExactEntry{"%globaltimer_hi", scalar(ScalarType::U32, 3, 1, 30)},
      ExactEntry{"%reserved_smem_offset_begin",
                 scalar(ScalarType::B32, 7, 6, 80)},
      ExactEntry{"%reserved_smem_offset_end",
                 scalar(ScalarType::B32, 7, 6, 80)},
      ExactEntry{"%reserved_smem_offset_cap",
                 scalar(ScalarType::B32, 7, 6, 80)},
      ExactEntry{"%total_smem_size", scalar(ScalarType::U32, 4, 1, 20)},
      ExactEntry{"%aggr_smem_size", scalar(ScalarType::U32, 8, 1, 90)},
      ExactEntry{"%dynamic_smem_size", scalar(ScalarType::U32, 4, 1, 20)},
      ExactEntry{"%current_graph_exec", scalar(ScalarType::U64, 8, 0, 50)},
  };
  const auto exact =
      std::ranges::find(exact_entries, spelling, &ExactEntry::spelling);
  if (exact != exact_entries.end())
    return exact->info;

  constexpr std::array vector_entries{
      ExactEntry{"%tid", scalar(ScalarType::U32, 2, 0)},
      ExactEntry{"%ntid", scalar(ScalarType::U32, 2, 0)},
      ExactEntry{"%ctaid", scalar(ScalarType::U32, 2, 0)},
      ExactEntry{"%nctaid", scalar(ScalarType::U32, 2, 0)},
      ExactEntry{"%clusterid", scalar(ScalarType::U32, 7, 8, 90)},
      ExactEntry{"%nclusterid", scalar(ScalarType::U32, 7, 8, 90)},
      ExactEntry{"%cluster_ctaid", scalar(ScalarType::U32, 7, 8, 90)},
      ExactEntry{"%cluster_nctaid", scalar(ScalarType::U32, 7, 8, 90)},
  };
  for (const ExactEntry& entry : vector_entries) {
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

  if (parseIndexed(spelling, "%pm", 8, "_64"))
    return scalar(ScalarType::U64, 4, 0, 50);
  if (const auto index = parseIndexed(spelling, "%pm", 8)) {
    return *index < 4 ? scalar(ScalarType::U32, 1, 3)
                      : scalar(ScalarType::U32, 3, 0, 20);
  }
  if (parseIndexed(spelling, "%envreg", 32))
    return scalar(ScalarType::B32, 2, 1);
  if (parseIndexed(spelling, "%reserved_smem_offset_", 2))
    return scalar(ScalarType::B32, 7, 6, 80);
  return std::nullopt;
}

}  // namespace ptx_frontend::special_registers
