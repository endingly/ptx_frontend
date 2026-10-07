#pragma once

#include <expected>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>

namespace ptx_frontend::resolved_ir::test_support {

/** Facts needed by module acceptance tests without exposing instruction variants. */
struct FunctionSnapshot {
  /** Identity into ModuleSnapshot::symbols. */
  binding::SymbolId symbol_id;
  /** Owned function name for a useful assertion failure. */
  std::string name;
  /** Whether this declaration has no body. */
  bool is_prototype{};
  /** Number of resolved instructions in source order. */
  std::size_t instruction_count{};
  /** Complete owned formal declaration metadata, independent of instructions. */
  std::vector<ResolvedParameterDeclaration> parameter_declarations;
};

/** Minimal owned storage facts asserted by module corpus tests. */
struct StorageSnapshot {
  /** Bound declaration spelling. */
  std::string name;
  /** Number of flattened initializer values. */
  std::size_t initializer_count{};
  /** Bits of the first scalar constant, when that is its initializer. */
  std::optional<uint64_t> first_constant_bits;
};

/** Owned projection of a fully resolved module for test assertions. */
struct ModuleSnapshot {
  /** Owned binding table for assertions about declaration identities. */
  binding::SymbolTable symbols;
  /** Functions and prototypes in source order. */
  std::vector<FunctionSnapshot> functions;
  /** Storage declarations in source order. */
  std::vector<StorageSnapshot> storage_declarations;
  /** Complete owned declaration records for metadata contract tests. */
  std::vector<ResolvedStorageDeclaration> storage_metadata;
};

/** Module mutations whose complete-context validation is exercised after AST release. */
enum class OwnedMutationScenario {
  DivSourceWidth,
  AbsSourceWidth,
  RcpSourceWidth
};

/** Full-module checker results before and after one owned-IR mutation. */
struct OwnedMutationCheck {
  /** Validation before mutation, after parser and AST destruction. */
  checker::CheckResult before;
  /** Validation after mutating one bound source to an incompatible width. */
  checker::CheckResult after;
};

/** Parse, resolve, release source ownership, and revalidate one test mutation. */
std::expected<OwnedMutationCheck, std::vector<ResolveDiagnostic>>
checkOwnedModuleMutation(std::string source, OwnedMutationScenario scenario);

/** Resolve with available source context and project only stable test facts. */
std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveModuleSnapshot(const syntax_ast::AstModule& ast);

/** Resolve and check with complete source context before projecting test facts. */
std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndValidateModuleSnapshot(const syntax_ast::AstModule& ast);

/** Preserve resolution and checker stages for tests of module validation. */
std::expected<checker::CheckResult, std::vector<ResolveDiagnostic>>
resolveOnlyAndCheckModule(const syntax_ast::AstModule& ast);

/** Check available source context after resolution, then project test facts. */
std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndCheckAvailableModuleSnapshot(const syntax_ast::AstModule& ast);

/** Resolve a module and check each instruction at an explicit target. */
std::expected<ModuleSnapshot, std::vector<ResolveDiagnostic>>
resolveAndCheckInstructionSnapshot(const syntax_ast::AstModule& ast,
                                   const checker::Context& context);

/** Result of validating owned IR against a different source configuration. */
struct RetargetedAvailability {
  /** Checker result from the second source configuration. */
  checker::CheckResult checked;
  /** Original instruction range retained by the owned module. */
  SourceRange original_instruction_range;
};

/** Resolve one source and check it against a second source configuration. */
std::expected<RetargetedAvailability, std::vector<ResolveDiagnostic>>
checkRetargetedModuleAvailability(const syntax_ast::AstModule& original,
                                  const syntax_ast::AstModule& retargeted);

/** Checker results around one public unified-load flag mutation. */
struct UnifiedLoadMutationCheck {
  /** Validation of the resolved module before mutation. */
  checker::CheckResult before;
  /** Validation after clearing the first load's unified address flag. */
  checker::CheckResult after;
};

/** Exercise owned module revalidation of a unified load's mutable flag. */
std::expected<UnifiedLoadMutationCheck, std::vector<ResolveDiagnostic>>
checkUnifiedLoadMutation(const syntax_ast::AstModule& ast);

}  // namespace ptx_frontend::resolved_ir::test_support
