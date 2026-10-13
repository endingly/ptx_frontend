#include <optional>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Verify installed multimem form and completion contracts without GPU execution. */
int main() {
  using namespace ptx_frontend::resolved_ir;
  std::optional<ResolvedModule> module;
  {
    constexpr auto source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.global .align 16 .b8 mm[64];
.shared .align 16 .b8 src[64];
.visible .entry kernel() {
  .reg .u32 %u;
  .reg .f32 %f<4>;
  multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [src], 16;
  multimem.ld_reduce.gpu.add.u32 %u, [mm];
  multimem.st.weak.sys.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};
  multimem.st.u32 [mm], %u;
  multimem.ld_reduce.weak.add.u32 %u, [mm];
  ret;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 1;
    auto result = resolveAndValidateModule(*ast);
    if (!result || result->functions.front().body.size() != 6)
      return 2;
    module.emplace(std::move(*result));
  }
  const auto& body = module->functions.front().body;
  const auto* form =
      dynamic_cast<const ptx_frontend::resolved_ir::MultimemCpAsyncBulkWeak*>(
          module->functions.front().body.front().get());
  if (!form || form->completion_kind !=
                   ptx_frontend::base::AsyncCompletionKind::BulkGroup)
    return 3;
  const auto* load =
      dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak*>(
          body[1].get());
  const auto* store =
      dynamic_cast<const MultimemStFloatNoneV4DefaultWeak*>(body[2].get());
  const auto* bare =
      dynamic_cast<const MultimemStIntegerNoneScalarDefaultWeak*>(
          body[3].get());
  const auto* weak =
      dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak*>(
          body[4].get());
  if (!load || !store || !bare || !weak)
    return 4;
  if (load->semantics.value != MemoryConsistency::Omitted ||
      !load->semantics.locs.empty() || load->scope.value != MemoryScope::Gpu ||
      load->scope.locs.size() != 1)
    return 5;
  if (store->semantics.value != MemoryConsistency::Weak ||
      store->semantics.locs.size() != 1 ||
      store->scope.value != MemoryScope::Sys || store->scope.locs.size() != 1)
    return 6;
  if (bare->semantics.value != MemoryConsistency::Omitted ||
      !bare->semantics.locs.empty() || bare->scope.value != MemoryScope::None ||
      !bare->scope.locs.empty() ||
      weak->semantics.value != MemoryConsistency::Weak ||
      weak->semantics.locs.size() != 1 ||
      weak->scope.value != MemoryScope::None || !weak->scope.locs.empty())
    return 7;
  return validateModule(*module) ? 0 : 8;
}
