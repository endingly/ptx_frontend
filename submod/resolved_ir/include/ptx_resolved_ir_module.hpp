#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include "resolved_ir.gen.hpp"

namespace ptx_frontend::resolved_ir {

/** Associates a bound function-local label with its flattened body position. */
struct ResolvedLabelPosition {
  /** Label identity in the owning module's symbol table. */
  binding::SymbolId symbol_id;
  /** Zero-based index into the owning function's instruction body. */
  std::size_t instruction_offset;
  /** Compare the bound label identity and its body position. */
  bool operator==(const ResolvedLabelPosition&) const = default;
};

/** Owns one resolved entry, device function, or declaration-only prototype. */
struct ResolvedFunction {
  /** Function identity in the owning module's symbol table. */
  binding::SymbolId symbol_id;
  /** Owned source spelling of the function name. */
  std::string name;
  /** Whether this declaration denotes a kernel entry. */
  bool is_entry{};
  /** Whether this declaration has no source body. */
  bool is_prototype{};
  /** Owned flattened instructions, ordered as they occur in the function body. */
  std::vector<ResolvedInstruction> body;
  /** Bound label locations whose offsets index ``body``. */
  std::vector<ResolvedLabelPosition> label_positions;
  /** Source extent of this function declaration and body. */
  SourceRange range;
  /**
   * Owned .param returns, inputs, then body declarations in lexical traversal
   * order. Filter ParameterDeclarationRole::EntryInput for source-ordered
   * entry inputs.
   */
  std::vector<ResolvedParameterDeclaration> parameter_declarations;
  /** Function declaration scope, stable in the owning module symbol table. */
  binding::ScopeId declaration_scope;
  /** One owned source range per body instruction, parallel to ``body``. */
  std::vector<SourceRange> instruction_ranges;
  /** One owned source opcode spelling per body instruction, parallel to ``body``. */
  std::vector<std::string> instruction_opcodes;
  /** Owned enclosing source target spelling when a module supplied one. */
  std::optional<std::string> source_target;
  /** Parsed enclosing source version when a module supplied one. */
  std::optional<checker::PtxVersion> source_version;
  /** Location-independent owned function identity used for source retargeting. */
  std::string source_identity;
};

/** Owns all resolved declarations, functions, and source correspondence data. */
struct ResolvedModule {
  /** Owning symbol table; every retained SymbolId and ScopeId refers to it. */
  binding::SymbolTable symbols;
  /** Owned functions and prototypes in source traversal order. */
  std::vector<ResolvedFunction> functions;
  /** Source extent of the complete module. */
  SourceRange range;
  /** Owned non-parameter storage declarations, in source traversal order. */
  std::vector<ResolvedStorageDeclaration> storage_declarations;
  /** Canonical module syntax identity excluding target, version, and locations. */
  std::string source_identity;
};

}  // namespace ptx_frontend::resolved_ir
