#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fabric.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

/** Exercise installed CFT fields and immutable protocol metadata without a GPU. */
int main() {
  constexpr auto source = R"ptx(
.version 9.3
.target sm_100
.address_size 64
.shared .align 16 .b8 data[64];
.shared .align 16 .b8 bar[16];
.visible .entry kernel() {
  .reg .b32 %endpoint;
  .reg .b64 %dataoff;
  .reg .b64 %counteroff;
  fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 [%endpoint, %dataoff, %counteroff], [data], 16, [bar];
  fabric.submit.op_restrict::fetching;
  fabric.wait.sync_restrict::reads;
  ret;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 1;
  auto module = ptx_frontend::resolved_ir::resolveAndValidateModule(*ast);
  if (!module || module->functions.front().body.size() != 4)
    return 2;
  using namespace ptx_frontend::resolved_ir;
  const auto* put = dynamic_cast<const FabricTryPutUnicastCounted*>(
      module->functions.front().body.front().get());
  if (!put || !put->dst.value.counter_offset ||
      !put->dst.value.endpoint.value.declared_type ||
      !put->dst.value.data_offset.value.declared_type ||
      !put->fabric_contract.counted || !put->fabric_contract.reports_fabric ||
      put->fabric_contract.endpoint != FabricEndpointKind::Unicast ||
      put->fabric_contract.shared_access != FabricSharedAccess::Read ||
      put->fabric_contract.completion !=
          ptx_frontend::base::AsyncCompletionKind::MbarrierCompleteTx16B ||
      put->fabric_contract.required_mbarrier_layout !=
          ptx_frontend::base::MbarrierLayout::V1)
    return 3;
  const auto* submit = dynamic_cast<const FabricSubmit*>(
      module->functions.front().body[1].get());
  const auto* wait =
      dynamic_cast<const FabricWait*>(module->functions.front().body[2].get());
  if (!submit || !wait ||
      wait->fabric_contract.completion !=
          ptx_frontend::base::AsyncCompletionKind::FabricReadWait)
    return 4;
  return validateModule(*module) ? 0 : 5;
}
