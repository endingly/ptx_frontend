#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "ptx_ir/instr.hpp"

namespace ptx_frontend {

// ============================================================
// §1  SymbolId  —  opaque integer handle for every declared symbol
// ============================================================

using SymbolId = uint32_t;
static constexpr SymbolId kInvalidSymbolId = UINT32_MAX;

// ============================================================
// §2  SymbolKind  —  what kind of entity a symbol represents
// ============================================================

enum class SymbolKind : uint8_t {
  Register,  // .reg variable (including parameterised ranges %r<N>)
  Global,    // .global variable
  Const,     // .const variable
  Local,     // .local variable
  Shared,    // .shared / .shared::cluster variable
  Param,     // function parameter or return argument (.param)
  Label,     // jump-target label  (ident:)
  Function,  // function or kernel name (.entry / .func)
};

// ============================================================
// §3  SymbolEntry  —  information stored per declared symbol
// ============================================================

struct SymbolEntry {
  std::string name;        // owned copy of the identifier
  SymbolKind  kind;        // what kind of entity
  Type        type;        // PTX type (e.g. ScalarTypeT{F32})
  StateSpace  space;       // state space (.reg, .global, …)
  std::string func_scope;  // "" = module scope; function name = local scope
};

// ============================================================
// §4  ResolveError
// ============================================================

struct ResolveError {
  std::string message;
};

// ============================================================
// §5  SymbolTable
// ============================================================

/// Flat, globally-indexed symbol table.
///
/// All symbols (module-level and per-function) share one flat
/// `entries_` vector.  Each symbol gets a globally-unique SymbolId
/// (its index).  Two look-up maps maintain name → SymbolId:
///
///   module_scope_  – functions and global/const variables
///   func_scopes_   – per-function registers, params, labels
///     (key = function name, value = name → SymbolId)
///
/// Look-up order (during resolution):
///   1.  function-local scope (if a function name is supplied)
///   2.  module scope
class SymbolTable {
 public:
  SymbolTable() = default;

  // ---- insertion --------------------------------------------------

  /// Insert a module-level symbol (function, global, const, …).
  /// Returns an error if the name is already present in the module scope.
  expected<SymbolId, ResolveError> insert_global(std::string name,
                                                  SymbolKind kind, Type type,
                                                  StateSpace space);

  /// Insert a function-local symbol (register, param, label, …).
  /// `func_name` identifies the enclosing function scope.
  /// Returns an error if the name is already declared in that scope.
  expected<SymbolId, ResolveError> insert_local(const std::string& func_name,
                                                 std::string name,
                                                 SymbolKind kind, Type type,
                                                 StateSpace space);

  // ---- look-up ----------------------------------------------------

  /// Look up a name, searching the function scope first, then the
  /// module scope.  Pass an empty `func_name` to search only the
  /// module scope.
  expected<SymbolId, ResolveError> lookup(
      std::string_view name, const std::string& func_name = "") const;

  // ---- access -----------------------------------------------------

  const SymbolEntry& entry(SymbolId id) const { return entries_.at(id); }
  const std::vector<SymbolEntry>& entries() const { return entries_; }
  std::size_t size() const { return entries_.size(); }

 private:
  /// Allocate a new SymbolEntry and return its id.
  SymbolId alloc(std::string name, SymbolKind kind, Type type, StateSpace space,
                 std::string func_scope);

  std::vector<SymbolEntry> entries_;

  // module-level name → SymbolId
  std::unordered_map<std::string, SymbolId> module_scope_;

  // function_name → (local_name → SymbolId)
  std::unordered_map<std::string,
                     std::unordered_map<std::string, SymbolId>>
      func_scopes_;
};

// ============================================================
// §6  ResolvedOp  —  ParsedOperand after symbol resolution
// ============================================================

/// After resolution every Ident in operands is replaced by a SymbolId.
using ResolvedOp = ParsedOperand<SymbolId>;

// ============================================================
// §7  ResolvedModule  —  Module after symbol resolution
// ============================================================

/// A PTX module where all operand identifiers have been replaced by
/// their SymbolId, and all declarations have been collected into a
/// SymbolTable.
struct ResolvedModule {
  std::pair<uint8_t, uint8_t>         ptx_version;
  uint32_t                            sm_version;
  SymbolTable                         symbols;
  std::vector<Directive<ResolvedOp>>  directives;
  size_t                              invalid_directives = 0;
};

// ============================================================
// §8  resolve_module  —  main entry point
// ============================================================

/// Build the symbol table and resolve all operand identifiers in
/// a parsed module.
///
/// Errors are appended to `out_errors`; resolution continues even
/// after errors (undefined symbols are given kInvalidSymbolId so the
/// caller sees as many problems as possible in one pass).
ResolvedModule resolve_module(const Module& mod,
                              std::vector<ResolveError>& out_errors);

}  // namespace ptx_frontend
