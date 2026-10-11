#include "ptx_call_tables.hpp"

#include <algorithm>
#include <tuple>

namespace ptx_frontend::resolved_ir::detail {
namespace {
/** Require a nonempty positive range inside its source owner. */
bool inside(SourceRange child, SourceRange parent) {
  const auto key = [](SourcePos p) {
    return std::tuple{p.line, p.column};
  };
  return child.start.line > 0 && child.start.column > 0 &&
         key(parent.start) <= key(child.start) &&
         key(child.start) < key(child.end) && key(child.end) <= key(parent.end);
}
}  // namespace

std::expected<ResolvedCallTableContract, std::string> build_call_table(
    const ResolvedStorageDeclaration& storage,
    const binding::SymbolTable& symbols,
    const CallTableSignatures& signatures) {
  const auto fail = [](std::string message)
      -> std::expected<ResolvedCallTableContract, std::string> {
    return std::unexpected(std::move(message));
  };
  if (storage.symbol_id.value >= symbols.symbols().size())
    return fail("Call table has no actual storage identity.");
  const auto& symbol = symbols.symbol(storage.symbol_id);
  const auto* type = std::get_if<base::ScalarType>(&storage.element_type);
  if (!type ||
      (*type != base::ScalarType::U32 && *type != base::ScalarType::U64) ||
      (storage.space != StorageSpace::Global &&
       storage.space != StorageSpace::Constant) ||
      storage.vector_width != 1 || storage.array_extents.size() != 1 ||
      !storage.array_extents.front() || *storage.array_extents.front() == 0 ||
      storage.parameterized_count ||
      storage.declaration_kind != StorageDeclarationKind::Definition ||
      storage.initialization != StorageInitializationKind::Explicit ||
      storage.initializer.empty())
    return fail(
        "Call table requires initialized one-dimensional scalar .u32/.u64 "
        "global or const storage.");
  const uint64_t stride = *type == base::ScalarType::U32 ? 4 : 8;
  const auto expected_space = storage.space == StorageSpace::Global
                                  ? base::DeclarationStateSpace::Global
                                  : base::DeclarationStateSpace::Constant;
  if (symbol.kind != binding::SymbolKind::Variable ||
      symbol.scope != storage.scope_id ||
      symbol.state_space != expected_space ||
      symbol.type != (*type == base::ScalarType::U32 ? ".u32" : ".u64") ||
      symbol.vector_width || symbol.parameterized_count ||
      !symbols.hasVariableDeclaration(symbol.id, storage.range) ||
      *storage.array_extents.front() > UINT64_MAX / stride ||
      storage.byte_extent != *storage.array_extents.front() * stride)
    return fail(
        "Call table storage shape or binding provenance is inconsistent.");
  std::optional<binding::SymbolId> owner;
  auto scope = std::optional{storage.scope_id};
  while (scope) {
    if (scope->value >= symbols.scopes().size())
      return fail("Call table has an invalid lexical scope.");
    const auto& current = symbols.scope(*scope);
    if (current.kind == binding::ScopeKind::Function) {
      owner = current.owner;
      break;
    }
    scope = current.parent;
  }
  if (storage.owner_function != owner)
    return fail("Call table storage owner does not match its lexical scope.");
  ResolvedCallTableContract table{.symbol_id = symbol.id,
                                  .scope_id = symbol.scope,
                                  .owner_function = owner,
                                  .space = storage.space,
                                  .element_type = *type,
                                  .extent = *storage.array_extents.front(),
                                  .byte_extent = *storage.byte_extent,
                                  .name = symbol.name,
                                  .range = storage.range};
  std::vector<SourceRange> targets;
  for (size_t index = 0; index < storage.initializer.size(); ++index) {
    const auto& entry = storage.initializer[index];
    const auto* relocation = std::get_if<StorageRelocation>(&entry.value);
    if (!relocation ||
        relocation->address_kind != StorageAddressKind::Function ||
        relocation->addend_bits != 0 || relocation->byte_mask ||
        relocation->parameterized_index ||
        relocation->symbol_id.value >= symbols.symbols().size() ||
        entry.byte_offset != index * stride ||
        index >= *storage.array_extents.front() ||
        !inside(entry.range, storage.range))
      return fail(
          "Call table entries must be ordered complete unmasked zero-addend "
          "device-function relocations.");
    const auto& target = symbols.symbol(relocation->symbol_id);
    const auto canonical = target.canonical_function.value_or(target.id);
    if (target.kind != binding::SymbolKind::Function ||
        target.function_is_entry ||
        canonical.value >= symbols.symbols().size() ||
        symbols.symbol(canonical).kind != binding::SymbolKind::Function ||
        symbols.symbol(canonical).function_is_entry)
      return fail("Call table entry does not name a device function.");
    const binding::SymbolReference* reference =
        symbols.initializerReference(relocation->target_range);
    if (!reference || !reference->target ||
        !inside(reference->range, entry.range) ||
        reference->scope != storage.scope_id ||
        reference->target->symbol != target.id ||
        reference->target->parameterized_index ||
        symbols.hasPriorDeclaration(target.id, reference->range) != true)
      return fail(
          "Call table entry requires its exact previously declared function "
          "occurrence.");
    if (std::ranges::find(targets, reference->range) != targets.end())
      return fail("Call table entries cannot reuse another initializer token.");
    targets.push_back(reference->range);
    const auto signature = signatures.find(canonical.value);
    if (signature == signatures.end() || signature->second.is_entry)
      return fail(
          "Call table entry has no canonical device-function signature.");
    if (index == 0)
      table.signature = signature->second;
    else if (table.signature != signature->second)
      return fail(
          "Call table targets require identical full function signatures.");
    table.slots.push_back({.index = index,
                           .byte_offset = entry.byte_offset,
                           .symbol_id = target.id,
                           .canonical_function = canonical,
                           .range = entry.range,
                           .target_range = reference->range});
  }
  size_t source_entries = 0;
  for (const auto& reference : symbols.references())
    if (reference.kind == binding::ReferenceKind::Initializer &&
        reference.scope == storage.scope_id &&
        inside(reference.range, storage.range))
      ++source_entries;
  if (source_entries != targets.size())
    return fail(
        "Call table slots do not cover the explicit source initializer "
        "references.");
  return table;
}
}  // namespace ptx_frontend::resolved_ir::detail
