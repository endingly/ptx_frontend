#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ptx_frontend/base/ptx_ast_types.hpp>
#include <ptx_frontend/common/source_loc.hpp>

namespace ptx_frontend::syntax_ast {
struct AstModule;
}

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
  Block,
};

enum class SymbolKind : uint8_t {
  Variable,
  InputParameter,
  ReturnParameter,
  CallParameter,
  Function,
  Label,
  CallPrototype,
  CallTargetSet,
  BranchTargetSet,
  /** Debug-only module metadata; excluded from ordinary lexical lookup. */
  DebugFile,
  /** `.debug_str` and its raw payload labels; metadata-only. */
  DebugStringLabel,
};

enum class SymbolLinkage : uint8_t {
  None,
  External,
  Visible,
  Weak,
};

enum class ReferenceKind : uint8_t {
  InstructionOperand,
  Predicate,
  Initializer,
  ArrayDimension,
  CallTarget,
  CallReturnParameter,
  CallArgument,
  CallTargetSet,
  BranchTarget,
  BranchTargetSet,
  DebugFile,
  DebugFunctionName,
};

enum class ReferenceClassification : uint8_t {
  DeclaredSymbol,
  ExternalSymbol,
  SpecialRegister,
  Unresolved,
};

struct Scope {
  ScopeId id;
  ScopeKind kind{};
  std::optional<ScopeId> parent;
  std::optional<SymbolId> owner;
  std::optional<SourceRange> range;
};

struct Symbol {
  SymbolId id;
  ScopeId scope;
  SymbolKind kind{};
  std::string name;
  SourceRange declaration_range;
  SymbolLinkage linkage{};
  /** Semantic declaration state space; syntax aliases remain source-compatible. */
  std::optional<base::DeclarationStateSpace> state_space;
  std::optional<std::string> type;
  std::optional<uint8_t> vector_width;
  /** Guaranteed byte alignment for an address of this data declaration. */
  std::optional<uint64_t> address_alignment;
  std::optional<uint32_t> parameterized_count;
  std::optional<ScopeId> owned_scope;
  /** Meaningful only for ``Function`` symbols. */
  bool function_is_entry{};
  /** A same-module `.alias` target; absent for ordinary functions. */
  std::optional<SymbolId> canonical_function;
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
  ReferenceClassification classification = ReferenceClassification::Unresolved;
  std::optional<SymbolLookup> target;
};

/** One lexical declaration occurrence for a stable symbol identity. */
struct SymbolDeclarationOccurrence {
  /** Stable identity shared by compatible redeclarations. */
  SymbolId symbol;
  /** Declaration spelling location retained for diagnostics and association. */
  SourceRange range;
  /** Source traversal position; absent when the source location is ambiguous. */
  std::optional<uint32_t> lexical_order;
};

enum class BindDiagnosticKind : uint8_t {
  DuplicateSymbol,
  InvalidParameterizedCount,
  InvalidDebugFileId,
  ConflictingLinkageQualifiers,
  UnresolvedReference,
  InvalidReferenceTarget,
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

  /** Return the lexical child block identified by its parent and AST range. */
  [[nodiscard]] std::optional<ScopeId> blockScope(ScopeId parent,
                                                  SourceRange range) const;

  /** Return the function scope whose declaration occupies ``range``. */
  [[nodiscard]] std::optional<ScopeId> functionScope(SourceRange range) const;

  /** Look up an exact or parameterized name, walking parent scopes. */
  [[nodiscard]] std::optional<SymbolLookup> lookup(ScopeId scope,
                                                   std::string_view name) const;

  /**
   * Return the first non-metadata declaration with this exact spelling and
   * parameterized form in ``scope``.
   *
   * This does not interpret generated parameterized members and never walks a
   * parent scope.  It is therefore suitable for associating an AST declarator
   * with its bound exact declaration identity.  Legal redeclarations retain
   * the first stable identity.
   */
  [[nodiscard]] std::optional<SymbolId> exactDeclaration(
      ScopeId scope, std::string_view name, bool parameterized) const;

  /**
   * Return the first initializer reference recorded at ``range``.
   *
   * The returned reference may be unresolved.  Its pointer remains valid until
   * this table is moved, assigned, or destroyed.  Other reference kinds are
   * deliberately excluded.
   */
  [[nodiscard]] const SymbolReference* initializerReference(
      SourceRange range) const noexcept;

  /** Return the brx.idx target-set reference recorded at ``range``, if any. */
  [[nodiscard]] const SymbolReference* branchTargetSetReference(
      SourceRange range) const noexcept;

  /**
   * Return whether a declaration occurrence for ``symbol`` precedes ``use``.
   *
   * ``nullopt`` means the supplied source has no unambiguous lexical ordering
   * for ``use``.  This deliberately keeps occurrence visibility separate from
   * the symbol's canonical identity.
   */
  [[nodiscard]] std::optional<bool> hasPriorDeclaration(
      SymbolId symbol, SourceRange use) const noexcept;

 private:
  friend struct SymbolTableBuilder;

  /** Transparent owned-string hash supporting allocation-free string-view probes. */
  struct StringViewHash {
    using is_transparent = void;
    /** Hash a string-like spelling without requiring an owned temporary. */
    [[nodiscard]] size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view>{}(value);
    }
  };

  /** Transparent spelling equality for owned index keys and lookup views. */
  struct StringViewEqual {
    using is_transparent = void;
    /** Compare two string-like spellings without copying either operand. */
    [[nodiscard]] bool operator()(std::string_view left,
                                  std::string_view right) const noexcept {
      return left == right;
    }
  };

  /** Hash a source range for the initializer-reference occurrence index. */
  struct SourceRangeHash {
    /** Return a stable hash of both endpoints of a source range. */
    [[nodiscard]] size_t operator()(const SourceRange& range) const noexcept;
  };

  /** A compact binary trie node for a range of decimal member indices. */
  struct NumericMemberIndexNode {
    /** Children selected by the corresponding bit of a 32-bit member index. */
    std::array<std::optional<uint32_t>, 2> children;
    /** Lowest inserted SymbolId reachable from this node, if any. */
    std::optional<SymbolId> earliest_symbol;
  };

  /** Owns the sparse binary trie used to find members below a group count. */
  struct NumericMemberIndex {
    /** Root-first trie storage; vector relocation does not invalidate node IDs. */
    std::vector<NumericMemberIndexNode> nodes{1};
  };

  /** A character-trie node identifying parameterized bases that share a prefix. */
  struct ParameterizedPrefixNode {
    /** Owned outgoing edges, indexed by the next spelling character. */
    std::unordered_map<char, uint32_t> children;
    /** Parameterized declaration whose base ends at this node, if present. */
    std::optional<SymbolId> symbol;
  };

  /** Owns the parameterized-base prefix trie for one lexical scope. */
  struct ParameterizedPrefixIndex {
    /** Root-first trie storage; edges refer to stable numeric node IDs. */
    std::vector<ParameterizedPrefixNode> nodes{1};
  };

  /** All owned lookup and overlap indexes for a single lexical scope. */
  struct ScopeNameIndex {
    /** Exact ordinary declaration spelling to its first stable identity. */
    std::unordered_map<std::string, SymbolId, StringViewHash, StringViewEqual>
        ordinary_exact;
    /** Exact parameterized base spelling to its first stable identity. */
    std::unordered_map<std::string, SymbolId, StringViewHash, StringViewEqual>
        parameterized_exact;
    /** Prefix index of compact parameterized declaration bases. */
    ParameterizedPrefixIndex parameterized_prefixes;
    /** Member-spelling base to sparse numeric range index. */
    std::unordered_map<std::string, NumericMemberIndex, StringViewHash,
                       StringViewEqual>
        member_prefixes;
  };

  /** Retain the lower stable identity while combining index candidates. */
  static void keepEarliest(std::optional<SymbolId>& destination,
                           SymbolId candidate);
  /** Insert one concrete member number into a compact range index. */
  static void indexMemberNumber(NumericMemberIndex& index, uint32_t member,
                                SymbolId symbol);
  /** Return the first identity associated with a member below ``limit``. */
  [[nodiscard]] static std::optional<SymbolId> earliestMemberBelow(
      const NumericMemberIndex& index, uint32_t limit);
  /** Insert one parameterized declaration base into its prefix index. */
  static void indexParameterizedBase(ParameterizedPrefixIndex& index,
                                     std::string_view base, SymbolId symbol);
  /** Find the earliest compact group whose generated member matches spelling. */
  [[nodiscard]] static std::optional<SymbolId> parameterizedContaining(
      const ParameterizedPrefixIndex& index, const std::vector<Symbol>& symbols,
      std::string_view spelling);
  /** Index every valid base/member decomposition of one actual member spelling. */
  static void indexMemberSpelling(ScopeNameIndex& index,
                                  std::string_view spelling, SymbolId symbol);

  std::vector<Scope> scopes_;
  std::vector<Symbol> symbols_;
  std::vector<SymbolReference> references_;
  /** Declaration occurrences grouped by stable SymbolId. */
  std::vector<std::vector<SymbolDeclarationOccurrence>>
      declaration_occurrences_;
  /** Unambiguous lexical source order keyed by exact syntax range. */
  std::unordered_map<SourceRange, uint32_t, SourceRangeHash> source_orders_;
  /** First initializer reference by exact source range; values index references_. */
  std::unordered_map<SourceRange, size_t, SourceRangeHash>
      initializer_reference_indexes_;
  /** Exact brx.idx target-set reference ranges; values index references_. */
  std::unordered_map<SourceRange, size_t, SourceRangeHash>
      branch_target_set_reference_indexes_;
  /** Scope-aligned owned accelerators; never borrow Symbol::name storage. */
  std::vector<ScopeNameIndex> scope_name_indexes_;
};

struct SymbolBinding {
  SymbolTable table;
  std::vector<BindDiagnostic> diagnostics;
};

/** Collect declarations and lexically bind references in a Syntax AST module. */
[[nodiscard]] SymbolBinding bindSymbols(const syntax_ast::AstModule& module);

/** Return whether a spelling is a predefined PTX special register. */
[[nodiscard]] bool isSpecialRegister(std::string_view spelling) noexcept;

}  // namespace ptx_frontend::binding
