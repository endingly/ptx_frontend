#pragma once

#include "test_instruction_access.hpp"

#include <cstddef>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/resolved_instruction_union.gen.hpp>

namespace test_ir_access {

/** Visit an exact borrowed opcode payload without copying the owner or payload. */
template <typename Result, std::size_t Index = 0, typename Visitor,
          typename Owner>
  requires std::same_as<std::remove_cvref_t<Owner>,
                        ptx_frontend::resolved_ir::OwnedInstruction>
Result visit_owned(Visitor&& visitor, Owner&& owner) {
  if constexpr (Index == std::variant_size_v<
                             ptx_frontend::resolved_ir::InstructionUnion>) {
    throw std::bad_variant_access();
  } else {
    using T =
        std::variant_alternative_t<Index,
                                   ptx_frontend::resolved_ir::InstructionUnion>;
    if (auto* value = owner.template get_if<T>())
      return std::invoke(std::forward<Visitor>(visitor), *value);
    return visit_owned<Result, Index + 1>(std::forward<Visitor>(visitor),
                                          std::forward<Owner>(owner));
  }
}

/** Visit a mutable borrowed owner payload with the visitor's original result. */
template <typename Visitor>
decltype(auto) visit(Visitor&& visitor,
                     ptx_frontend::resolved_ir::OwnedInstruction& owner) {
  using First =
      std::variant_alternative_t<0,
                                 ptx_frontend::resolved_ir::InstructionUnion>;
  using Result = std::invoke_result_t<Visitor&, First&>;
  return visit_owned<Result>(std::forward<Visitor>(visitor), owner);
}

/** Visit a const borrowed owner payload with the visitor's original result. */
template <typename Visitor>
decltype(auto) visit(Visitor&& visitor,
                     const ptx_frontend::resolved_ir::OwnedInstruction& owner) {
  using First =
      std::variant_alternative_t<0,
                                 ptx_frontend::resolved_ir::InstructionUnion>;
  using Result = std::invoke_result_t<Visitor&, const First&>;
  return visit_owned<Result>(std::forward<Visitor>(visitor), owner);
}

}  // namespace test_ir_access
