#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_unified_id.hpp>
#include <ptx_frontend/semantic/ptx_function_contract.hpp>
#include "resolved_ir.gen.hpp"

namespace ptx_frontend::resolved_ir {

/** Records whether an effective source-header value was explicit or inferred. */
enum class SourceConfigurationProvenance : uint8_t {
  Missing,
  Explicit,
  Defaulted
};

/** One ordered source target region; it never implies a physical target. */
struct ResolvedSourceTargetRegion {
  /** Range covered by this configuration until the next target directive. */
  SourceRange range;
  /** Effective PTX language version, if the source supplied a valid one. */
  std::optional<checker::PtxVersion> version;
  /** All source target option spellings in source order. */
  std::vector<std::string> target_options;
  /** Effective PTX address width; an omitted directive owns the default 32 bits. */
  std::optional<uint32_t> address_size_bits;
  /** Provenance of the effective version, target, and address-width values. */
  SourceConfigurationProvenance version_provenance{};
  SourceConfigurationProvenance target_provenance{};
  SourceConfigurationProvenance address_size_provenance{};
};

/** Owned normalized source configuration retained independently of the AST. */
struct ResolvedModuleHeader {
  /** Ordered effective configurations; functions retain an index into this list. */
  std::vector<ResolvedSourceTargetRegion> regions;
  /** Source directives with invalid, duplicate, or conflicting forms. */
  std::vector<SourceRange> invalid_directives;
};

/** Enumerates the supported source-level launch resource contracts. */
enum class ResolvedKernelResourceKind : uint8_t {
  MaxNreg,
  MaxNtid,
  ReqNtid,
  MinNctaPerSm,
  ReqNctaPerCluster,
  ExplicitCluster,
  MaxClusterRank,
};

/** Owned source-level resource directive, not a physical allocation decision. */
struct ResolvedKernelResourceContract {
  /** Resource category independent of the syntax AST. */
  ResolvedKernelResourceKind kind{};
  /**
   * Normalized numeric dimensions. A resource accepting one to three source
   * dimensions owns three values, with omitted trailing dimensions inferred as
   * one; ``explicit_value_count`` preserves whether that inference occurred.
   */
  std::vector<uint32_t> values;
  /** Number of dimensions actually present in source before normalization. */
  uint8_t explicit_value_count{};
  /** Source range of this directive for diagnostics and provenance. */
  SourceRange range;
};

/** One source-level ABI preservation suffix attached to a function contract. */
struct ResolvedAbiPreservationContract {
  /** Number of preserved registers required by the source contract. */
  uint32_t count{};
  /** Whether this is the control-register variant of .abi_preserve. */
  bool control_registers{};
  /** Source provenance of the suffix. */
  SourceRange range;
  /** Compare the complete owned ABI suffix contract. */
  bool operator==(const ResolvedAbiPreservationContract&) const = default;
};

/** Supported function attribute categories independent of syntax-AST enums. */
enum class ResolvedFunctionAttributeKind : uint8_t { Managed, Unified };

/** One owned function attribute retained for source-profile validation. */
struct ResolvedFunctionAttribute {
  /** Semantic attribute category. */
  ResolvedFunctionAttributeKind kind{};
  /**
   * `.unified` UUID halves in PTX operand order. No byte order or host-address
   * interpretation is applied.
   * A finalized attribute with kind Unified must provide this payload.
   */
  std::optional<ResolvedUnifiedId> unified_id;
  /** Source provenance of this attribute. */
  SourceRange range;
};

/** One expanded logical branch target retained independently of compact syntax. */
struct ResolvedBranchTargetContract {
  /** Bound label identity in the enclosing function scope. */
  binding::SymbolId symbol_id;
  /** Owned label spelling for a consumer-facing diagnostic. */
  std::string name;
  /** Source range of the explicit or compact source entry that selected it. */
  SourceRange range;
};

/** An ordered .branchtargets declaration associated with one metadata label. */
struct ResolvedBranchTargetSetContract {
  /** Bound declaration identity used by indirect branch operands. */
  binding::SymbolId symbol_id;
  /** Owning lexical scope for the metadata label and targets. */
  binding::ScopeId scope_id;
  /** Owned metadata-label spelling. */
  std::string name;
  /** Expanded logical target sequence in declared source order. */
  std::vector<ResolvedBranchTargetContract> targets;
  /** Source provenance of the complete declaration. */
  SourceRange range;
};

/** One bound target retained from a .calltargets declaration. */
struct ResolvedCallTargetContract {
  /** Bound declaration identity named by this target entry. */
  binding::SymbolId symbol_id;
  /** Canonical function identity used for signature equivalence. */
  binding::SymbolId canonical_function;
  /** Owned source spelling of the referenced function. */
  std::string name;
  /** Source provenance of the target entry. */
  SourceRange range;
};

/** An ordered .calltargets declaration associated with one metadata label. */
struct ResolvedCallTargetSetContract {
  /** Bound declaration identity used by indirect call metadata operands. */
  binding::SymbolId symbol_id;
  /** Owning lexical scope for the metadata label and targets. */
  binding::ScopeId scope_id;
  /** Owned metadata-label spelling. */
  std::string name;
  /** Bound function targets in declared source order. */
  std::vector<ResolvedCallTargetContract> targets;
  /** Canonical signature shared by the admitted target set. */
  declaration_semantics::FunctionSignature signature;
  /** Source provenance of the complete declaration. */
  SourceRange range;
};

/** An owned .callprototype contract associated with a bound metadata label. */
struct ResolvedCallPrototypeContract {
  /** Bound declaration identity used by indirect call metadata operands. */
  binding::SymbolId symbol_id;
  /** Owning lexical scope for this metadata label. */
  binding::ScopeId scope_id;
  /** Owned metadata-label spelling. */
  std::string name;
  /** Normalized source ABI shared with normal function signatures. */
  declaration_semantics::FunctionSignature signature;
  /** True when this prototype declares .noreturn. */
  bool is_noreturn{};
  /** Optional .abi_preserve suffix retained as source ABI, not allocation. */
  std::optional<ResolvedAbiPreservationContract> abi_preserve;
  /** Optional .abi_preserve_control suffix retained as source ABI. */
  std::optional<ResolvedAbiPreservationContract> abi_preserve_control;
  /** Source provenance of the complete declaration. */
  SourceRange range;
};

/** Owned resource and ABI attributes attached to one resolved function. */
struct ResolvedFunctionContract {
  /** Canonical source ABI; parameter identities remain in parameter_declarations. */
  declaration_semantics::FunctionSignature signature;
  /** Function linkage preserved from the owning module symbol. */
  binding::SymbolLinkage linkage{};
  /** Function attributes whose availability is part of the source contract. */
  std::vector<ResolvedFunctionAttribute> attributes;
  /** Canonical declaration identity; aliases and compatible declarations share it. */
  binding::SymbolId canonical_function;
  /** True only for a declaration that explicitly carries .noreturn. */
  bool is_noreturn{};
  /** Source-level ABI register preservation; no physical register assignment. */
  std::optional<ResolvedAbiPreservationContract> abi_preserve;
  /** Source-level ABI control-register preservation contract. */
  std::optional<ResolvedAbiPreservationContract> abi_preserve_control;
  /** Source resource contracts, including multidimensional cluster values. */
  std::vector<ResolvedKernelResourceContract> resources;
  /** True when this entry uses `.blocksareclusters` launch semantics. */
  bool blocks_are_clusters{};
  /** Ordered `.language` operands; absent means the directive was not supplied. */
  std::optional<std::vector<std::string>> language_values;
};

/** Records a module alias without making consumers inspect syntax directives. */
struct ResolvedFunctionAlias {
  /** Bound alias declaration identity. */
  binding::SymbolId symbol_id;
  /** Canonical function reached through this alias. */
  binding::SymbolId canonical_function;
  /** Owned alias spelling. */
  std::string name;
  /** Owned aliasee spelling. */
  std::string aliasee_name;
  /** Source provenance of the alias directive. */
  SourceRange range;
  /** Source configuration active at this directive, if one was modeled. */
  std::optional<std::size_t> source_region;
};

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
  /** Canonical ABI/resource data; it deliberately does not duplicate identities. */
  ResolvedFunctionContract contract;
  /** Function-local indirect-branch metadata in lexical source order. */
  std::vector<ResolvedBranchTargetSetContract> branch_target_sets;
  /** Function-local indirect-call target metadata in lexical source order. */
  std::vector<ResolvedCallTargetSetContract> call_target_sets;
  /** Function-local indirect-call prototype metadata in lexical source order. */
  std::vector<ResolvedCallPrototypeContract> call_prototypes;
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
  /** Index of the effective ordered source configuration in ResolvedModule::header. */
  std::optional<std::size_t> source_region;
  /** Location-independent owned function identity used for source retargeting. */
  std::string source_identity;
};

/** Owns all resolved declarations, functions, and source correspondence data. */
struct ResolvedModule {
  /** Owning symbol table; every retained SymbolId and ScopeId refers to it. */
  binding::SymbolTable symbols;
  /** Owned functions and prototypes in source traversal order. */
  std::vector<ResolvedFunction> functions;
  /** Function aliases in source order; aliases do not duplicate function bodies. */
  std::vector<ResolvedFunctionAlias> function_aliases;
  /** Owned ordered source contexts used for AST-free source availability checks. */
  ResolvedModuleHeader header;
  /** Source extent of the complete module. */
  SourceRange range;
  /** Owned non-parameter storage declarations, in source traversal order. */
  std::vector<ResolvedStorageDeclaration> storage_declarations;
  /** Canonical module syntax identity excluding target, version, and locations. */
  std::string source_identity;
};

}  // namespace ptx_frontend::resolved_ir
