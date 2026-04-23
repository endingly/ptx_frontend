/*
 * src/symbol_resolver.cpp
 *
 * Symbol resolution and binding for the PTX frontend.
 *
 * Phase 1 – collection
 *   Walk the parsed Module and insert every declared symbol into the
 *   SymbolTable (module-scope for globals/functions; per-function scope
 *   for registers, parameters, and labels).
 *
 * Phase 2 – resolution
 *   Walk every instruction operand and map each Ident to its SymbolId
 *   using the VisitorMap / map_instruction infrastructure already
 *   defined in ptx_visit_dispatch.hpp.
 */

#include "symbol_resolver.hpp"
#include "ptx_visit.hpp"
#include "ptx_visit_dispatch.hpp"

#include <cassert>
#include <string>
#include <variant>

namespace ptx_frontend {

// ============================================================
// SymbolTable implementation
// ============================================================

SymbolId SymbolTable::alloc(std::string name, SymbolKind kind, Type type,
                            StateSpace space, std::string func_scope) {
  auto id = static_cast<SymbolId>(entries_.size());
  entries_.push_back(SymbolEntry{std::move(name), kind, std::move(type), space,
                                 std::move(func_scope)});
  return id;
}

expected<SymbolId, ResolveError> SymbolTable::insert_global(std::string name,
                                                            SymbolKind kind,
                                                            Type type,
                                                            StateSpace space) {
  if (module_scope_.count(name))
    return unexpected(
        ResolveError{"symbol '" + name + "' already declared in module scope"});
  SymbolId id = alloc(name, kind, std::move(type), space, /*func_scope=*/"");
  module_scope_.emplace(std::move(name), id);
  return id;
}

expected<SymbolId, ResolveError> SymbolTable::insert_local(
    const std::string& func_name, std::string name, SymbolKind kind, Type type,
    StateSpace space) {
  auto& local = func_scopes_[func_name];
  if (local.count(name))
    return unexpected(ResolveError{"symbol '" + name +
                                   "' already declared in function '" +
                                   func_name + "'"});
  SymbolId id = alloc(name, kind, std::move(type), space, func_name);
  local.emplace(std::move(name), id);
  return id;
}

expected<SymbolId, ResolveError> SymbolTable::lookup(
    std::string_view name, const std::string& func_name) const {
  // 1. function-local scope
  if (!func_name.empty()) {
    auto fit = func_scopes_.find(func_name);
    if (fit != func_scopes_.end()) {
      auto it = fit->second.find(std::string(name));
      if (it != fit->second.end())
        return it->second;
    }
  }
  // 2. module scope
  auto it = module_scope_.find(std::string(name));
  if (it != module_scope_.end())
    return it->second;

  return unexpected(
      ResolveError{"undefined symbol '" + std::string(name) + "'"});
}

// ============================================================
// Helpers: collect symbols from variable declarations
// ============================================================

namespace {

/// Return the SymbolKind corresponding to a StateSpace.
static SymbolKind kind_for_space(StateSpace ss) {
  switch (ss) {
    case StateSpace::Reg:
      return SymbolKind::Register;
    case StateSpace::Global:
      return SymbolKind::Global;
    case StateSpace::Const:
      return SymbolKind::Const;
    case StateSpace::Local:
      return SymbolKind::Local;
    case StateSpace::Shared:
    case StateSpace::SharedCluster:
      return SymbolKind::Shared;
    case StateSpace::Param:
      return SymbolKind::Param;
    default:
      return SymbolKind::Global;
  }
}

/// Insert all names declared by a MultiVariable into `sym`.
/// For parameterised ranges (%r<N>) we expand %r0 … %r(N-1).
/// Returns any insertion errors.
static void collect_multivariable(SymbolTable& sym, const MultiVariable& mv,
                                  const std::string& func_name,
                                  std::vector<ResolveError>& errs) {
  std::visit(
      [&](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        const VariableInfo& info = v.info;
        SymbolKind kind = kind_for_space(info.state_space);

        if constexpr (std::is_same_v<V, MultiVariableParameterized>) {
          // Expand %r<N> into %r0 … %r(N-1)
          std::string base(v.name);
          for (uint32_t i = 0; i < v.count; ++i) {
            std::string indexed = base + std::to_string(i);
            expected<SymbolId, ResolveError> r;
            if (func_name.empty())
              r = sym.insert_global(indexed, kind, info.v_type,
                                    info.state_space);
            else
              r = sym.insert_local(func_name, indexed, kind, info.v_type,
                                   info.state_space);
            if (!r)
              errs.push_back(std::move(r.error()));
          }
        } else {
          // MultiVariableNames — explicit list
          for (const Ident& nm : v.names) {
            std::string name(nm);
            expected<SymbolId, ResolveError> r;
            if (func_name.empty())
              r = sym.insert_global(name, kind, info.v_type, info.state_space);
            else
              r = sym.insert_local(func_name, name, kind, info.v_type,
                                   info.state_space);
            if (!r)
              errs.push_back(std::move(r.error()));
          }
        }
      },
      mv);
}

// ============================================================
// Phase 1 – collection
// ============================================================

/// Recursively collect all label and variable declarations from a
/// list of statements into the function-local scope of `func_name`.
static void collect_stmts(SymbolTable& sym, const std::string& func_name,
                          const std::vector<Statement<ParsedOp>>& stmts,
                          std::vector<ResolveError>& errs) {
  for (const auto& stmt : stmts) {
    std::visit(
        [&](const auto& s) {
          using S = std::decay_t<decltype(s)>;

          if constexpr (std::is_same_v<S, Statement<ParsedOp>::LabelS>) {
            // label: insert with a placeholder type (Pred is conventional)
            std::string name(s.label);
            if (auto r = sym.insert_local(func_name, name, SymbolKind::Label,
                                          make_scalar(ScalarType::Pred),
                                          StateSpace::Reg);
                !r)
              errs.push_back(std::move(r.error()));
          } else if constexpr (std::is_same_v<S,
                                              Statement<ParsedOp>::VariableS>) {
            collect_multivariable(sym, s.var, func_name, errs);
          } else if constexpr (std::is_same_v<S, Statement<ParsedOp>::BlockS>) {
            collect_stmts(sym, func_name, s.stmts, errs);
          }
          // InstructionS: no new declarations
        },
        stmt.inner);
  }
}

/// Collect all declarations from a Function into module-scope (func name)
/// and function-local scope (params, registers, labels).
static void collect_function(SymbolTable& sym, const std::string& func_name,
                             const Function<ParsedOp>& func,
                             std::vector<ResolveError>& errs) {
  // Return parameters
  for (const auto& param : func.func_directive.return_arguments) {
    std::string name(param.name);
    if (auto r = sym.insert_local(func_name, name, SymbolKind::Param,
                                  param.info.v_type, StateSpace::Param);
        !r)
      errs.push_back(std::move(r.error()));
  }
  // Input parameters
  for (const auto& param : func.func_directive.input_arguments) {
    std::string name(param.name);
    if (auto r = sym.insert_local(func_name, name, SymbolKind::Param,
                                  param.info.v_type, StateSpace::Param);
        !r)
      errs.push_back(std::move(r.error()));
  }
  // Optional shared-memory parameter (.entry only)
  if (func.func_directive.shared_mem) {
    std::string name(*func.func_directive.shared_mem);
    if (auto r =
            sym.insert_local(func_name, name, SymbolKind::Param,
                             make_scalar(ScalarType::B64), StateSpace::Param);
        !r)
      errs.push_back(std::move(r.error()));
  }
  // Function body
  if (func.body)
    collect_stmts(sym, func_name, *func.body, errs);
}

// ============================================================
// Phase 2 – resolution helpers
// ============================================================

/// Build a VisitorMap lambda that resolves an Ident to a SymbolId.
/// Unknown identifiers are mapped to kInvalidSymbolId (and an error
/// is appended to `errs`).
static auto make_resolve_fn(const SymbolTable& sym,
                            const std::string& func_name,
                            std::vector<ResolveError>& errs) {
  return [&sym, &func_name, &errs](
             Ident id, std::optional<VisitTypeSpace>, bool /*is_dst*/,
             bool /*relaxed*/) -> expected<SymbolId, ResolveError> {
    auto r = sym.lookup(id, func_name);
    if (!r) {
      errs.push_back(r.error());
      return kInvalidSymbolId;
    }
    return *r;
  };
}

/// Resolve a single statement (recursively for blocks).
static Statement<ResolvedOp> resolve_stmt(const Statement<ParsedOp>& stmt,
                                          const SymbolTable& sym,
                                          const std::string& func_name,
                                          std::vector<ResolveError>& errs) {
  return std::visit(
      [&](const auto& s) -> Statement<ResolvedOp> {
        using S = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<S, Statement<ParsedOp>::LabelS>) {
          // Map the label Ident to a SymbolId
          auto r = sym.lookup(s.label, func_name);
          SymbolId id = r ? *r : kInvalidSymbolId;
          if (!r)
            errs.push_back(r.error());
          return Statement<ResolvedOp>::Label(id);

        } else if constexpr (std::is_same_v<S,
                                            Statement<ParsedOp>::VariableS>) {
          // Variable declarations are preserved (no operand to resolve)
          return Statement<ResolvedOp>::Var(s.var);

        } else if constexpr (std::is_same_v<
                                 S, Statement<ParsedOp>::InstructionS>) {
          auto vm = make_visitor_map<ParsedOp, ResolvedOp, ResolveError>(
              make_resolve_fn(sym, func_name, errs));

          auto instr_r = map_instruction(s.instr, vm);
          if (!instr_r) {
            errs.push_back(instr_r.error());
            // Produce a no-op placeholder: InstrTrap
            Instruction<ResolvedOp> placeholder = InstrTrap{};
            return Statement<ResolvedOp>::Instr(std::nullopt,
                                                std::move(placeholder));
          }

          // Resolve predicate guard Ident → SymbolId
          std::optional<PredAt> pred;
          if (s.pred) {
            auto pr = sym.lookup(s.pred->label, func_name);
            SymbolId pid = pr ? *pr : kInvalidSymbolId;
            if (!pr)
              errs.push_back(pr.error());
            pred = PredAt{s.pred->negate, std::string_view{}};
            // PredAt stores an Ident (string_view) — we store the resolved id
            // in a separate field; for now we keep the original text so higher
            // passes can still print it. A fully-typed resolved representation
            // would embed the SymbolId directly; that refactor is out of scope
            // for this pass.
            pred = s.pred;  // keep original for display purposes
            (void)pid;
          }

          return Statement<ResolvedOp>::Instr(pred, std::move(*instr_r));

        } else {
          // BlockS — recurse
          std::vector<Statement<ResolvedOp>> resolved;
          resolved.reserve(s.stmts.size());
          for (const auto& inner : s.stmts)
            resolved.push_back(resolve_stmt(inner, sym, func_name, errs));
          return Statement<ResolvedOp>::Block(std::move(resolved));
        }
      },
      stmt.inner);
}

/// Resolve the body of a Function<ParsedOp> into Function<ResolvedOp>.
static Function<ResolvedOp> resolve_function(const Function<ParsedOp>& func,
                                             const SymbolTable& sym,
                                             const std::string& func_name,
                                             std::vector<ResolveError>& errs) {
  Function<ResolvedOp> out;
  out.func_directive = func.func_directive;
  out.tuning = func.tuning;

  if (func.body) {
    std::vector<Statement<ResolvedOp>> resolved;
    resolved.reserve(func.body->size());
    for (const auto& stmt : *func.body)
      resolved.push_back(resolve_stmt(stmt, sym, func_name, errs));
    out.body = std::move(resolved);
  }

  return out;
}

}  // anonymous namespace

// ============================================================
// resolve_module  —  public API
// ============================================================

ResolvedModule resolve_module(const Module& mod,
                              std::vector<ResolveError>& out_errors) {
  ResolvedModule result;
  result.ptx_version = mod.ptx_version;
  result.sm_version = mod.sm_version;
  result.invalid_directives = mod.invalid_directives;

  SymbolTable& sym = result.symbols;

  // ── Phase 1: collect all declarations ────────────────────────────────────

  for (const auto& dir : mod.directives) {
    std::visit(
        [&](const auto& d) {
          using D = std::decay_t<decltype(d)>;

          if constexpr (std::is_same_v<D, Directive<ParsedOp>::VariableD>) {
            // Global / const variable
            VariableInfo info = d.var.info;
            std::string name(d.var.name);
            SymbolKind kind = kind_for_space(info.state_space);
            if (auto r = sym.insert_global(name, kind, info.v_type,
                                           info.state_space);
                !r)
              out_errors.push_back(std::move(r.error()));

          } else if constexpr (std::is_same_v<D,
                                              Directive<ParsedOp>::MethodD>) {
            // Function / kernel name → module scope
            std::string fname(d.func.func_directive.name);
            SymbolKind kind = SymbolKind::Function;
            // Use a placeholder type for the function symbol itself
            if (auto r = sym.insert_global(
                    fname, kind, make_scalar(ScalarType::B64), StateSpace::Reg);
                !r)
              out_errors.push_back(std::move(r.error()));

            // Function-local declarations (params, registers, labels)
            collect_function(sym, fname, d.func, out_errors);
          }
        },
        dir.inner);
  }

  // ── Phase 2: resolve all operand identifiers ──────────────────────────────

  for (const auto& dir : mod.directives) {
    std::visit(
        [&](const auto& d) {
          using D = std::decay_t<decltype(d)>;

          if constexpr (std::is_same_v<D, Directive<ParsedOp>::VariableD>) {
            // No instruction operands in a variable declaration
            result.directives.push_back(Directive<ResolvedOp>{
                Directive<ResolvedOp>::VariableD{d.linking, d.var}});

          } else if constexpr (std::is_same_v<D,
                                              Directive<ParsedOp>::MethodD>) {
            std::string fname(d.func.func_directive.name);
            auto resolved_func =
                resolve_function(d.func, sym, fname, out_errors);
            result.directives.push_back(
                Directive<ResolvedOp>{Directive<ResolvedOp>::MethodD{
                    d.linking, std::move(resolved_func)}});
          }
        },
        dir.inner);
  }

  return result;
}

}  // namespace ptx_frontend
