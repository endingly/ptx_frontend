#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ptx_ir/source_loc.hpp"
#include "ptx_ir/syntax/ptx_syntax_ast.hpp"

namespace ptx_frontend::binding {

struct SymbolId {
  uint32_t value{};
  bool operator==(const SymbolId&) const = default;
};

struct ScopeId {
  uint32_t value{};
  bool operator==(const ScopeId&) const = default;
};

enum class ScopeKind : uint8_t {
  Module,
  Function,
};

enum class SymbolKind : uint8_t {
  Variable,
  InputParameter,
  ReturnParameter,
  Function,
  Label,
};

enum class ReferenceKind : uint8_t {
  InstructionOperand,
  Predicate,
  Initializer,
  ArrayDimension,
};

struct Scope {
  ScopeId id;
  ScopeKind kind{};
  std::optional<ScopeId> parent;
  std::optional<SymbolId> owner;
};

struct Symbol {
  SymbolId id;
  ScopeId scope;
  SymbolKind kind{};
  std::string name;
  SourceRange declaration_range;
  std::optional<syntax_ast::AstStateSpace> state_space;
  std::optional<std::string> type;
  std::optional<uint32_t> parameterized_count;
  std::optional<ScopeId> owned_scope;
};

struct SymbolLookup {
  SymbolId symbol;
  std::optional<uint32_t> parameterized_index;
};

struct SymbolReference {
  ScopeId scope;
  ReferenceKind kind{};
  std::string spelling;
  SourceRange range;
  std::optional<SymbolLookup> target;
};

enum class BindDiagnosticKind : uint8_t {
  DuplicateSymbol,
  InvalidParameterizedCount,
};

struct BindDiagnostic {
  BindDiagnosticKind kind{};
  SourceRange range;
  std::optional<SourceRange> previous_range;
  std::string message;
};

/** A lexical symbol table for one parsed PTX module. */
class SymbolTable {
 public:
  [[nodiscard]] ScopeId moduleScope() const noexcept { return ScopeId{0}; }

  [[nodiscard]] const std::vector<Scope>& scopes() const noexcept {
    return scopes_;
  }
  [[nodiscard]] const std::vector<Symbol>& symbols() const noexcept {
    return symbols_;
  }
  [[nodiscard]] const std::vector<SymbolReference>& references()
      const noexcept {
    return references_;
  }

  [[nodiscard]] const Scope& scope(ScopeId id) const;
  [[nodiscard]] const Symbol& symbol(SymbolId id) const;

  /** Look up an exact or parameterized name, walking parent scopes. */
  [[nodiscard]] std::optional<SymbolLookup> lookup(ScopeId scope,
                                                   std::string_view name) const;

 private:
  friend struct SymbolTableBuilder;

  std::vector<Scope> scopes_;
  std::vector<Symbol> symbols_;
  std::vector<SymbolReference> references_;
};

struct SymbolBinding {
  SymbolTable table;
  std::vector<BindDiagnostic> diagnostics;
};

/** Collect declarations and lexically bind references in a Syntax AST module. */
[[nodiscard]] SymbolBinding bindSymbols(const syntax_ast::AstModule& module);

}  // namespace ptx_frontend::binding
