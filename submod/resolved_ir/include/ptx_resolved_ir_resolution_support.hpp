#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_diagnostics.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_ast.hpp>

namespace ptx_frontend::resolved_ir {

struct ResolvedCallTableContract;

/** Declaration context used while resolving an instruction inside a module. */
struct ResolveContext {
  /** Borrowed lexical symbol table; caller keeps it alive through resolution. */
  const binding::SymbolTable& symbols;
  /** Lexical scope of the instruction being resolved. */
  binding::ScopeId scope;
  /** Containing function scope, absent for standalone resolution. */
  std::optional<binding::ScopeId> function_scope;
  /** Whether the containing function is a kernel entry. */
  bool function_is_entry{};
  /** Bound storage identities carrying a PTX `.unified` attribute. */
  std::span<const binding::SymbolId> unified_storage_symbols;
  /** Source-associated module texturing mode for direct resource resolution. */
  TextureMode texture_mode = TextureMode::Unified;
  /** Borrowed owned shapes, valid only throughout this synchronous resolution. */
  std::span<const ResolvedStorageDeclaration> storage_declarations;
  std::span<const ResolvedParameterDeclaration> parameter_declarations;
  /** Validated consumed tables, borrowed only during synchronous module resolution. */
  std::span<const ResolvedCallTableContract> call_tables;
};

/** Preserve the written atomic address suffix after syntax selection. */
WithLocs<AtomicAddressQualifier> atomic_address_qualifier_from_ast(
    const syntax_ast::AstInstruction& ast);

}  // namespace ptx_frontend::resolved_ir
