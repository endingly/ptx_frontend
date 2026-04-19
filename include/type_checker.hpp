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

// Legacy lightweight symbol table used by the standalone TypeChecker
// (before the full SymbolTable / ResolvedModule pipeline is integrated).
// Kept for backward-compatibility with existing tests.
struct RegDecl {
  ScalarType type;   // reg type, e.g. F32
  StateSpace space;  // state space, e.g. Reg
};
using LegacySymbolTable = std::unordered_map<std::string, RegDecl>;

class TypeChecker {
  const LegacySymbolTable& sym_;
  const CompileTarget& target_;
  std::vector<TypeError> errors_;

  void error(std::string msg);
  bool require_sm(uint32_t min_v, std::string_view ctx);
  bool require_ptx(float min_v, std::string_view ctx);
  bool require_sm(uint32_t min_v);
  bool require_ptx(float min_v);

  // operand type check via LegacySymbolTable
  void check_operand(const ParsedOp& op, ScalarType expected);
  void check_dst_src2(const InstrAdd<ParsedOp>& i, ScalarType t);

  template <typename T, std::size_t N>
  static constexpr bool is_one_of(
      const T& target,
      const std::array<std::remove_const_t<T>, N>& arr) noexcept {
    return ptx_frontend::utils::contains(arr, target);
  }

 public:
  TypeChecker(const LegacySymbolTable& sym, const CompileTarget& target)
      : sym_(sym), target_(target) {}

#include "type_checker.src.gen"

  const std::vector<TypeError>& errors() const { return errors_; }
};

};  // namespace ptx_frontend