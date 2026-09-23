#pragma once

#include <expected>
#include <string>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_module_snapshot.hpp"

namespace ptx_frontend::resolved_ir::test_support {

/** Which existing module pipeline a typed test projection runs. */
enum class ModulePipeline { ResolveOnly, AvailableContext, CompleteContext };

/** An owned function with only the instruction families requested by a test. */
template <PtxOperator... Instructions>
struct TypedFunctionSnapshot {
  /** Identity into TypedModuleSnapshot::symbols. */
  binding::SymbolId symbol_id;
  /** Source function name retained for assertion output. */
  std::string name;
  /** Instructions in source order; monostate marks an unrequested family. */
  std::vector<std::variant<std::monostate, Instructions...>> body;
  /** Owned source ranges parallel to body. */
  std::vector<SourceRange> instruction_ranges;
};

/** A typed test view of full module resolution. */
template <PtxOperator... Instructions>
struct TypedModuleSnapshot {
  /** Owned binding table for tests of resolved operand identities. */
  binding::SymbolTable symbols;
  /** Functions and prototypes in source order. */
  std::vector<TypedFunctionSnapshot<Instructions...>> functions;
  /** Storage facts independent of the global instruction variant. */
  std::vector<StorageSnapshot> storage_declarations;
};

/** Resolve through the full implementation, then copy selected typed families. */
template <PtxOperator... Instructions>
std::expected<TypedModuleSnapshot<Instructions...>,
              std::vector<ResolveDiagnostic>>
resolveTypedModule(const syntax_ast::AstModule& ast, ModulePipeline pipeline);

}  // namespace ptx_frontend::resolved_ir::test_support
