#include "ptx_visit_dispatch.hpp"
#include "ptx_ir.hpp"

namespace ptx_frontend {

template <>
ScalarType get_scalar_type(const ArithDetails& data) {
  return std::visit(
      [](auto&& item) -> ScalarType {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, ArithInteger>) {
          return item.type_;
        } else if constexpr (std::is_same_v<T, ArithFloat>) {
          return item.type_;
        }
      },
      data);
}

};  // namespace ptx_frontend