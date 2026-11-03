#pragma once

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace ptx_frontend::resolved_ir::detail {
/** One actual call participating in a known direct or finite-candidate cycle. */
struct RecursiveCall {
  /** Current instruction extent, independent of producing syntax lifetime. */
  SourceRange range;
  /** True when this cycle witness depends on at least one finite candidate edge. */
  bool possible{};
};
/** Per-validation graph result; no graph or source borrow is retained by the module. */
struct CallGraphAnalysis {
  /** Participating calls indexed by actual owning function occurrence. */
  std::vector<std::vector<RecursiveCall>> recursive_calls;
  /** External/prototype/dynamic boundaries preclude whole-program acyclicity claims. */
  bool has_open_boundaries{};
  /** Invalid current joins cannot be manufactured into recursion witnesses. */
  checker::CheckDiagnostics diagnostics;
};
namespace call_graph_detail {
/** A typed edge between compact local-definition vertices. */
struct Edge {
  /** Compact caller and callee definition indices. */
  size_t from{}, to{};
  /** Actual current call extent and finite-alternative qualification. */
  SourceRange range;
  bool candidate{};
};
/** Compute SCC identities with iterative DFS in O(V+E), optionally using exact edges only. */
inline std::vector<size_t> components(size_t count,
                                      const std::vector<Edge>& edges,
                                      bool direct_only) {
  std::vector<std::vector<size_t>> forward(count), reverse(count);
  for (const auto& edge : edges)
    if (!direct_only || !edge.candidate) {
      forward[edge.from].push_back(edge.to);
      reverse[edge.to].push_back(edge.from);
    }
  std::vector<bool> seen(count);
  std::vector<size_t> order;
  for (size_t root = 0; root < count; ++root) {
    if (seen[root])
      continue;
    /** Each frame owns a vertex and its next outgoing edge cursor. */
    std::vector<std::pair<size_t, size_t>> stack{{root, 0}};
    seen[root] = true;
    while (!stack.empty()) {
      auto& [node, next] = stack.back();
      if (next < forward[node].size()) {
        const auto child = forward[node][next++];
        if (!seen[child]) {
          seen[child] = true;
          stack.emplace_back(child, 0);
        }
      } else {
        order.push_back(node);
        stack.pop_back();
      }
    }
  }
  const auto missing = std::numeric_limits<size_t>::max();
  std::vector<size_t> result(count, missing);
  size_t identity = 0;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (result[*it] != missing)
      continue;
    std::vector<size_t> stack{*it};
    result[*it] = identity;
    while (!stack.empty()) {
      const auto node = stack.back();
      stack.pop_back();
      for (const auto child : reverse[node])
        if (result[child] == missing) {
          result[child] = identity;
          stack.push_back(child);
        }
    }
    ++identity;
  }
  return result;
}
/** Copy selected call identities; register carriers do not themselves enumerate callees. */
struct CallUse final : IReferenceObserver {
  /** Actual direct target, if this is a selected direct layout. */
  std::optional<binding::SymbolId> direct;
  /** A finite flist identity, or a signature-only prototype identity. */
  std::optional<binding::SymbolId> metadata;
  /** Layout counts distinguish invalid combinations from open indirect calls. */
  size_t direct_count{};
  bool indirect{};
  /** Indexed actual flist use; declaration-member positions are not lexical-order events. */
  std::optional<SourceRange> metadata_use;
  /** Capture a device-function identity without a borrowed callback value. */
  void function_ref(const ResolvedFunctionRef& value,
                    std::span<const SourceRange>,
                    checker::AddressSymbolResolutionPolicy) override {
    ++direct_count;
    direct = value.symbol_id;
  }
  /** Capture actual metadata/table branches; ordinary target registers stay open unless a flist exists. */
  void indirect_callee(const ResolvedIndirectCallee& value,
                       std::span<const SourceRange> locations,
                       checker::AddressSymbolResolutionPolicy) override {
    indirect = true;
    if (const auto* ref = std::get_if<ResolvedIndirectMetadataRef>(&value)) {
      metadata = ref->symbol_id;
      if (locations.size() == 1)
        metadata_use = locations.front();
    } else if (const auto* ref = std::get_if<ResolvedCallTableRef>(&value)) {
      metadata = ref->symbol_id;
      if (locations.size() == 1)
        metadata_use = locations.front();
    }
  }
};
/** Known physical member ranges lie inside their owning metadata declaration. */
inline bool inside(SourceRange child, SourceRange owner) {
  const auto key = [](SourcePos p) {
    return std::tuple{p.line, p.column};
  };
  return child.start.line > 0 && child.start.column > 0 &&
         key(owner.start) <= key(child.start) &&
         key(child.start) < key(child.end) && key(child.end) <= key(owner.end);
}
}  // namespace call_graph_detail

/** Analyze current typed calls after normal identity/ABI/table validation; SCC work is linear. */
inline CallGraphAnalysis analyze_call_cycles(const ResolvedModule& module) {
  CallGraphAnalysis result;
  result.recursive_calls.resize(module.functions.size());
  const auto report = [&](SourceRange range, std::string message) {
    result.diagnostics.push_back(
        {.kind = checker::CheckDiagnosticKind::ModuleSourceMismatch,
         .range = range,
         .message = std::move(message)});
  };
  const auto canonical =
      [&](binding::SymbolId id) -> std::optional<binding::SymbolId> {
    if (id.value >= module.symbols.symbols().size())
      return std::nullopt;
    const auto& symbol = module.symbols.symbol(id);
    const auto key = symbol.canonical_function.value_or(symbol.id);
    if (symbol.id != id || symbol.kind != binding::SymbolKind::Function ||
        symbol.function_is_entry ||
        key.value >= module.symbols.symbols().size() ||
        module.symbols.symbol(key).kind != binding::SymbolKind::Function ||
        module.symbols.symbol(key).function_is_entry)
      return std::nullopt;
    return key;
  };
  /** Canonical identities map only actual local non-entry definitions to vertices. */
  std::unordered_map<uint32_t, size_t> definitions;
  std::vector<size_t> owners;
  std::unordered_map<uint32_t, const declaration_semantics::FunctionSignature*>
      signatures;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    const auto& function = module.functions[i];
    if (function.is_entry)
      continue;
    const auto key = canonical(function.symbol_id);
    if (!key || *key != function.contract.canonical_function ||
        function.contract.signature.is_entry) {
      report(function.range,
             "Call graph function has an invalid canonical identity.");
      continue;
    }
    const auto [signature, inserted] =
        signatures.emplace(key->value, &function.contract.signature);
    if (!inserted && *signature->second != function.contract.signature)
      report(function.range, "Call graph canonical signatures disagree.");
    if (!function.is_prototype) {
      if (!definitions.emplace(key->value, owners.size()).second)
        report(
            function.range,
            "Call graph requires one local definition per canonical function.");
      else
        owners.push_back(i);
    }
  }
  /** Metadata is indexed once, but only consumed finite lists are validated and expanded. */
  std::unordered_map<uint32_t, const ResolvedCallTargetSetContract*> lists;
  std::unordered_map<uint32_t, const ResolvedCallTableContract*> tables;
  for (const auto& function : module.functions)
    for (const auto& targets : function.call_target_sets)
      if (!lists.emplace(targets.symbol_id.value, &targets).second)
        lists[targets.symbol_id.value] = nullptr;
  for (const auto& table : module.call_tables)
    tables.emplace(table.symbol_id.value, &table);
  std::unordered_map<uint32_t, std::vector<binding::SymbolId>> finite;
  const auto candidates =
      [&](binding::SymbolId id,
          SourceRange use) -> const std::vector<binding::SymbolId>* {
    if (const auto found = finite.find(id.value); found != finite.end())
      return &found->second;
    std::vector<binding::SymbolId> values;
    if (const auto found = lists.find(id.value); found != lists.end()) {
      if (!found->second) {
        report(use, "Call graph consumed target-list identity is ambiguous.");
        return nullptr;
      }
      const auto& list = *found->second;
      for (const auto& target : list.targets) {
        const auto key = canonical(target.symbol_id);
        const auto lookup =
            module.symbols.lookup(module.symbols.moduleScope(), target.name);
        const auto signature =
            key ? signatures.find(key->value) : signatures.end();
        // Module function declarations cannot interleave within this owning
        // body, so the indexed flist use preserves their prior visibility.
        if (!key || *key != target.canonical_function || !lookup ||
            lookup->symbol != target.symbol_id ||
            signature == signatures.end() ||
            *signature->second != list.signature ||
            !call_graph_detail::inside(target.range, list.range) ||
            module.symbols.hasPriorDeclaration(target.symbol_id, use) != true) {
          report(target.range,
                 "Call graph finite target disagrees with its canonical "
                 "signature or binding.");
          return nullptr;
        }
        values.push_back(*key);
      }
    } else if (const auto found = tables.find(id.value);
               found != tables.end()) {
      for (const auto& slot : found->second->slots) {
        const auto key = canonical(slot.symbol_id);
        const auto signature =
            key ? signatures.find(key->value) : signatures.end();
        if (!key || *key != slot.canonical_function ||
            signature == signatures.end() ||
            *signature->second != found->second->signature) {
          report(slot.range,
                 "Call graph table slot has an invalid canonical signature or "
                 "binding.");
          return nullptr;
        }
        values.push_back(*key);
      }
    } else {
      if (id.value >= module.symbols.symbols().size() ||
          module.symbols.symbol(id).kind != binding::SymbolKind::CallPrototype)
        report(use,
               "Call graph call has no retained finite or prototype contract.");
      return nullptr;
    }
    return &finite.emplace(id.value, std::move(values)).first->second;
  };
  std::vector<call_graph_detail::Edge> edges;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    const auto& function = module.functions[i];
    if (function.is_prototype)
      continue;
    const auto caller =
        definitions.find(function.contract.canonical_function.value);
    const bool local = !function.is_entry && caller != definitions.end();
    for (size_t index = 0; index < function.body.size(); ++index) {
      const auto* call =
          dynamic_cast<const CallDirect*>(function.body[index].get());
      if (!call)
        continue;
      if (index >= function.instruction_ranges.size()) {
        report(function.range,
               "Call graph instruction has no owned source range.");
        continue;
      }
      const auto range = function.instruction_ranges[index];
      call_graph_detail::CallUse use;
      call->visit_references(use);
      const auto add = [&](binding::SymbolId target, bool candidate) {
        const auto found = definitions.find(target.value);
        if (found == definitions.end())
          result.has_open_boundaries = true;
        else if (local)
          edges.push_back({caller->second, found->second, range, candidate});
      };
      if (use.direct_count == 1 && !use.indirect && use.direct) {
        if (const auto target = canonical(*use.direct))
          add(*target, false);
        else
          report(range,
                 "Call graph direct call has an invalid device-function "
                 "identity.");
      } else if (use.direct_count == 0 && use.indirect) {
        if (use.metadata) {
          if (const auto* targets =
                  candidates(*use.metadata, use.metadata_use.value_or(range)))
            for (const auto target : *targets)
              add(target, true);
          else
            result.has_open_boundaries = true;
        } else
          result.has_open_boundaries = true;
      } else
        report(range, "Call graph call has no valid selected target layout.");
    }
  }
  if (!result.diagnostics.empty())
    return result;
  const auto all = call_graph_detail::components(owners.size(), edges, false);
  const auto direct = call_graph_detail::components(owners.size(), edges, true);
  for (const auto& edge : edges)
    if (all[edge.from] == all[edge.to])
      result.recursive_calls[owners[edge.from]].push_back(
          {edge.range, edge.candidate || direct[edge.from] != direct[edge.to]});
  return result;
}
}  // namespace ptx_frontend::resolved_ir::detail
