#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Verify installed multimem form and completion contracts without GPU execution. */
int main() {
  constexpr auto source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.global .align 16 .b8 mm[64];
.shared .align 16 .b8 src[64];
.visible .entry kernel() {
  multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [src], 16;
  ret;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 1;
  auto module = ptx_frontend::resolved_ir::resolveAndValidateModule(*ast);
  if (!module || module->functions.front().body.size() != 2)
    return 2;
  const auto* form =
      dynamic_cast<const ptx_frontend::resolved_ir::MultimemCpAsyncBulkWeak*>(
          module->functions.front().body.front().get());
  if (!form || form->completion_kind !=
                   ptx_frontend::base::AsyncCompletionKind::BulkGroup)
    return 3;
  return ptx_frontend::resolved_ir::validateModule(*module) ? 0 : 4;
}
