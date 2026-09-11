#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#ifndef PTX_BENCHMARK_CORPUS_PATH
#error "PTX_BENCHMARK_CORPUS_PATH must be supplied by the benchmark target."
#endif

namespace {

/** Physical declaration spelling used for otherwise identical logical registers. */
enum class DeclarationLayout { Ordinary, Compact };

/** Distribution of declarations across lexical scopes. */
enum class ScopeLayout { Single, Nested };

/** Immutable parameters identifying one generated symbol-table workload. */
struct WorkloadConfiguration {
  /** Number of logical registers represented by the generated declarations. */
  std::size_t logical_registers{};
  /** Whether the source uses individual or parameterized register declarations. */
  DeclarationLayout declarations{};
  /** Whether declarations occupy one function scope or nested block scopes. */
  ScopeLayout scopes{};
};

/** One generated workload and the names expected to resolve in its late scopes. */
struct Workload {
  /** Stable case label used in diagnostics and benchmark names. */
  std::string name;
  /** Source containing declarations but no instruction references. */
  std::string declaration_source;
  /** Source containing declarations and late instruction references. */
  std::string full_source;
  /** One late-register spelling for each scope exercised by lookup-only. */
  std::vector<std::string> late_names;
  /** Number of logical registers represented by this workload. */
  std::size_t logical_registers{};
  /** Source declaration representation. */
  DeclarationLayout declarations{};
  /** Lexical scope distribution. */
  ScopeLayout scopes{};
};

/** Observable results retained after one benchmark operation completes. */
struct BenchmarkOutcome {
  /** Deterministic value derived from validated operation output. */
  uint64_t checksum{};
  /** Number of symbols retained by the associated binding result. */
  std::size_t stored_symbols{};
  /** Number of references retained by the associated binding result. */
  std::size_t references{};
  /** Number of direct table lookup calls made by the operation. */
  std::size_t lookup_calls{};
};

/** Parsed corpus source and the invariants reused by its selected benchmarks. */
struct CorpusWorkload {
  /** Path-independent owning copy of the representative PTX source. */
  std::string source;
  /** Expected number of functions in every successful parse. */
  std::size_t functions{};
  /** Expected recursive instruction count in every successful parse. */
  std::size_t instructions{};
};

/** Tracks validation failures so the custom benchmark main returns nonzero. */
bool validation_failed = false;

/** Return the diagnostic label for a declaration layout. */
std::string_view layout_name(DeclarationLayout layout) {
  return layout == DeclarationLayout::Ordinary ? "ordinary" : "compact";
}

/** Return the diagnostic label for a scope layout. */
std::string_view scope_name(ScopeLayout layout) {
  return layout == ScopeLayout::Single ? "single" : "nested";
}

/** Serialize one ordinary or parameterized register declaration. */
void append_declarations(std::string& source, std::size_t count,
                         DeclarationLayout layout) {
  if (layout == DeclarationLayout::Compact) {
    source += "  .reg .u32 %r<" + std::to_string(count) + ">;\n";
    return;
  }
  for (std::size_t index = 0; index < count; ++index)
    source += "  .reg .u32 %r" + std::to_string(index) + ";\n";
}

/** Append instruction references while preserving declaration-only source separately. */
void append_references(std::string& source, std::size_t destination,
                       std::size_t source_register, std::size_t count) {
  for (std::size_t index = 0; index < count; ++index)
    source += "  mov.u32 %r" + std::to_string(destination) + ", %r" +
              std::to_string(source_register) + ";\n";
}

/** Construct a valid module with N logical registers and N late-register references. */
Workload make_workload(WorkloadConfiguration configuration) {
  const std::size_t register_count = configuration.logical_registers;
  if (register_count < 4 || register_count % 4 != 0)
    throw std::invalid_argument("register count must be a multiple of four");

  Workload workload{
      .name = std::string(layout_name(configuration.declarations)) + "_" +
              std::string(scope_name(configuration.scopes)) + "_" +
              std::to_string(register_count),
      .logical_registers = register_count,
      .declarations = configuration.declarations,
      .scopes = configuration.scopes};
  std::string declarations_source =
      ".version 8.0\n.target sm_80\n.address_size 64\n";

  if (configuration.scopes == ScopeLayout::Single) {
    declarations_source += ".visible .entry issue116_single() {\n";
    append_declarations(declarations_source, register_count,
                        configuration.declarations);
    workload.late_names.push_back("%r" + std::to_string(register_count - 1));
    workload.declaration_source = declarations_source + "}\n";
    workload.full_source = declarations_source;
    append_references(workload.full_source, register_count - 1,
                      register_count - 1, register_count);
    workload.full_source += "}\n";
    return workload;
  }

  const std::size_t registers_per_function = register_count / 2;
  const std::size_t registers_per_scope = registers_per_function / 2;
  for (std::size_t function = 0; function < 2; ++function) {
    declarations_source += ".visible .entry issue116_nested_" +
                           std::to_string(function) + "() {\n";
    append_declarations(declarations_source, registers_per_scope,
                        configuration.declarations);
    declarations_source += "  {\n";
    append_declarations(declarations_source, registers_per_scope,
                        configuration.declarations);
    declarations_source += "  }\n}\n";
    workload.late_names.push_back("%r" +
                                  std::to_string(registers_per_scope - 1));
    workload.late_names.push_back("%r" +
                                  std::to_string(registers_per_scope - 1));
  }
  workload.declaration_source = declarations_source;

  std::string full_source = ".version 8.0\n.target sm_80\n.address_size 64\n";
  for (std::size_t function = 0; function < 2; ++function) {
    const std::size_t last_register = registers_per_scope - 1;
    full_source += ".visible .entry issue116_nested_" +
                   std::to_string(function) + "() {\n";
    append_declarations(full_source, registers_per_scope,
                        configuration.declarations);
    full_source += "  {\n";
    append_declarations(full_source, registers_per_scope,
                        configuration.declarations);
    append_references(full_source, last_register, last_register,
                      registers_per_function / 2);
    full_source += "  }\n";
    append_references(full_source, last_register, last_register,
                      registers_per_function / 2);
    full_source += "}\n";
  }
  workload.full_source = std::move(full_source);
  return workload;
}

/** Parse a complete module and reject partial ASTs or parser diagnostics. */
ptx_frontend::syntax_ast::AstModule parse_clean(std::string_view source,
                                                std::string_view name) {
  ptx_frontend::PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed.value || !parsed.diagnostics.empty())
    throw std::runtime_error("parse validation failed for " +
                             std::string(name));
  return std::move(*parsed.value);
}

/** Mix stable SymbolIds and parameterized member indexes into an observable value. */
uint64_t mix_lookup(uint64_t checksum,
                    const ptx_frontend::binding::SymbolLookup& lookup) {
  constexpr uint64_t multiplier = 1'099'511'628'211ULL;
  checksum ^= static_cast<uint64_t>(lookup.symbol.value) + 1;
  checksum *= multiplier;
  checksum ^= lookup.parameterized_index.value_or(0) + 1;
  return checksum * multiplier;
}

/** Count function declarations represented in a parsed module. */
std::size_t ast_function_count(
    const ptx_frontend::syntax_ast::AstModule& module) {
  return std::count_if(
      module.items.begin(), module.items.end(), [](const auto& item) {
        return std::holds_alternative<ptx_frontend::syntax_ast::AstFunction>(
            item);
      });
}

/** Count instructions recursively, including instructions inside lexical blocks. */
std::size_t ast_instruction_count(
    const std::vector<ptx_frontend::syntax_ast::AstFunctionBodyItem>& body) {
  std::size_t count = 0;
  for (const auto& item : body) {
    if (std::holds_alternative<ptx_frontend::syntax_ast::AstInstruction>(item))
      ++count;
    if (const auto* block =
            std::get_if<std::unique_ptr<ptx_frontend::syntax_ast::AstBlock>>(
                &item))
      count += ast_instruction_count((*block)->body);
  }
  return count;
}

/** Count every instruction represented by a module's function bodies. */
std::size_t ast_instruction_count(
    const ptx_frontend::syntax_ast::AstModule& module) {
  std::size_t count = 0;
  for (const auto& item : module.items) {
    if (const auto* function =
            std::get_if<ptx_frontend::syntax_ast::AstFunction>(&item))
      count += ast_instruction_count(function->body);
  }
  return count;
}

/** Validate that generated source retained all expected functions and instructions. */
void validate_workload_ast(const ptx_frontend::syntax_ast::AstModule& module,
                           const Workload& workload, bool has_references) {
  const std::size_t expected_functions =
      workload.scopes == ScopeLayout::Single ? 1 : 2;
  if (ast_function_count(module) != expected_functions)
    throw std::runtime_error("unexpected function count for " + workload.name);
  const std::size_t expected_instructions =
      has_references ? workload.logical_registers : 0;
  if (ast_instruction_count(module) != expected_instructions)
    throw std::runtime_error("unexpected instruction count for " +
                             workload.name);
}

/** Validate a binding result and return a deterministic identity/count checksum. */
uint64_t validate_binding(const ptx_frontend::binding::SymbolBinding& binding,
                          const Workload& workload, bool has_references) {
  if (!binding.diagnostics.empty())
    throw std::runtime_error("binding diagnostics for " + workload.name);
  const std::size_t expected_variables =
      workload.declarations == DeclarationLayout::Ordinary
          ? workload.logical_registers
      : workload.scopes == ScopeLayout::Single ? 1
                                               : 4;
  const std::size_t expected_symbols =
      expected_variables + (workload.scopes == ScopeLayout::Single ? 1 : 2);
  if (binding.table.symbols().size() != expected_symbols)
    throw std::runtime_error("unexpected symbol count for " + workload.name);
  const std::size_t expected_references =
      has_references ? 2 * workload.logical_registers : 0;
  if (binding.table.references().size() != expected_references)
    throw std::runtime_error("unexpected reference count for " + workload.name);
  uint64_t checksum = binding.table.symbols().size();
  for (const auto& symbol : binding.table.symbols())
    checksum = (checksum * 131) ^ symbol.id.value;
  return checksum;
}

/** Locate function and block scopes in creation order for direct table lookup tests. */
std::vector<ptx_frontend::binding::ScopeId> lookup_scopes(
    const ptx_frontend::binding::SymbolTable& table, ScopeLayout layout) {
  std::vector<ptx_frontend::binding::ScopeId> result;
  for (const auto& scope : table.scopes()) {
    if (layout == ScopeLayout::Single &&
        scope.kind == ptx_frontend::binding::ScopeKind::Function)
      result.push_back(scope.id);
    if (layout == ScopeLayout::Nested &&
        (scope.kind == ptx_frontend::binding::ScopeKind::Function ||
         scope.kind == ptx_frontend::binding::ScopeKind::Block))
      result.push_back(scope.id);
  }
  return result;
}

/** Verify a lookup result's symbol, scope, and parameterized member identity. */
void validate_lookup(const ptx_frontend::binding::SymbolTable& table,
                     const Workload& workload,
                     ptx_frontend::binding::ScopeId lookup_scope,
                     std::string_view name,
                     const ptx_frontend::binding::SymbolLookup& found) {
  const auto& symbol = table.symbol(found.symbol);
  if (symbol.scope != lookup_scope)
    throw std::runtime_error("late lookup crossed its expected scope for " +
                             workload.name);
  if (workload.declarations == DeclarationLayout::Compact) {
    const uint32_t expected_member =
        workload.scopes == ScopeLayout::Single
            ? static_cast<uint32_t>(workload.logical_registers - 1)
            : static_cast<uint32_t>(workload.logical_registers / 4 - 1);
    if (!found.parameterized_index ||
        *found.parameterized_index != expected_member || symbol.name != "%r")
      throw std::runtime_error("compact member identity missing for " +
                               workload.name);
    return;
  }
  if (found.parameterized_index || symbol.name != name)
    throw std::runtime_error("ordinary lookup became parameterized for " +
                             workload.name);
}

/** Validate a resolved module's function and flattened instruction totals. */
void validate_resolved_module(
    const ptx_frontend::resolved_ir::ResolvedModule& resolved,
    std::size_t expected_functions, std::size_t expected_instructions,
    std::string_view name) {
  std::size_t resolved_instructions = 0;
  for (const auto& function : resolved.functions)
    resolved_instructions += function.body.size();
  if (resolved.functions.size() != expected_functions ||
      resolved_instructions != expected_instructions)
    throw std::runtime_error("resolved model count mismatch for " +
                             std::string(name));
}

/** Populate framework counters from one validated operation result. */
void set_counters(benchmark::State& state, std::size_t logical_registers,
                  const BenchmarkOutcome& outcome) {
  constexpr uint64_t lower_32_bits = 0xFFFF'FFFFULL;
  state.counters["logical_registers"] = static_cast<double>(logical_registers);
  state.counters["stored_symbols"] =
      static_cast<double>(outcome.stored_symbols);
  state.counters["references"] = static_cast<double>(outcome.references);
  state.counters["lookup_calls"] = static_cast<double>(outcome.lookup_calls);
  state.counters["checksum_hi"] = static_cast<double>(outcome.checksum >> 32U);
  state.counters["checksum_lo"] =
      static_cast<double>(outcome.checksum & lower_32_bits);
}

/** Mark a failed validation as a skipped benchmark and preserve a failing process status. */
void report_validation_failure(benchmark::State& state,
                               const std::exception& error) {
  validation_failed = true;
  state.SkipWithError(error.what());
}

/** Mark an unknown validation failure as a skipped benchmark and failing status. */
void report_unknown_validation_failure(benchmark::State& state) {
  validation_failed = true;
  state.SkipWithError("unknown benchmark validation failure");
}

/** Benchmark parsing a generated source with declarations and instruction references. */
void benchmark_parse(benchmark::State& state,
                     WorkloadConfiguration configuration) {
  try {
    const Workload workload = make_workload(configuration);
    const auto initial = parse_clean(workload.full_source, workload.name);
    validate_workload_ast(initial, workload, true);
    BenchmarkOutcome outcome{};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      auto parsed = parse_clean(workload.full_source, workload.name);
      validate_workload_ast(parsed, workload, true);
      outcome = {.checksum = workload.full_source.size() ^
                             static_cast<uint64_t>(ast_function_count(parsed))};
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, workload.logical_registers, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Benchmark binding declarations after parsing their source outside framework timing. */
void benchmark_bind_declarations(benchmark::State& state,
                                 WorkloadConfiguration configuration) {
  try {
    const Workload workload = make_workload(configuration);
    const auto declarations =
        parse_clean(workload.declaration_source, workload.name);
    validate_workload_ast(declarations, workload, false);
    BenchmarkOutcome outcome{};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      const auto binding = ptx_frontend::binding::bindSymbols(declarations);
      outcome = {.checksum = validate_binding(binding, workload, false),
                 .stored_symbols = binding.table.symbols().size(),
                 .references = binding.table.references().size()};
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, workload.logical_registers, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Benchmark binding declarations and instruction references after parsing outside timing. */
void benchmark_bind_with_references(benchmark::State& state,
                                    WorkloadConfiguration configuration) {
  try {
    const Workload workload = make_workload(configuration);
    const auto parsed = parse_clean(workload.full_source, workload.name);
    validate_workload_ast(parsed, workload, true);
    BenchmarkOutcome outcome{};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      const auto binding = ptx_frontend::binding::bindSymbols(parsed);
      outcome = {.checksum = validate_binding(binding, workload, true),
                 .stored_symbols = binding.table.symbols().size(),
                 .references = binding.table.references().size(),
                 .lookup_calls = binding.table.references().size()};
      if (outcome.references == 0)
        throw std::runtime_error("expected instruction references for " +
                                 workload.name);
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, workload.logical_registers, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Benchmark direct late-register symbol-table lookups using a prebuilt binding. */
void benchmark_lookup_only(benchmark::State& state,
                           WorkloadConfiguration configuration) {
  try {
    const Workload workload = make_workload(configuration);
    const auto parsed = parse_clean(workload.full_source, workload.name);
    validate_workload_ast(parsed, workload, true);
    const auto binding = ptx_frontend::binding::bindSymbols(parsed);
    const uint64_t binding_checksum = validate_binding(binding, workload, true);
    const auto scopes = lookup_scopes(binding.table, workload.scopes);
    if (scopes.size() != workload.late_names.size())
      throw std::runtime_error("unexpected scope count for " + workload.name);

    BenchmarkOutcome outcome{.stored_symbols = binding.table.symbols().size(),
                             .references = binding.table.references().size(),
                             .lookup_calls = workload.logical_registers};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      uint64_t checksum = binding_checksum;
      for (std::size_t lookup_index = 0;
           lookup_index < workload.logical_registers; ++lookup_index) {
        const std::size_t scope_index = lookup_index % scopes.size();
        const auto found = binding.table.lookup(
            scopes[scope_index], workload.late_names[scope_index]);
        if (!found)
          throw std::runtime_error("late lookup failed for " + workload.name);
        validate_lookup(binding.table, workload, scopes[scope_index],
                        workload.late_names[scope_index], *found);
        checksum = mix_lookup(checksum, *found);
      }
      outcome.checksum = checksum;
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, workload.logical_registers, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Benchmark resolving a pre-parsed generated module. */
void benchmark_resolve_module(benchmark::State& state,
                              WorkloadConfiguration configuration) {
  try {
    const Workload workload = make_workload(configuration);
    const auto parsed = parse_clean(workload.full_source, workload.name);
    validate_workload_ast(parsed, workload, true);
    const auto binding = ptx_frontend::binding::bindSymbols(parsed);
    const uint64_t binding_checksum = validate_binding(binding, workload, true);
    BenchmarkOutcome outcome{.stored_symbols = binding.table.symbols().size(),
                             .references = binding.table.references().size(),
                             .lookup_calls = workload.logical_registers};
    const std::size_t expected_functions =
        workload.scopes == ScopeLayout::Single ? 1 : 2;
    for (auto iteration : state) {
      static_cast<void>(iteration);
      const auto resolved = ptx_frontend::resolved_ir::resolveModule(parsed);
      if (!resolved)
        throw std::runtime_error("module resolution diagnostics for " +
                                 workload.name);
      validate_resolved_module(*resolved, expected_functions,
                               workload.logical_registers, workload.name);
      outcome.checksum = binding_checksum ^ workload.full_source.size();
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, workload.logical_registers, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Read a corpus source file as an owning string. */
std::string read_file(std::string_view path) {
  std::ifstream input{std::string(path)};
  if (!input)
    throw std::runtime_error("cannot open corpus file: " + std::string(path));
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

/** Lazily prepare the representative corpus only when a corpus benchmark is selected. */
CorpusWorkload make_corpus_workload() {
  CorpusWorkload workload{.source = read_file(PTX_BENCHMARK_CORPUS_PATH)};
  const auto parsed = parse_clean(workload.source, PTX_BENCHMARK_CORPUS_PATH);
  workload.functions = ast_function_count(parsed);
  workload.instructions = ast_instruction_count(parsed);
  if (workload.functions == 0)
    throw std::runtime_error("corpus contains no parsed functions: " +
                             std::string(PTX_BENCHMARK_CORPUS_PATH));
  return workload;
}

/** Verify that a corpus parse preserves the source-derived AST shape. */
void validate_corpus_ast(const ptx_frontend::syntax_ast::AstModule& parsed,
                         const CorpusWorkload& workload) {
  if (ast_function_count(parsed) != workload.functions ||
      ast_instruction_count(parsed) != workload.instructions)
    throw std::runtime_error("corpus parse changed AST counts for " +
                             std::string(PTX_BENCHMARK_CORPUS_PATH));
}

/** Benchmark parsing the representative corpus source. */
void benchmark_corpus_parse(benchmark::State& state) {
  try {
    const CorpusWorkload workload = make_corpus_workload();
    BenchmarkOutcome outcome{};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      const auto parsed =
          parse_clean(workload.source, PTX_BENCHMARK_CORPUS_PATH);
      validate_corpus_ast(parsed, workload);
      outcome.checksum = workload.source.size() ^ workload.functions;
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, 0, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Benchmark resolving a pre-parsed representative corpus module. */
void benchmark_corpus_resolve_module(benchmark::State& state) {
  try {
    const CorpusWorkload workload = make_corpus_workload();
    const auto parsed = parse_clean(workload.source, PTX_BENCHMARK_CORPUS_PATH);
    validate_corpus_ast(parsed, workload);
    const auto binding = ptx_frontend::binding::bindSymbols(parsed);
    if (!binding.diagnostics.empty())
      throw std::runtime_error("binding diagnostics for corpus " +
                               std::string(PTX_BENCHMARK_CORPUS_PATH));
    BenchmarkOutcome outcome{.stored_symbols = binding.table.symbols().size(),
                             .references = binding.table.references().size(),
                             .lookup_calls = binding.table.references().size()};
    for (auto iteration : state) {
      static_cast<void>(iteration);
      const auto resolved = ptx_frontend::resolved_ir::resolveModule(parsed);
      if (!resolved)
        throw std::runtime_error("corpus resolution changed outcome: " +
                                 std::string(PTX_BENCHMARK_CORPUS_PATH));
      validate_resolved_module(*resolved, workload.functions,
                               workload.instructions,
                               PTX_BENCHMARK_CORPUS_PATH);
      outcome.checksum = workload.source.size() ^ outcome.stored_symbols;
      benchmark::DoNotOptimize(outcome.checksum);
    }
    set_counters(state, 0, outcome);
  } catch (const std::exception& error) {
    report_validation_failure(state, error);
  } catch (...) {
    report_unknown_validation_failure(state);
  }
}

/** Construct a readable benchmark name for one generated operation and workload. */
std::string benchmark_name(std::string_view operation,
                           WorkloadConfiguration configuration) {
  return "symbol_table_scaling/" + std::string(operation) + "/" +
         std::string(layout_name(configuration.declarations)) + "/" +
         std::string(scope_name(configuration.scopes)) + "/N" +
         std::to_string(configuration.logical_registers);
}

/** Register every generated operation without constructing source or AST fixtures. */
void register_generated_benchmarks() {
  constexpr std::size_t sizes[] = {1000, 2000, 4000, 8000};
  constexpr DeclarationLayout declarations[] = {DeclarationLayout::Ordinary,
                                                DeclarationLayout::Compact};
  constexpr ScopeLayout scopes[] = {ScopeLayout::Single, ScopeLayout::Nested};
  for (const std::size_t size : sizes) {
    for (const DeclarationLayout declaration : declarations) {
      for (const ScopeLayout scope : scopes) {
        const WorkloadConfiguration configuration{size, declaration, scope};
        benchmark::RegisterBenchmark(benchmark_name("parse", configuration),
                                     benchmark_parse, configuration)
            ->UseRealTime();
        benchmark::RegisterBenchmark(
            benchmark_name("bind_declarations_only", configuration),
            benchmark_bind_declarations, configuration)
            ->UseRealTime();
        benchmark::RegisterBenchmark(
            benchmark_name("bind_with_references", configuration),
            benchmark_bind_with_references, configuration)
            ->UseRealTime();
        benchmark::RegisterBenchmark(
            benchmark_name("lookup_only", configuration), benchmark_lookup_only,
            configuration)
            ->UseRealTime();
        benchmark::RegisterBenchmark(
            benchmark_name("resolve_module", configuration),
            benchmark_resolve_module, configuration)
            ->UseRealTime();
      }
    }
  }
}

/** Register corpus operations without eagerly reading the corpus source. */
void register_corpus_benchmarks() {
  benchmark::RegisterBenchmark(
      "symbol_table_scaling/corpus_parse/natural_kernel_sm80",
      benchmark_corpus_parse)
      ->UseRealTime();
  benchmark::RegisterBenchmark(
      "symbol_table_scaling/corpus_resolve_module/natural_kernel_sm80",
      benchmark_corpus_resolve_module)
      ->UseRealTime();
}

}  // namespace

/** Register benchmark cases, execute selected cases, and signal validation failure. */
int main(int argc, char** argv) {
  try {
    register_generated_benchmarks();
    register_corpus_benchmarks();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
      return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return validation_failed ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << "frontend_symbol_table_scaling: " << error.what() << '\n';
    return 1;
  }
}
