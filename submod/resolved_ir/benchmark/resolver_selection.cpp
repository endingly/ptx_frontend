#include <benchmark/benchmark.h>
#include <iostream>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/matrix/wgmma.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_selection.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <stdexcept>
#include "test_modifier_candidate_plan.hpp"
namespace ptx_frontend::resolved_ir {
/** Return the existing generated MOV catalog. */
const check_end::SyntaxInstructionDescriptor& mov_syntax_descriptor() noexcept;
/** Return the existing generated WGMMA catalog. */
const check_end::SyntaxInstructionDescriptor&
wgmma_syntax_descriptor() noexcept;
}  // namespace ptx_frontend::resolved_ir
namespace {
namespace ir = ptx_frontend::resolved_ir;
/** Measure selection or resolution of one pre-parsed legal source fragment. */
void measure(benchmark::State& state, int family, int mode) {
  const char* source = family == 0 ? "mov.u32 %r0, %r1;"
                       : family == 1
                           ? "wgmma.fence.sync.aligned;"
                           : "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16."
                             "f16 {%d0,%d1}, %rd0, %rd1, 1, -1, 1, 0, 1;";
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto parsed = parser.parseInstruction();
  if (!parsed || !parsed.diagnostics.empty()) {
    state.SkipWithError("fragment parse failed");
    return;
  }
  const auto& descriptor =
      family != 0 ? ir::wgmma_syntax_descriptor() : ir::mov_syntax_descriptor();
  const auto initial = ir::select_variant_name(*parsed, descriptor);
  if (!initial) {
    state.SkipWithError("initial selection failed");
    return;
  }
  const auto expected_kind =
      family == 0   ? ir::MovScalar::kind
      : family == 1 ? ir::WgmmaFenceSyncAligned::kind
                    : ir::WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain::kind;
  const auto preflight = ir::resolveInstruction(*parsed);
  if (!preflight || !*preflight ||
      (*preflight)->opcode_name() != parsed->opcode.syntax.text ||
      (*preflight)->instruction_kind() != expected_kind) {
    state.SkipWithError("preflight resolution kind or opcode mismatch");
    return;
  }
  ir::test_helpers::OwnedCandidatePlan plan(descriptor);
  std::vector<uint64_t> scratch((descriptor.variants.size() + 63) / 64);
  size_t candidates = 0;
  for (size_t form = 0; form < descriptor.variants.size(); ++form) {
    bool included = true;
    for (const auto& modifier : parsed->modifiers) {
      const auto row =
          std::ranges::find(plan.rows, modifier.syntax.text,
                            &ir::detail::ModifierCandidateRow::spelling);
      if (row == plan.rows.end() ||
          !(plan.words[(row - plan.rows.begin()) * scratch.size() + form / 64] &
            (uint64_t{1} << (form % 64))))
        included = false;
    }
    candidates += included;
  }
  state.counters["candidates"] = candidates;
  state.counters["index_bytes"] =
      plan.rows.size() * sizeof(ir::detail::ModifierCandidateRow) +
      plan.words.size() * sizeof(uint64_t);
  state.counters["forms"] = descriptor.variants.size();
  for (auto ignored : state) {
    static_cast<void>(ignored);
    if (mode == 2) {
      auto result = ir::resolveInstruction(*parsed);
      if (!result || !*result ||
          (*result)->opcode_name() != parsed->opcode.syntax.text ||
          (*result)->instruction_kind() != expected_kind) {
        state.SkipWithError("resolution failed or returned wrong kind/opcode");
        break;
      }
      benchmark::DoNotOptimize(result->get());
    } else if (mode == 1) {
      auto result = ir::detail::select_indexed_variant(*parsed, descriptor,
                                                       plan.view(), scratch);
      if (!result || descriptor.variants[*result].variant_name != *initial) {
        state.SkipWithError("indexed selection changed");
        break;
      }
      benchmark::DoNotOptimize(*result);
    } else {
      auto result = ir::select_variant_name(*parsed, descriptor);
      if (!result || *result != *initial) {
        state.SkipWithError("selection changed");
        break;
      }
      benchmark::DoNotOptimize(result->data());
    }
  }
}
}  // namespace
/** Register bounded resolver measurements, then execute the requested sample. */
int main(int argc, char** argv) {
  for (int family = 0; family < 3; ++family) {
    const char* name = family == 0   ? "mov"
                       : family == 1 ? "wgmma_fence"
                                     : "wgmma_mma";
    benchmark::RegisterBenchmark((std::string("selection/") + name).c_str(),
                                 measure, family, 0);
    benchmark::RegisterBenchmark((std::string("indexed/") + name).c_str(),
                                 measure, family, 1);
    benchmark::RegisterBenchmark((std::string("resolution/") + name).c_str(),
                                 measure, family, 2);
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
