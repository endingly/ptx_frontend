#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "ptx_ir/instr.hpp"
#include "symbol_resolver.hpp"
#include "utils.hpp"

namespace ptx_frontend {

struct CompileTarget {
  uint32_t sm;        // e.g. 89, 90, 100, 120
  float ptx_version;  // e.g. 8.6, 9.2
};

struct TypeError {
  std::string message;
  // TODO: source location
};

class TypeChecker {
  const SymbolTable& sym_;
  const CompileTarget& target_;
  std::vector<TypeError> errors_;

  void error(std::string msg);
  bool require_sm(uint32_t min_v, std::string_view ctx);
  bool require_ptx(float min_v, std::string_view ctx);
  bool require_sm(uint32_t min_v);
  bool require_ptx(float min_v);

  /**
   * @brief Check if the operand matches the expected type.
   * 
   * @param op target operand
   * @param expected expected type
   * @return true if the operand matches the expected type
   * @return false if the operand does not match the expected type, and record an error message
   */
  bool check_operand(const ResolvedOp& op, ScalarType expected);
  bool check_operand(const ResolvedOp& op,
                     const std::vector<ScalarType>& expected);

  template <typename T, std::size_t N>
  static constexpr bool is_one_of(
      const T& target,
      const std::array<std::remove_const_t<T>, N>& arr) noexcept {
    return ptx_frontend::utils::contains(arr, target);
  }

 public:
  TypeChecker(const SymbolTable& sym, const CompileTarget& target)
      : sym_(sym), target_(target) {}

#include "type_checker.src.gen"

  const std::vector<TypeError>& errors() const { return errors_; }
};

};  // namespace ptx_frontend